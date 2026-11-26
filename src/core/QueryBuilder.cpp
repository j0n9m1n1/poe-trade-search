// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/QueryBuilder.h"

#include "core/TradeDb.h"

#include <QHash>
#include <QJsonArray>

namespace pts {

namespace {

// DB 에 필터 정보가 없을 때 쓰는 기본 위치 (2026-11 기준 실측)
QString defaultGroup(Game g, const QString& filter)
{
    static const QHash<QString, QString> p1 = {
        {QStringLiteral("category"), QStringLiteral("type_filters")},
        {QStringLiteral("rarity"), QStringLiteral("type_filters")},
        {QStringLiteral("sockets"), QStringLiteral("socket_filters")},
        {QStringLiteral("links"), QStringLiteral("socket_filters")},
        {QStringLiteral("quality"), QStringLiteral("misc_filters")},
        {QStringLiteral("ilvl"), QStringLiteral("misc_filters")},
        {QStringLiteral("gem_level"), QStringLiteral("misc_filters")},
        {QStringLiteral("gem_transfigured"), QStringLiteral("misc_filters")},
        {QStringLiteral("corrupted"), QStringLiteral("misc_filters")},
        {QStringLiteral("synthesised_item"), QStringLiteral("misc_filters")},
        {QStringLiteral("map_tier"), QStringLiteral("map_filters")},
        {QStringLiteral("map_blighted"), QStringLiteral("map_filters")},
        {QStringLiteral("map_uberblighted"), QStringLiteral("map_filters")},
        {QStringLiteral("heist_objective_value"), QStringLiteral("heist_filters")},
        {QStringLiteral("ultimatum_reward"), QStringLiteral("ultimatum_filters")},
        {QStringLiteral("ultimatum_output"), QStringLiteral("ultimatum_filters")},
        {QStringLiteral("indexed"), QStringLiteral("trade_filters")},
        {QStringLiteral("sale_type"), QStringLiteral("trade_filters")},
        {QStringLiteral("price"), QStringLiteral("trade_filters")},
        {QStringLiteral("collapse"), QStringLiteral("trade_filters")},
    };
    static const QHash<QString, QString> p2 = {
        {QStringLiteral("collapse"), QStringLiteral("trade_filters")},
        {QStringLiteral("category"), QStringLiteral("type_filters")},
        {QStringLiteral("rarity"), QStringLiteral("type_filters")},
        {QStringLiteral("ilvl"), QStringLiteral("type_filters")},
        {QStringLiteral("quality"), QStringLiteral("type_filters")},
        {QStringLiteral("rune_sockets"), QStringLiteral("equipment_filters")},
        {QStringLiteral("gem_level"), QStringLiteral("misc_filters")},
        {QStringLiteral("corrupted"), QStringLiteral("misc_filters")},
        {QStringLiteral("map_tier"), QStringLiteral("map_filters")},
        {QStringLiteral("indexed"), QStringLiteral("trade_filters")},
        {QStringLiteral("sale_type"), QStringLiteral("trade_filters")},
        {QStringLiteral("price"), QStringLiteral("trade_filters")},
    };
    return (g == Game::Poe1 ? p1 : p2).value(filter);
}

QJsonObject minMax(const OptDouble& mn, const OptDouble& mx)
{
    QJsonObject o;
    if (mn)
        o.insert(QStringLiteral("min"), *mn);
    if (mx)
        o.insert(QStringLiteral("max"), *mx);
    return o;
}

QJsonObject option(const QString& v)
{
    return QJsonObject{{QStringLiteral("option"), v}};
}

class FilterSet {
public:
    FilterSet(Game g, const TradeDb* db) : m_game(g), m_db(db) {}

    void add(const QString& filter, const QJsonObject& value)
    {
        if (value.isEmpty())
            return;
        QString group = m_db ? m_db->filterGroup(filter) : QString();
        if (group.isEmpty())
            group = defaultGroup(m_game, filter);
        if (group.isEmpty())
            return;
        QJsonObject g = m_groups.value(group).toObject();
        QJsonObject f = g.value(QStringLiteral("filters")).toObject();
        f.insert(filter, value);
        g.insert(QStringLiteral("filters"), f);
        m_groups.insert(group, g);
    }

