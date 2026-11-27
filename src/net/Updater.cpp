// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/Updater.h"

#include "core/JsonUtil.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QVersionNumber>
#include <memory>

namespace pts {

namespace {

const QString kZipName = QStringLiteral("PoeTradeSearch-win64.zip");
const QString kSetupName = QStringLiteral("PoeTradeSearch-Setup.exe");

QNetworkRequest request(const QUrl& url, int timeoutMs)
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("PoeTradeSearchQt/%1").arg(QStringLiteral(PTS_VERSION)));
    req.setRawHeader("Accept", "application/vnd.github+json, application/octet-stream;q=0.9, */*;q=0.8");
    req.setTransferTimeout(timeoutMs);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    return req;
}

// 압축을 풀어 실행 폴더에 덮어쓰고 재실행. 실패해도 기존 버전으로 다시 켬
const char* kScript = R"PS1(param([int]$ProcId, [string]$Zip, [string]$Dest, [string]$Exe, [string]$Log, [string]$DataDir)
function Log($m) { Add-Content -LiteralPath $Log -Value ("{0} {1}" -f (Get-Date -Format s), $m) }
function Relaunch([string]$Flag) {
    $a = @($Flag, '--wait-shutdown')
    if ($DataDir) { $a += @('--restart-data-dir', ('"{0}"' -f $DataDir)) }
    Start-Process -FilePath $Exe -ArgumentList $a
}
try {
    Log "waiting for process $ProcId"
    try { Wait-Process -Id $ProcId -Timeout 30 -ErrorAction Stop } catch {}
    Start-Sleep -Milliseconds 500
    $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("pts_update_" + [guid]::NewGuid().ToString("N"))
    Expand-Archive -LiteralPath $Zip -DestinationPath $tmp -Force
    $src = $tmp
    $items = @(Get-ChildItem -LiteralPath $tmp)
    if ($items.Count -eq 1 -and $items[0].PSIsContainer) { $src = $items[0].FullName }
    Get-ChildItem -LiteralPath $src -Force | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $Dest -Recurse -Force }
    Remove-Item -LiteralPath $tmp -Recurse -Force
    Remove-Item -LiteralPath $Zip -Force
    Log "updated"
    Relaunch '--updated'
} catch {
    Log ("failed: " + $_)
    Relaunch '--update-failed'
}
)PS1";

// 설치판: 새 설치 프로그램을 조용히 실행. 성공하면 설치 프로그램이 앱을 다시 켬 (installer.iss [Run])
const char* kSetupScript = R"PS1(param([int]$ProcId, [string]$Setup, [string]$Exe, [string]$Log, [string]$DataDir)
function Log($m) { Add-Content -LiteralPath $Log -Value ("{0} {1}" -f (Get-Date -Format s), $m) }
function RelaunchFailed() {
    $a = @('--update-failed', '--wait-shutdown')
    if ($DataDir) { $a += @('--restart-data-dir', ('"{0}"' -f $DataDir)) }
    Start-Process -FilePath $Exe -ArgumentList $a
}
try {
    Log "waiting for process $ProcId"
    try { Wait-Process -Id $ProcId -Timeout 30 -ErrorAction Stop } catch {}
    Start-Sleep -Milliseconds 500
    $sArgs = @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-')
    if ($DataDir) { $sArgs += ('/PTSDATADIR="{0}"' -f $DataDir) }
    $p = Start-Process -FilePath $Setup -ArgumentList $sArgs -Wait -PassThru
    Log ("setup exit " + $p.ExitCode)
    Remove-Item -LiteralPath $Setup -Force -ErrorAction SilentlyContinue
    if ($p.ExitCode -ne 0) { RelaunchFailed }
} catch {
    Log ("failed: " + $_)
    RelaunchFailed
}
)PS1";

} // namespace

bool Updater::isInstalled()
{
    return QFileInfo::exists(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("unins000.exe")));
}

Updater::Updater(const QString& repo, const QString& workDir, QObject* parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this)), m_repo(repo), m_dir(workDir)
{
}

