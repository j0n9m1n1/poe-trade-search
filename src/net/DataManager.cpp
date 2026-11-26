// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/DataManager.h"

#include "core/JsonUtil.h"
#include "core/Keywords.h"
#include "net/Endpoints.h"
#include "net/TradeClient.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <memory>

namespace pts {

DataManager::DataManager(const QString& dataDir, TradeClient* client, QObject* parent)
    : QObject(parent), m_dataDir(dataDir), m_client(client)
{
}

QStringList DataManager::kinds()
{
    return {QStringLiteral("leagues"), QStringLiteral("stats"), QStringLiteral("items"), QStringLiteral("static"),
            QStringLiteral("filters")};
}

QString DataManager::cacheDir(Game g, Lang l) const
{
    return QDir(m_dataDir).filePath(QStringLiteral("cache/%1/%2").arg(gameKey(g), langKey(l)));
}

bool DataManager::hasCache(Game g) const
{
    for (Lang l : {Lang::Ko, Lang::En}) {
        if (QFileInfo::exists(QDir(cacheDir(g, l)).filePath(QStringLiteral("stats.json"))))
            return true;
    }
    return false;
}

QDateTime DataManager::updatedAt(Game g) const
{
    QDateTime best;
    for (Lang l : {Lang::Ko, Lang::En}) {
        QFile f(QDir(cacheDir(g, l)).filePath(QStringLiteral("updated.txt")));
        if (f.open(QIODevice::ReadOnly)) {
            const QDateTime t = QDateTime::fromString(QString::fromUtf8(f.readAll()).trimmed(), Qt::ISODate);
            if (t.isValid() && (!best.isValid() || t > best))
                best = t;
        }
    }
    return best;
}

QUrl DataManager::matcherUrl(Game g, Lang l)
{
    // Awakened PoE Trade (PoE1) / Exiled Exchange 2 (PoE2) 의 stats.ndjson, MIT 라이선스
    const QString repo = g == Game::Poe1 ? QStringLiteral("SnosMe/awakened-poe-trade")
                                         : QStringLiteral("Kvan7/Exiled-Exchange-2");
    return QUrl(QStringLiteral("https://raw.githubusercontent.com/%1/master/renderer/public/data/%2/stats.ndjson")
                    .arg(repo, langKey(l)));
}

bool DataManager::isStale(Game g, int maxAgeDays) const
{
    if (!hasCache(g))
        return true;
    for (Lang l : {Lang::Ko, Lang::En}) {
        const QDir d(cacheDir(g, l));
        if (QFileInfo::exists(d.filePath(QStringLiteral("stats.json"))) &&
            !QFileInfo::exists(d.filePath(QStringLiteral("matchers.ndjson"))))
            return true; // 매처 데이터 생기기 전 캐시
    }
    const QDateTime t = updatedAt(g);
    return !t.isValid() || (maxAgeDays > 0 && t.daysTo(QDateTime::currentDateTimeUtc()) >= maxAgeDays);
}

void DataManager::update(Game g, std::function<void(bool, const QString&)> done)
{
    if (m_updating[gi(g)]) {
        if (done)
            done(false, QStringLiteral("이미 업데이트 중"));
        return;
    }
    m_updating[gi(g)] = true;
    for (Lang l : {Lang::Ko, Lang::En})
        QDir(cacheDir(g, l) + QStringLiteral(".new")).removeRecursively();

    struct Job {
        Lang lang;
        QString kind;
    };
    QVector<Job> jobs;
    for (Lang l : {Lang::En, Lang::Ko}) {
        for (const QString& k : kinds())
            jobs.push_back({l, k});
        jobs.push_back({l, QStringLiteral("matchers")}); // 선택 (실패해도 됨)
    }
    auto remaining = std::make_shared<int>(jobs.size());
    auto okLang = std::make_shared<QHash<int, int>>(); // lang -> 성공 개수
    auto lastError = std::make_shared<QString>();
    const quint64 token = m_client->newToken();

    emit progress(QStringLiteral("%1 거래소 데이터 받는 중...").arg(g == Game::Poe1 ? QStringLiteral("PoE1") : QStringLiteral("PoE2")));

    for (const Job& j : jobs) {
        const QString dir = cacheDir(g, j.lang);
        // 임시 폴더에 받고 다 되면 교체
        const QString tmpDir = dir + QStringLiteral(".new");
        const bool isMatchers = j.kind == QStringLiteral("matchers");
        m_client->get(
            TradeClient::Policy::Data, isMatchers ? matcherUrl(g, j.lang) : endpoints::data(g, j.lang, j.kind),
            [this, g, j, dir, tmpDir, isMatchers, remaining, okLang, lastError, done, total = int(jobs.size())](
                int status, const QByteArray& body, const QString& error) {
                if (isMatchers) {
                    // 첫 줄이 JSON 객체면 ndjson 으로 봄
                    const bool looks = error.isEmpty() && status == 200 &&
                                       QJsonDocument::fromJson(body.left(body.indexOf('\n'))).isObject();
                    if (looks)
                        json::writeRaw(QDir(tmpDir).filePath(QStringLiteral("matchers.ndjson")), body);
                    else
                        qWarning() << "matcher data download failed" << langKey(j.lang) << status << error;
                }
                bool ok = !isMatchers && error.isEmpty() && status == 200 &&
                          QJsonDocument::fromJson(body).object().contains(QStringLiteral("result"));
                if (isMatchers) {
                    // 아래 성공 카운트에 안 넣음
                } else if (ok) {
                    QString werr;
                    ok = json::writeRaw(QDir(tmpDir).filePath(j.kind + QStringLiteral(".json")), body, &werr);
                    if (!ok)
                        *lastError = werr;
                } else {
                    *lastError = QStringLiteral("%1 %2: %3")
                                     .arg(langKey(j.lang), j.kind, error.isEmpty() ? QString::number(status) : error);
                }
                if (ok && !isMatchers)
                    (*okLang)[li(j.lang)]++;
                emit progress(QStringLiteral("데이터 받는 중 %1/%2").arg(total - *remaining + 1).arg(total));

                if (--(*remaining) > 0)
                    return;

                // 한 언어 단위로 전부 성공한 것만 교체
                bool any = false;
                for (Lang l : {Lang::Ko, Lang::En}) {
                    const QString d = cacheDir(g, l);
                    const QString t = d + QStringLiteral(".new");
                    if (okLang->value(li(l)) == kinds().size()) {
                        // 매처를 못 받았으면 예전 것 유지
                        const QString oldM = QDir(d).filePath(QStringLiteral("matchers.ndjson"));
                        const QString newM = QDir(t).filePath(QStringLiteral("matchers.ndjson"));
                        if (!QFileInfo::exists(newM) && QFileInfo::exists(oldM))
                            QFile::copy(oldM, newM);
                        QDir(d).removeRecursively();
                        QDir().rename(t, d);
                        json::writeRaw(QDir(d).filePath(QStringLiteral("updated.txt")),
                                       QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toUtf8());
                        any = true;
                    } else {
                        QDir(t).removeRecursively();
                    }
                }
                m_updating[gi(g)] = false;
                if (done)
                    done(any, any ? QString() : *lastError);
            },
            token);
    }
}

bool DataManager::load(Game g, GameDb* out, const Keywords& kw, QString* error) const
{
    bool any = false;
    QString err;
    for (Lang l : {Lang::En, Lang::Ko}) {
        QString e;
        if ((*out)[l].load(cacheDir(g, l), &e))
            any = true;
        else if (!e.isEmpty())
            err = e;
    }
    (*out)[Lang::En].resolveScopes(kw, nullptr);
    (*out)[Lang::Ko].resolveScopes(kw, out->loaded(Lang::En) ? &(*out)[Lang::En] : nullptr);
    for (Lang l : {Lang::En, Lang::Ko}) {
        const QString mf = QDir(cacheDir(g, l)).filePath(QStringLiteral("matchers.ndjson"));
        if (QFileInfo::exists(mf) && !out->matchers[li(l)].load(mf))
            qWarning() << "matcher data load failed" << mf;
    }
    if (!any && error)
        *error = err;
    return any;
}

} // namespace pts
