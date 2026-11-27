// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <functional>

class QNetworkAccessManager;

namespace pts {

// GitHub Release 기반 자동 업데이트
//   1) api.github.com/repos/<repo>/releases/latest 로 최신 태그 확인
//   2) 설치판(설치 폴더에 unins000.exe): PoeTradeSearch-Setup.exe + .sha256
//      포터블(zip)판: PoeTradeSearch-win64.zip + .sha256  -> 해시 검증
//   3) PowerShell 스크립트를 띄우고 앱 종료 -> 스크립트가 앱 종료를 기다렸다가
//      설치판: Setup.exe /VERYSILENT (설치 프로그램이 끝나고 앱을 다시 켬)
//      포터블: 압축 풀어 실행 폴더에 덮어쓰고 다시 켬
// 실행 폴더 안의 data/ (설정/캐시) 는 배포 파일에 없으니 그대로 남음.
class Updater : public QObject {
    Q_OBJECT
public:
    struct Release {
        QString tag;     // "v0.2.1"
        QString version; // "0.2.1"
        QString page;    // 릴리즈 웹 페이지
        QString zipUrl;
        QString shaUrl;
        QString setupUrl;
        QString setupShaUrl;
    };

    // 설치 프로그램으로 깔린 것인지 (설치 폴더에 Inno Setup 제거 프로그램이 있음)
    static bool isInstalled();

    Updater(const QString& repo, const QString& workDir, QObject* parent = nullptr);

    bool enabled() const { return !m_repo.isEmpty(); }
    static QString currentVersion();
    static bool isNewer(const QString& current, const QString& candidate);
    // 배포 빌드 + Windows + 실행 폴더 쓰기 가능할 때만 스스로 교체
    static bool canSelfInstall(QString* why = nullptr);

    // newer == nullptr 이면 최신
    void check(std::function<void(const Release* newer, const QString& error)> cb);
    void download(const Release& r, std::function<void(const QString& zipPath, const QString& error)> cb);
    // 성공하면 호출한 쪽이 바로 앱을 종료해야 함
    bool install(const QString& zipPath, QString* error, const QString& dataDir = QString());

    bool busy() const { return m_busy; }
    QString logPath() const;

private:
    QNetworkAccessManager* m_nam;
    QString m_repo;
    QString m_dir;
    bool m_busy = false;
};

} // namespace pts