QString Updater::currentVersion()
{
    return QStringLiteral(PTS_VERSION);
}

bool Updater::isNewer(const QString& current, const QString& candidate)
{
    auto strip = [](QString v) {
        if (v.startsWith(QLatin1Char('v')) || v.startsWith(QLatin1Char('V')))
            v.remove(0, 1);
        return QVersionNumber::fromString(v); // "0.2.0-ci.5" -> 0.2.0
    };
    const QVersionNumber a = strip(current);
    const QVersionNumber b = strip(candidate);
    if (b.isNull())
        return false;
    return QVersionNumber::compare(b, a) > 0;
}

bool Updater::canSelfInstall(QString* why)
{
#if defined(Q_OS_WIN) && defined(PTS_RELEASE_BUILD)
    if (isInstalled())
        return true; // 설치 프로그램이 알아서 덮어씀 (모든 사용자 설치면 UAC 창 뜸)
    const QString dir = QCoreApplication::applicationDirPath();
    QFile probe(QDir(dir).filePath(QStringLiteral(".update_write_test")));
    if (!probe.open(QIODevice::WriteOnly)) {
        if (why)
            *why = QStringLiteral("실행 폴더에 쓸 수 없습니다 (Program Files 등)");
        return false;
    }
    probe.close();
    probe.remove();
    return true;
#else
    if (why)
        *why = QStringLiteral("직접 빌드한 개발 버전은 자동 설치를 지원하지 않습니다");
    return false;
#endif
}

QString Updater::logPath() const
{
    return QDir(m_dir).filePath(QStringLiteral("update.log"));
}

void Updater::check(std::function<void(const Release*, const QString&)> cb)
{
    if (!enabled()) {
        cb(nullptr, QStringLiteral("업데이트 저장소가 설정되지 않았습니다"));
        return;
    }
    if (m_busy) {
        cb(nullptr, QStringLiteral("이미 업데이트 작업이 진행 중입니다"));
        return;
    }
    m_busy = true;
    const QUrl url(QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(m_repo));
    QNetworkReply* r = m_nam->get(request(url, 15000));
    connect(r, &QNetworkReply::finished, this, [this, r, cb]() {
        r->deleteLater();
        m_busy = false;
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 404) {
            cb(nullptr, QString()); // 릴리즈 아직 없음
            return;
        }
        if (r->error() != QNetworkReply::NoError) {
            cb(nullptr, r->errorString());
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        Release rel;
        rel.tag = o.value(QStringLiteral("tag_name")).toString();
        rel.version = rel.tag.startsWith(QLatin1Char('v')) ? rel.tag.mid(1) : rel.tag;
        rel.page = o.value(QStringLiteral("html_url")).toString();
        for (const auto& av : o.value(QStringLiteral("assets")).toArray()) {
            const QJsonObject a = av.toObject();
            const QString name = a.value(QStringLiteral("name")).toString();
            const QString url = a.value(QStringLiteral("browser_download_url")).toString();
            if (name == kZipName)
                rel.zipUrl = url;
            else if (name == kZipName + QStringLiteral(".sha256"))
                rel.shaUrl = url;
            else if (name == kSetupName)
                rel.setupUrl = url;
            else if (name == kSetupName + QStringLiteral(".sha256"))
                rel.setupShaUrl = url;
        }
        if (o.value(QStringLiteral("draft")).toBool() || o.value(QStringLiteral("prerelease")).toBool() ||
            !isNewer(currentVersion(), rel.tag)) {
            cb(nullptr, QString());
            return;
        }
        cb(&rel, QString());
    });
}

