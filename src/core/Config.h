// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Types.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace pts {

struct Shortcut {
    int keycode = 0;   // Windows VK 코드 (원본 Config.txt 와 같음)
    int modifiers = 0; // 1=Alt 2=Ctrl 4=Shift 8=Win (RegisterHotKey MOD_* 와 같음)
    QString value;     // "{Run}", "{Pause}", "{Restart}", "{Link}URL{Link}", "{Enter}/hideout{Enter}" ...
};

// 원본 Config.txt 포맷 호환 + 확장 키
struct Config {
    // ---- 원본 키
    QString league = QStringLiteral("Standard"); // PoE1
    int serverType = 0;                          // 0 자동(아이템 언어) / 1 한국 / 2 영국
    int serverTimeout = 5;
    bool serverRedirect = false;
    int searchAutoDelay = 0;
    int searchListCount = 20;
    int searchPriceMinimum = 0;
    int searchBeforeDay = 7;
    bool autoCheckUnique = true;
    bool autoCheckTotalres = true;
    bool autoSelectPseudo = true;
    QString autoSelectCorrupt;
    QString autoSelectByType;
    bool checkUpdates = true;
    bool useCtrlWheel = false; // 1차 미지원, 값만 보존

    // ---- 추가 키
    QString leaguePoe2 = QStringLiteral("Standard");
    QString game = QStringLiteral("auto"); // auto / poe1 / poe2
    QString tradeStatus = QStringLiteral("available");
    int dataMaxAgeDays = 7;
    bool autoPriceCheck = false; // 창 뜰 때 바로 시세 조회 (원본: search_auto_delay > 0)
    int searchStatRange = 10;    // 옵션 최소값을 값의 N% 낮춰서 채움 (0 = 정확값)
    bool collapseListings = true; // 시세 조회 시 계정별 묶기

    QVector<Shortcut> shortcuts;

    static constexpr int kConfigVersion = 2;
    int configVersion = kConfigVersion;
    bool migrated = false; // load 때 마이그레이션 했으면 true -> 저장 필요

    bool load(const QString& path, QString* error = nullptr);
    void migrate(); // 예전 설정 고치기 (load 에서 자동 호출)
    bool save(const QString& path, QString* error = nullptr) const;
    static Config defaults();

    QString leagueFor(Game g) const { return g == Game::Poe1 ? league : leaguePoe2; }
    void setLeague(Game g, const QString& l) { (g == Game::Poe1 ? league : leaguePoe2) = l; }
    QStringList byTypeList() const;
};

} // namespace pts
