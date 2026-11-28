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
    QString value;     // "{Run}", "{Last}", "{Pause}", "{Restart}", "{Link}URL{Link}", "{Enter}/hideout{Enter}" ...
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
    int searchBeforeDay = -1; // 등록일 제한(일). -1 = 자동: 즉시 구입만이면 언제든, 직접 거래 포함이면 3일
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
    QString tradeStatus = QStringLiteral("securable"); // 기본: 즉시 구입만
    // 가격 화폐 필터 (거래소 "즉시 구입 가격" 옵션). "" = 등가물(모든 화폐)
    // 기본은 기준 화폐 + 신성만 -> 색채/쥬얼러 같은 잡화폐로 올린 매물 거름
    QString priceCurrency = QStringLiteral("chaos_divine");       // PoE1
    QString priceCurrencyPoe2 = QStringLiteral("exalted_divine"); // PoE2
    int dataMaxAgeDays = 7;
    bool autoPriceCheck = true;  // 창 뜰 때 바로 시세 조회 (원본: search_auto_delay > 0)
    bool autoPriceRefresh = true; // 조건을 바꾸면 시세 재조회
    bool exchangeSearch = true; // 일괄 교환되는 아이템(화폐·조각 등)은 교환 매물도 함께 조회
    QString popupPosition = QStringLiteral("center"); // 창 위치: center(화면 가운데) / cursor(커서 근처) / last(마지막 위치)
    bool closeOnOutsideClick = true; // 팝업 바깥(게임 화면)을 누르면 닫음
    bool clipboardWatch = true;      // 게임에서 Ctrl+C 로 복사해도 창 띄움 ({Run} 단축키와 별개)
    QString appUpdate = QStringLiteral("notify"); // 앱 업데이트: notify(알림만) / auto(자동 설치) / off
    int searchStatRange = 0;     // 옵션 최소값을 값의 N% 낮춰서 채움 (0 = 일치)
    bool collapseListings = true; // 시세 조회 시 계정별 묶기

    QVector<Shortcut> shortcuts;

    static constexpr int kConfigVersion = 9;
    int configVersion = kConfigVersion;
    bool migrated = false; // load 때 마이그레이션 했으면 true -> 저장 필요

    bool load(const QString& path, QString* error = nullptr);
    void migrate(); // 예전 설정 고치기 (load 에서 자동 호출)
    bool save(const QString& path, QString* error = nullptr) const;
    static Config defaults();

    QString leagueFor(Game g) const { return g == Game::Poe1 ? league : leaguePoe2; }
    void setLeague(Game g, const QString& l) { (g == Game::Poe1 ? league : leaguePoe2) = l; }
    QString priceCurrencyFor(Game g) const { return g == Game::Poe1 ? priceCurrency : priceCurrencyPoe2; }
    QStringList byTypeList() const;
};

} // namespace pts
