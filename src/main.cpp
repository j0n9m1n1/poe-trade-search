// SPDX-License-Identifier: GPL-3.0-or-later
//
// Poe Trade Search (C++/Qt 포팅)
// 원본: PoeTradeSearch by phiDelPark (https://github.com/phiDelPark/PoeTradeSearch), GPL-3.0
#include "app/Application.h"
#include "platform/Platform.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTextStream>
#include <QTabWidget>
#include <QTimer>
#include <QWidget>
#include <memory>

namespace {

QFile* g_log = nullptr;

void logHandler(QtMsgType type, const QMessageLogContext&, const QString& msg)
{
    static const char* names[] = {"DEBUG", "WARN", "CRIT", "FATAL", "INFO"};
    const char* n = type <= QtInfoMsg ? names[type] : "?";
    const QString line = QStringLiteral("%1 [%2] %3\n")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODate), QLatin1String(n), msg);
    if (g_log && g_log->isOpen()) {
        g_log->write(line.toUtf8());
        g_log->flush();
    }
    fprintf(stderr, "%s", line.toLocal8Bit().constData());
    if (type == QtFatalMsg)
        abort();
}

QString pickDataDir()
{
    const QString env = qEnvironmentVariable("PTS_DATA_DIR");
    if (!env.isEmpty())
        return env;
    // 원본처럼 실행 파일 옆 폴더 (포터블). 못 쓰면 AppData
    const QString portable = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data"));
    if (QDir().mkpath(portable)) {
        QFile probe(QDir(portable).filePath(QStringLiteral(".write_test")));
        if (probe.open(QIODevice::WriteOnly)) {
            probe.close();
            probe.remove();
            return portable;
        }
    }
    const QString app = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(app);
    return app;
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PoeTradeSearch"));
    QApplication::setApplicationVersion(QStringLiteral(PTS_VERSION));
    QApplication::setQuitOnLastWindowClosed(false);
    Q_INIT_RESOURCE(resources);

    const QString dataDir = pickDataDir();

    // 로그 (원본: 실행 파일 이름.log)
    g_log = new QFile(QDir(dataDir).filePath(QStringLiteral("PoeTradeSearch.log")));
    g_log->open(QIODevice::WriteOnly | QIODevice::Truncate);
    qInstallMessageHandler(logHandler);

    // 중복 실행 방지. --wait-shutdown 이면 이전 프로세스 종료를 10초까지 기다림
    const bool waitShutdown = QCoreApplication::arguments().contains(QStringLiteral("--wait-shutdown"));
    QLockFile lock(QDir(dataDir).filePath(QStringLiteral("PoeTradeSearch.lock")));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(waitShutdown ? 10000 : 200)) {
        QMessageBox::information(nullptr, QStringLiteral("중복 실행"),
                                 waitShutdown ? QStringLiteral("이전 프로그램이 아직 종료되지 않았습니다.")
                                              : QStringLiteral("프로그램이 이미 실행 중입니다. 트레이 아이콘을 확인해 주세요."));
        return 1;
    }

    pts::platform::createInstanceMutex();

    pts::Application a(dataDir);
    QString err;
    if (!a.start(&err)) {
        QMessageBox::critical(nullptr, QStringLiteral("시작 실패"), err);
        return 2;
    }
    // 개발용: PTS_DEV_ITEM=<아이템 텍스트 파일> [PTS_DEV_SHOT=<png>] 로 팝업 확인
    // PTS_DEV_ITEM 에 ';' 로 여러 파일 주면 PTS_DEV_STEP(ms) 간격으로 차례로 띄움
    const QStringList devItems = qEnvironmentVariable("PTS_DEV_ITEM").split(QLatin1Char(';'), Qt::SkipEmptyParts);
    if (!devItems.isEmpty()) {
        const int step = qEnvironmentVariableIsSet("PTS_DEV_STEP") ? qEnvironmentVariableIntValue("PTS_DEV_STEP") : 300;
        int at = 300;
        for (const QString& path : devItems) {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly))
                continue;
            const QString text = QString::fromUtf8(f.readAll());
            QTimer::singleShot(at, &a, [&a, text]() { a.devShowItem(text); });
            at += step;
        }
        {
            const QString shot = qEnvironmentVariable("PTS_DEV_SHOT");
            if (!shot.isEmpty()) {
                const int delay = qEnvironmentVariableIsSet("PTS_DEV_SHOT_DELAY") ? qEnvironmentVariableIntValue("PTS_DEV_SHOT_DELAY") : 1500;
                QTimer::singleShot(at + delay, &a, [&a, shot]() {
                    if (QWidget* w = a.popupWidget()) {
                        if (qEnvironmentVariableIsSet("PTS_DEV_TAB")) {
                            if (auto* tabs = w->findChild<QTabWidget*>())
                                tabs->setCurrentIndex(qEnvironmentVariableIntValue("PTS_DEV_TAB"));
                        }
                        w->grab().save(shot);
                    }
                    QCoreApplication::quit();
                });
            }
        }
    }
    const int rc = app.exec();
    qInstallMessageHandler(nullptr);
    g_log->close();
    return rc;
}
