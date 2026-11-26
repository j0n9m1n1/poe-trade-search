// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/SearchDefaults.h"

#include "core/Keywords.h"
#include "core/TradeDb.h"

namespace pts {

SearchRequest::LevelKind levelKindFor(const ParsedItem& it)
{
    if (it.isGem)
        return SearchRequest::LevelKind::GemLevel;
    if (it.isMap && !it.isWaystone)
        return SearchRequest::LevelKind::MapTier;
    return SearchRequest::LevelKind::ItemLevel;
}

ItemFilterState defaultFilterState(const ParsedItem& it)
{
    ItemFilterState s;
    s.useLevel = it.checkLevel;
    const auto kind = levelKindFor(it);
    if (kind == SearchRequest::LevelKind::GemLevel) {
        if (it.level > 0)
            s.levelMin = it.level;
    } else if (kind == SearchRequest::LevelKind::MapTier) {
        if (it.mapTier > 0) {
            s.levelMin = it.mapTier;
            s.levelMax = it.mapTier;
        }
    } else if (it.itemLevel > 0) {
        s.levelMin = it.itemLevel;
    }
    s.useQuality = it.checkQuality;
    if (it.quality > 0)
        s.qualityMin = it.quality;
    s.useSockets = it.checkSockets;
    if (it.sockets > 0)
        s.socketsMin = it.sockets;
    if (it.links > 0)
        s.linksMin = it.links;
    s.corrupted = it.corruptDefault;
    s.rarity = it.rarityFilter;
    s.byCategory = it.byCategory;
    s.influences = it.influences;
    s.synthesised = it.synthesised;
    s.blighted = it.blighted;
    s.blightRavaged = it.blightRavaged;
    s.heistObjective = it.heistValue;
    s.ultimatumReward = it.ultimatumReward;
    s.ultimatumOutput = it.ultimatumOutput;
    return s;
}

SearchRequest makeRequest(const ParsedItem& it, const ItemFilterState& st, const Keywords& kw, const GameDb* gdb,
                          const RequestContext& ctx, QString* warning)
{
    SearchRequest r;
    r.game = it.game;
    r.lang = ctx.server;
    r.status = ctx.status;
    r.indexed = ctx.indexed;
    r.priceMin = ctx.priceMin;
    r.includeDisabledStats = ctx.forBrowser;

    r.name = it.name;
    r.type = it.type;
    r.disc = it.disc;
    if (ctx.server != it.lang) {
        QString t, n;
        if (gdb && gdb->translateItem(it.lang, ctx.server, it.type, it.name, &t, &n)) {
            r.type = t;
            r.name = n;
        } else if (warning) {
            *warning = QStringLiteral("다른 언어 서버용 이름 변환 실패 - 아이템 언어 서버로 검색");
            r.lang = it.lang;
        } else {
            r.lang = it.lang;
        }
    }
    r.byCategory = st.byCategory;
    r.category = it.category;
    r.rarity = st.rarity;

    r.useSockets = st.useSockets;
    r.socketsMin = st.socketsMin;
    r.socketsMax = st.socketsMax;
    r.linksMin = st.linksMin;
    r.linksMax = st.linksMax;
    r.useQuality = st.useQuality;
    r.qualityMin = st.qualityMin;
    r.qualityMax = st.qualityMax;
    r.useLevel = st.useLevel;
    r.levelKind = levelKindFor(it);
    r.levelMin = st.levelMin;
    r.levelMax = st.levelMax;
    r.corrupted = st.corrupted;
    r.synthesised = st.synthesised;
    r.blighted = st.blighted;
    r.blightRavaged = st.blightRavaged;
    r.heistObjective = st.heistObjective;
    r.ultimatumReward = st.ultimatumReward;
    r.ultimatumOutput = st.ultimatumOutput;

    if (it.game == Game::Poe1 && !it.isMap) {
        const auto& infl = kw.influences();
        for (int i : st.influences) {
            if (i >= 0 && i < infl.size() && !infl[i].pseudo.isEmpty())
                r.influencePseudo << infl[i].pseudo;
        }
    }

    // 옵션 줄
    double resSum = 0;
    bool resAny = false;
    for (const ParsedMod& m : it.mods) {
        const ModCandidate* c = m.current();
        if (!c)
            continue;
        if (m.checked && m.totalRes && m.resistance) {
            resSum += m.min.value_or(0) + m.max.value_or(0);
            resAny = true;
            continue;
        }
        StatQuery q;
        q.id = c->id;
        q.disabled = !m.checked || m.lockedOff;
        if (m.optionStat) {
            q.option = c->option;
        } else {
            q.min = m.min;
            q.max = m.max;
        }
        r.stats.push_back(q);
    }
    if (resAny) {
        StatQuery q;
        q.id = QStringLiteral("pseudo.pseudo_total_resistance");
        q.min = resSum;
        r.stats.prepend(q);
    }
    return r;
}

} // namespace pts
