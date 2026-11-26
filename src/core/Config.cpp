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
        {0, 0, QStringLiteral("{Run} 작동 (기본값: Ctrl+C)")},
        {122, 0, QStringLiteral("{Restart} 프로그램 재시작")},
        {72, 2, QStringLiteral("{Wiki} 아이템의 위키 보기")},
        {78, 2, QStringLiteral("{Link}https://poe.ninja/{Link}")},
    };
    return c;
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
    dataMaxAgeDays = i("data_max_age_days", d.dataMaxAgeDays);
    autoPriceCheck = b("auto_price_check", searchAutoDelay > 0);

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
    o.insert(QStringLiteral("search_auto_delay"), searchAutoDelay);
    o.insert(QStringLiteral("auto_price_check"), autoPriceCheck);
    o.insert(QStringLiteral("search_list_count"), searchListCount);
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
    root.insert(QStringLiteral("options"), o);
    root.insert(QStringLiteral("shortcuts"), sc);
    return json::writeFile(path, QJsonDocument(root), true, true, error);
}

} // namespace pts