    QJsonObject result() const { return m_groups; }

private:
    Game m_game;
    const TradeDb* m_db;
    QJsonObject m_groups;
};

} // namespace

QVector<PriceCurrency> priceCurrencies(Game g)
{
    // 카카오 거래소 /api/trade(2)/data/filters 의 price 옵션 표기 그대로 (자주 쓰는 것만)
    if (g == Game::Poe2) {
        return {{QString(), QStringLiteral("엑잘티드 오브 상당 아이템")},
                {QStringLiteral("exalted_divine"), QStringLiteral("엑잘티드 오브 및 신성한 오브")},
                {QStringLiteral("exalted"), QStringLiteral("엑잘티드 오브")},
                {QStringLiteral("divine"), QStringLiteral("신성한 오브")},
                {QStringLiteral("chaos"), QStringLiteral("카오스 오브")}};
    }
    return {{QString(), QStringLiteral("카오스 오브 등가물")},
            {QStringLiteral("chaos_divine"), QStringLiteral("카오스 오브 또는 신성한 오브")},
            {QStringLiteral("chaos"), QStringLiteral("카오스 오브")},
            {QStringLiteral("divine"), QStringLiteral("신성한 오브")}};
}

QString QueryBuilder::indexedFromDays(int days)
{
    if (days < 1)
        return {};
    if (days < 3)
        return QStringLiteral("1day");
    if (days < 7)
        return QStringLiteral("3days");
    if (days < 14)
        return QStringLiteral("1week");
    return QStringLiteral("2weeks");
}

QJsonObject QueryBuilder::build(const SearchRequest& r, const TradeDb* db)
{
    QJsonObject query;
    query.insert(QStringLiteral("status"), option(r.status.isEmpty() ? QStringLiteral("available") : r.status));

    const bool unique = r.rarity == QStringLiteral("unique");
    if (!r.byCategory) {
        if (unique && !r.name.isEmpty())
            query.insert(QStringLiteral("name"), r.name);
        if (!r.type.isEmpty()) {
            if (!r.disc.isEmpty())
                query.insert(QStringLiteral("type"), QJsonObject{{QStringLiteral("option"), r.type},
                                                                 {QStringLiteral("discriminator"), r.disc}});
            else
                query.insert(QStringLiteral("type"), r.type);
        }
    }

    FilterSet fs(r.game, db);
    if (!r.category.isEmpty() && (r.byCategory || r.type.isEmpty()))
        fs.add(QStringLiteral("category"), option(r.category));
    if (!r.rarity.isEmpty())
        fs.add(QStringLiteral("rarity"), option(r.rarity));

    if (r.useSockets) {
        if (r.game == Game::Poe1) {
            fs.add(QStringLiteral("sockets"), minMax(r.socketsMin, r.socketsMax));
            fs.add(QStringLiteral("links"), minMax(r.linksMin, r.linksMax));
        } else {
            fs.add(QStringLiteral("rune_sockets"), minMax(r.socketsMin, r.socketsMax));
        }
    }
    if (r.useQuality)
        fs.add(QStringLiteral("quality"), minMax(r.qualityMin, r.qualityMax));
    if (r.useLevel) {
        switch (r.levelKind) {
        case SearchRequest::LevelKind::ItemLevel:
            fs.add(QStringLiteral("ilvl"), minMax(r.levelMin, r.levelMax));
            break;
        case SearchRequest::LevelKind::GemLevel:
            fs.add(QStringLiteral("gem_level"), minMax(r.levelMin, r.levelMax));
            break;
        case SearchRequest::LevelKind::MapTier:
            fs.add(QStringLiteral("map_tier"), minMax(r.levelMin, r.levelMax));
            break;
        }
    }
    if (r.corrupted == 1)
        fs.add(QStringLiteral("corrupted"), option(QStringLiteral("true")));
    else if (r.corrupted == 2)
        fs.add(QStringLiteral("corrupted"), option(QStringLiteral("false")));
    if (r.synthesised && r.game == Game::Poe1)
        fs.add(QStringLiteral("synthesised_item"), option(QStringLiteral("true")));
    if (r.blighted)
        fs.add(QStringLiteral("map_blighted"), option(QStringLiteral("true")));
    if (r.blightRavaged)
        fs.add(QStringLiteral("map_uberblighted"), option(QStringLiteral("true")));
    if (!r.heistObjective.isEmpty())
        fs.add(QStringLiteral("heist_objective_value"), option(r.heistObjective));
    if (!r.ultimatumReward.isEmpty()) {
        fs.add(QStringLiteral("ultimatum_reward"), option(r.ultimatumReward));
        if (!r.ultimatumOutput.isEmpty())
            fs.add(QStringLiteral("ultimatum_output"), option(r.ultimatumOutput));
    }
    if (!r.indexed.isEmpty())
        fs.add(QStringLiteral("indexed"), option(r.indexed));
    if (!r.saleType.isEmpty())
        fs.add(QStringLiteral("sale_type"), option(r.saleType));
    if (r.collapse)
        fs.add(QStringLiteral("collapse"), option(QStringLiteral("true")));
    {
        QJsonObject price;
        if (r.priceMin && *r.priceMin > 0)
            price = minMax(r.priceMin, std::nullopt);
        if (!r.priceOption.isEmpty())
            price.insert(QStringLiteral("option"), r.priceOption);
        if (!price.isEmpty())
            fs.add(QStringLiteral("price"), price);
    }

    const QJsonObject filters = fs.result();
    if (!filters.isEmpty())
        query.insert(QStringLiteral("filters"), filters);

    QJsonArray statFilters;
    for (const QString& p : r.influencePseudo)
        statFilters.append(QJsonObject{{QStringLiteral("id"), p}});
    for (const StatQuery& s : r.stats) {
        if (s.disabled && !r.includeDisabledStats)
            continue;
        QJsonObject f;
        f.insert(QStringLiteral("id"), s.id);
        QJsonObject v = minMax(s.min, s.max);
        if (!s.option.isUndefined() && !s.option.isNull())
            v.insert(QStringLiteral("option"), s.option);
        if (!v.isEmpty())
            f.insert(QStringLiteral("value"), v);
        if (s.disabled)
            f.insert(QStringLiteral("disabled"), true);
        statFilters.append(f);
    }
    QJsonObject and_;
    and_.insert(QStringLiteral("type"), QStringLiteral("and"));
    and_.insert(QStringLiteral("filters"), statFilters);
    query.insert(QStringLiteral("stats"), QJsonArray{and_});

    QJsonObject root;
    root.insert(QStringLiteral("query"), query);
    root.insert(QStringLiteral("sort"), QJsonObject{{QStringLiteral("price"), QStringLiteral("asc")}});
    return root;
}

QJsonObject QueryBuilder::buildExchange(const QString& have, const QString& want, const QString& status)
{
    QJsonObject ex;
    ex.insert(QStringLiteral("status"), option(status));
    ex.insert(QStringLiteral("have"), QJsonArray{have});
    ex.insert(QStringLiteral("want"), QJsonArray{want});
    return QJsonObject{{QStringLiteral("exchange"), ex}};
}

} // namespace pts
