// SPDX-License-Identifier: GPL-3.0-or-later
//
// POE 거래소 검색 (C++/Qt 포팅)
// 원본: PoeTradeSearch by phiDelPark (https://github.com/phiDelPark/PoeTradeSearch), GPL-3.0
#include "app/Application.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTextStream>
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
                                 waitShutdown ? QStringLiteral("이전 프로그램이 종료되지 않았음")
                                              : QStringLiteral("이 프로그램은 이미 실행 중임"));
        return 1;
    }

    pts::Application a(dataDir);
    QString err;
    if (!a.start(&err)) {
        QMessageBox::critical(nullptr, QStringLiteral("시작 실패"), err);
        return 2;
    }
    // 개발용: PTS_DEV_ITEM=<아이템 텍스트 파일> [PTS_DEV_SHOT=<png>] 로 팝업 확인
    const QString devItem = qEnvironmentVariable("PTS_DEV_ITEM");
    if (!devItem.isEmpty()) {
        QFile f(devItem);
        if (f.open(QIODevice::ReadOnly)) {
            const QString text = QString::fromUtf8(f.readAll());
            QTimer::singleShot(300, &a, [&a, text]() { a.devShowItem(text); });
            const QString shot = qEnvironmentVariable("PTS_DEV_SHOT");
            if (!shot.isEmpty()) {
                QTimer::singleShot(1500, &a, [&a, shot]() {
                    if (QWidget* w = a.popupWidget())
                        w->grab().save(shot);
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