void Updater::download(const Release& rel, std::function<void(const QString&, const QString&)> cb)
{
    if (m_busy) {
        cb(QString(), QStringLiteral("이미 받는 중입니다"));
        return;
    }
    // 설치판은 설치 프로그램, 포터블은 zip
    const bool setup = isInstalled() && !rel.setupUrl.isEmpty() && !rel.setupShaUrl.isEmpty();
    const QString fileName = setup ? kSetupName : kZipName;
    const QString fileUrl = setup ? rel.setupUrl : rel.zipUrl;
    const QString shaUrl = setup ? rel.setupShaUrl : rel.shaUrl;
    if (fileUrl.isEmpty() || shaUrl.isEmpty()) {
        cb(QString(), QStringLiteral("릴리즈에 %1 / .sha256 파일이 없습니다").arg(fileName));
        return;
    }
    m_busy = true;
    QDir().mkpath(m_dir);
    const QString zipPath = QDir(m_dir).filePath(fileName);

    QNetworkReply* shaReply = m_nam->get(request(QUrl(shaUrl), 30000));
    connect(shaReply, &QNetworkReply::finished, this, [this, shaReply, fileUrl, zipPath, cb]() {
        shaReply->deleteLater();
        const QByteArray expected = shaReply->readAll().trimmed().split(' ').value(0).toLower();
        if (shaReply->error() != QNetworkReply::NoError || expected.size() != 64) {
            m_busy = false;
            cb(QString(), QStringLiteral("체크섬 파일을 받지 못했습니다: %1").arg(shaReply->errorString()));
            return;
        }
        QNetworkReply* zr = m_nam->get(request(QUrl(fileUrl), 300000));
        connect(zr, &QNetworkReply::finished, this, [this, zr, expected, zipPath, cb]() {
            zr->deleteLater();
            m_busy = false;
            const QByteArray data = zr->readAll();
            if (zr->error() != QNetworkReply::NoError || data.isEmpty()) {
                cb(QString(), QStringLiteral("업데이트 파일을 받지 못했습니다: %1").arg(zr->errorString()));
                return;
            }
            const QByteArray got = QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
            if (got != expected) {
                cb(QString(), QStringLiteral("체크섬이 일치하지 않습니다 (다운로드 중 손상되었거나 파일이 변경되었습니다)"));
                return;
            }
            QString err;
            if (!json::writeRaw(zipPath, data, &err)) {
                cb(QString(), err);
                return;
            }
            cb(zipPath, QString());
        });
    });
}

bool Updater::install(const QString& zipPath, QString* error, const QString& dataDir)
{
    QString why;
    if (!canSelfInstall(&why)) {
        if (error)
            *error = why;
        return false;
    }
    const bool setup = zipPath.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive);
    const QString script = QDir(m_dir).filePath(setup ? QStringLiteral("run-setup.ps1") : QStringLiteral("apply-update.ps1"));
    QString err;
    if (!json::writeRaw(script, QByteArray(setup ? kSetupScript : kScript), &err)) {
        if (error)
            *error = err;
        return false;
    }
    QStringList args = {QStringLiteral("-NoProfile"),
                        QStringLiteral("-ExecutionPolicy"),
                        QStringLiteral("Bypass"),
                        QStringLiteral("-WindowStyle"),
                        QStringLiteral("Hidden"),
                        QStringLiteral("-File"),
                        QDir::toNativeSeparators(script),
                        QStringLiteral("-ProcId"),
                        QString::number(QCoreApplication::applicationPid())};
    if (setup) {
        args << QStringLiteral("-Setup") << QDir::toNativeSeparators(zipPath);
    } else {
        args << QStringLiteral("-Zip") << QDir::toNativeSeparators(zipPath) << QStringLiteral("-Dest")
             << QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    }
    args << QStringLiteral("-Exe") << QDir::toNativeSeparators(QCoreApplication::applicationFilePath())
         << QStringLiteral("-Log") << QDir::toNativeSeparators(logPath());
    if (!dataDir.isEmpty())
        args << QStringLiteral("-DataDir") << QDir::toNativeSeparators(dataDir);
    if (!QProcess::startDetached(QStringLiteral("powershell.exe"), args)) {
        if (error)
            *error = QStringLiteral("PowerShell을 실행하지 못했습니다");
        return false;
    }
    return true;
}

} // namespace pts
