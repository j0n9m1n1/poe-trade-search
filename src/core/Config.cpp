// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Config.h"

#include "core/JsonUtil.h"

#include <QJsonArray>
#include <QJsonObject>

namespace pts {

Config Config::defaults()
{
    Config c;
    c.shortcuts = {
        {0x44, 1, QStringLiteral("{Run} 아이템 검색 (Ctrl+Alt+C 를 대신 눌러서 가져옴)")}, // Alt+D
        {0, 0, QStringLiteral("{Last} 마지막 창 다시 열기")},
        {116, 0, QStringLiteral("{Enter}/hideout{Enter}")}, // F5 은신처
        {115, 0, QStringLiteral("{Enter}/exit{Enter}")},    // F4 로그아웃 (캐릭터 선택)
        {122, 0, QStringLiteral("{Restart} 프로그램 재시작")},
    };
    return c;
}

void Config::migrate()
{
    if (configVersion >= kConfigVersion)
        return;
    if (configVersion < 2) {
        // v2: 위키/닌자 단축키 제거, 은신처/로그아웃 채팅 매크로 기본 추가
        QVector<Shortcut> keep;
        for (const Shortcut& s : shortcuts) {
            const QString v = s.value.trimmed().toLower();
            if (v.startsWith(QStringLiteral("{wiki}")) || v == QStringLiteral("{link}https://poe.ninja/{link}"))
                continue;
            keep.push_back(s);
        }
        auto has = [&](const QString& cmd) {
            for (const Shortcut& s : keep) {
                if (s.value.toLower().contains(cmd))
                    return true;
            }
            return false;
        };
        auto keyUsed = [&](int vk) {
            for (const Shortcut& s : keep) {
                if (s.keycode == vk && s.modifiers == 0)
                    return true;
            }
            return false;
        };
        if (!has(QStringLiteral("/hideout")))
            keep.push_back({keyUsed(116) ? 0 : 116, 0, QStringLiteral("{Enter}/hideout{Enter}")});
        if (!has(QStringLiteral("/exit")))
            keep.push_back({keyUsed(115) ? 0 : 115, 0, QStringLiteral("{Enter}/exit{Enter}")});
        shortcuts = keep;
    }
    if (configVersion < 3) {
        // v3: 단축키로 뜨면 바로 시세 조회, 마지막 창 다시 열기 명령 추가
        autoPriceCheck = true;
        bool hasLast = false;
        for (const Shortcut& s : shortcuts)
            hasLast = hasLast || s.value.toLower().startsWith(QStringLiteral("{last}"));
        if (!hasLast)
            shortcuts.insert(qMin<qsizetype>(1, shortcuts.size()), {0, 0, QStringLiteral("{Last} 마지막 창 다시 열기")});
    }
    if (configVersion < 4) {
        // v4: 기본 판매 방식을 즉시 구입만으로 (예전 기본값 그대로 쓰던 경우만)
        if (tradeStatus == QStringLiteral("available"))
            tradeStatus = QStringLiteral("securable");
    }
    if (configVersion < 8) {
        // v8: 창 위치 기본을 화면 가운데로
        popupPosition = QStringLiteral("center");
    }
    if (configVersion < 7) {
        // v7: 옵션 값 기본을 "일치"(0%)로 (예전 기본값 10% 를 그대로 쓰던 경우만)
        if (searchStatRange == 10)
            searchStatRange = 0;
    }
    if (configVersion < 6) {
        // v6: {Run} 기본 단축키 Alt+D. Ctrl+C 감시는 별도 설정으로 분리
        //  - {Run} 에 키가 없던 설정 -> Alt+D (다른 단축키가 Alt+D 를 쓰면 그대로), Ctrl+C 감시 켬
        //  - {Run} 에 직접 키를 지정했던 설정 -> 예전처럼 Ctrl+C 감시 끔
        bool altDUsed = false;
        for (const Shortcut& s : shortcuts)
            altDUsed = altDUsed || (s.keycode == 0x44 && s.modifiers == 1);
        bool runHadKey = false;
        for (Shortcut& s : shortcuts) {
            if (!s.value.trimmed().toLower().startsWith(QStringLiteral("{run}")))
                continue;
            if (s.keycode > 0) {
                runHadKey = true;
            } else if (!altDUsed) {
                s.keycode = 0x44;
                s.modifiers = 1;
            }
        }
        clipboardWatch = !runHadKey;
    }
    if (configVersion < 5) {
        // v5: 등록일 제한 기본값을 자동으로 (예전 기본값 7일을 그대로 쓰던 경우만)
        if (searchBeforeDay == 7)
            searchBeforeDay = -1;
    }
    if (configVersion < 9) {
        // v9: 은신처 기본 단축키 F2 -> F5. 예전 기본값(F2) 그대로이고 F5 가 비어 있을 때만 옮김
        bool f5Used = false;
        for (const Shortcut& s : shortcuts)
            f5Used = f5Used || (s.keycode == 116 && s.modifiers == 0);
        for (Shortcut& s : shortcuts) {
            if (!f5Used && s.keycode == 113 && s.modifiers == 0
                && s.value.trimmed().compare(QStringLiteral("{Enter}/hideout{Enter}"), Qt::CaseInsensitive) == 0)
                s.keycode = 116;
        }
    }
    configVersion = kConfigVersion;
    migrated = true;
}

QStringList Config::byTypeList() const
{
    return autoSelectByType.split(QLatin1Char(','), Qt::SkipEmptyParts);
}

bool Config::load(const QString& path, QString* error)
{
    QJsonDocument doc;
    if (!json::readFile(path, &doc, error))
        return false;
    const QJsonObject root = doc.object();
    const QJsonObject o = root.value(QStringLiteral("options")).toObject();
    const Config d = defaults();

    auto s = [&](const char* k, const QString& def) {
        const QJsonValue v = o.value(QLatin1String(k));
        return v.isString() ? v.toString() : def;
    };
    auto i = [&](const char* k, int def) {
        const QJsonValue v = o.value(QLatin1String(k));
        return v.isDouble() ? v.toInt() : def;
    };
    auto b = [&](const char* k, bool def) {
        const QJsonValue v = o.value(QLatin1String(k));
        return v.isBool() ? v.toBool() : def;
    };

    league = s("league", d.league);
    serverType = i("server_type", d.serverType);
    serverTimeout = qBound(1, i("server_timeout", d.serverTimeout), 60);
    serverRedirect = b("server_redirect", d.serverRedirect);
    searchAutoDelay = i("search_auto_delay", d.searchAutoDelay);
    searchListCount = qBound(10, i("search_list_count", d.searchListCount), 80);
    searchPriceMinimum = i("search_price_minimum", i("search_price_min", d.searchPriceMinimum));
    searchBeforeDay = i("search_before_day", d.searchBeforeDay);
    autoCheckUnique = b("auto_check_unique", d.autoCheckUnique);
    autoCheckTotalres = b("auto_check_totalres", d.autoCheckTotalres);
    autoSelectPseudo = b("auto_select_pseudo", d.autoSelectPseudo);
    autoSelectCorrupt = s("auto_select_corrupt", d.autoSelectCorrupt);
    autoSelectByType = s("auto_select_bytype", d.autoSelectByType);
    checkUpdates = b("check_updates", d.checkUpdates);
    useCtrlWheel = b("use_ctrl_wheel", b("ctrl_wheel", d.useCtrlWheel));

    leaguePoe2 = s("league_poe2", d.leaguePoe2);
    game = s("game", d.game);
    tradeStatus = s("trade_status", d.tradeStatus);
    priceCurrency = s("price_currency", d.priceCurrency);
    priceCurrencyPoe2 = s("price_currency_poe2", d.priceCurrencyPoe2);
    dataMaxAgeDays = i("data_max_age_days", d.dataMaxAgeDays);
    autoPriceCheck = b("auto_price_check", d.autoPriceCheck);
    autoPriceRefresh = b("auto_price_refresh", d.autoPriceRefresh);
    exchangeSearch = b("exchange_search", d.exchangeSearch);
    popupPosition = s("popup_position", b("popup_near_cursor", true) ? QStringLiteral("cursor") : QStringLiteral("last"));
    if (popupPosition != QStringLiteral("cursor") && popupPosition != QStringLiteral("last"))
        popupPosition = QStringLiteral("center");
    closeOnOutsideClick = b("close_on_outside_click", d.closeOnOutsideClick);
    clipboardWatch = b("clipboard_watch", d.clipboardWatch);
    appUpdate = s("app_update", d.appUpdate);
    if (appUpdate != QStringLiteral("auto") && appUpdate != QStringLiteral("off"))
        appUpdate = QStringLiteral("notify");
    searchStatRange = qBound(0, i("search_stat_range", d.searchStatRange), 50);
    collapseListings = b("collapse_listings", d.collapseListings);

    shortcuts.clear();
    const QJsonValue sv = root.value(QStringLiteral("shortcuts"));
    if (sv.isArray()) {
        for (const auto& v : sv.toArray()) {
            const QJsonObject x = v.toObject();
            Shortcut sc;
            sc.keycode = x.value(QStringLiteral("keycode")).toInt();
            sc.modifiers = x.value(QStringLiteral("modifiers")).toInt();
            sc.value = x.value(QStringLiteral("value")).toString();
            shortcuts.push_back(sc);
        }
    } else {
        shortcuts = d.shortcuts;
    }
    configVersion = root.value(QStringLiteral("config_version")).toInt(1);
    migrate();
    return true;
}

bool Config::save(const QString& path, QString* error) const
{
    QJsonObject o;
    o.insert(QStringLiteral("league"), league);
    o.insert(QStringLiteral("league_poe2"), leaguePoe2);
    o.insert(QStringLiteral("game"), game);
    o.insert(QStringLiteral("server_type"), serverType);
    o.insert(QStringLiteral("server_timeout"), serverTimeout);
    o.insert(QStringLiteral("server_redirect"), serverRedirect);
    o.insert(QStringLiteral("trade_status"), tradeStatus);
    o.insert(QStringLiteral("price_currency"), priceCurrency);
    o.insert(QStringLiteral("price_currency_poe2"), priceCurrencyPoe2);
    o.insert(QStringLiteral("search_auto_delay"), searchAutoDelay);
    o.insert(QStringLiteral("auto_price_check"), autoPriceCheck);
    o.insert(QStringLiteral("auto_price_refresh"), autoPriceRefresh);
    o.insert(QStringLiteral("exchange_search"), exchangeSearch);
    o.insert(QStringLiteral("popup_position"), popupPosition);
    o.insert(QStringLiteral("close_on_outside_click"), closeOnOutsideClick);
    o.insert(QStringLiteral("clipboard_watch"), clipboardWatch);
    o.insert(QStringLiteral("app_update"), appUpdate);
    o.insert(QStringLiteral("search_list_count"), searchListCount);
    o.insert(QStringLiteral("search_stat_range"), searchStatRange);
    o.insert(QStringLiteral("collapse_listings"), collapseListings);
    o.insert(QStringLiteral("search_price_minimum"), searchPriceMinimum);
    o.insert(QStringLiteral("search_before_day"), searchBeforeDay);
    o.insert(QStringLiteral("auto_check_unique"), autoCheckUnique);
    o.insert(QStringLiteral("auto_check_totalres"), autoCheckTotalres);
    o.insert(QStringLiteral("auto_select_pseudo"), autoSelectPseudo);
    o.insert(QStringLiteral("auto_select_corrupt"), autoSelectCorrupt);
    o.insert(QStringLiteral("auto_select_bytype"), autoSelectByType);
    o.insert(QStringLiteral("check_updates"), checkUpdates);
    o.insert(QStringLiteral("data_max_age_days"), dataMaxAgeDays);
    o.insert(QStringLiteral("use_ctrl_wheel"), useCtrlWheel);

    QJsonArray sc;
    for (const Shortcut& s : shortcuts) {
        sc.append(QJsonObject{{QStringLiteral("keycode"), s.keycode},
                              {QStringLiteral("modifiers"), s.modifiers},
                              {QStringLiteral("value"), s.value}});
    }
    QJsonObject root;
    root.insert(QStringLiteral("config_version"), configVersion);
    root.insert(QStringLiteral("options"), o);
    root.insert(QStringLiteral("shortcuts"), sc);
    return json::writeFile(path, QJsonDocument(root), true, true, error);
}

} // namespace pts
