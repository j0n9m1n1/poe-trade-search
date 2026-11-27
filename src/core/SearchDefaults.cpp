// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/SearchDefaults.h"

#include <cmath>

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
    } else if (it.ilvlFloor > 0) {
        s.levelMin = it.ilvlFloor; // 클러스터 주얼: 같은 노트 목록 구간
        if (it.ilvlCeil > 0)
            s.levelMax = it.ilvlCeil;
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
    s.twiceCorrupted = it.twiceCorruptDefault;
    s.mutated = it.mutatedDefault;
    s.rarity = it.rarityFilter;
    s.useName = !it.byCategory; // auto_select_bytype 대상이면 처음부터 유형 검색
    s.useType = !it.byCategory;
    s.influences = it.influences;
    s.synthesised = it.synthesised;
    s.blighted = it.blighted;
    s.blightRavaged = it.blightRavaged;
    s.heistObjective = it.heistValue;
    s.ultimatumReward = it.ultimatumReward;
    s.ultimatumOutput = it.ultimatumOutput;
    for (const ItemProp& p : it.props)
        s.props.push_back({p.key, p.use, std::floor(p.value), std::nullopt});
    return s;
}

ParsedItem applyPreset(const ParsedItem& parsed, Preset p, int rangePct, ItemFilterState* st)
{
    ParsedItem it = parsed;
    switch (p) {
    case Preset::Default:
        applyStatRanges(it, rangePct);
        if (st)
            *st = defaultFilterState(it);
        break;
    case Preset::Exact: {
        bool anyTier = false;
        for (const ParsedMod& m : it.mods)
            anyTier = anyTier || m.tier > 0;
        for (ParsedMod& m : it.mods) {
            if (m.unmatched)
                continue;
            // pseudo 말고 원래 그룹
            int sel = -1;
            for (int i = 0; i < m.cands.size(); ++i) {
                const QString& g = m.cands[i].group;
                if (!m.modType.isEmpty() && g == m.modType) {
                    sel = i;
                    break;
                }
                if (sel < 0 && g != QStringLiteral("pseudo"))
                    sel = i;
                if (m.modType.isEmpty() && g == QStringLiteral("explicit")) {
                    sel = i;
                    break;
                }
            }
            if (sel >= 0)
                m.selected = sel;
            const bool explicitLike = m.modType.isEmpty() || m.modType == QStringLiteral("fractured");
            if (m.lockedOff)
                m.checked = false;
            else if (anyTier)
                m.checked = explicitLike && m.tier >= 1 && m.tier <= 2;
            else
                m.checked = explicitLike || m.optionStat;
        }
        applyStatRanges(it, 0);
        if (st) {
            *st = defaultFilterState(it);
            st->exact = true;
        }
        break;
    }
    case Preset::Base:
        for (ParsedMod& m : it.mods)
            m.checked = false;
        applyStatRanges(it, rangePct);
        if (st) {
            *st = defaultFilterState(it);
            if (it.itemLevel > 0 && levelKindFor(it) == SearchRequest::LevelKind::ItemLevel) {
                st->useLevel = true;
                st->levelMin = it.itemLevel;
                st->levelMax.reset();
            }
            st->useQuality = false;
            st->useSockets = false;
            st->byCategory = false;
            st->useName = true;
            st->useType = true;
            if (it.rarityId != QStringLiteral("unique"))
                st->rarity = QStringLiteral("nonunique");
        }
        break;
    }
    // 아이템 속성(방어도/DPS) 최소값도 옵션처럼 완화 (정확 프리셋은 그대로)
    if (st && p != Preset::Exact && rangePct > 0) {
        for (PropFilterState& pr : st->props) {
            if (pr.min)
                pr.min = std::floor(*pr.min * (100 - rangePct) / 100.0);
        }
    }
    return it;
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
    r.priceOption = ctx.priceOption;
    r.collapse = ctx.collapse;
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
            *warning = QStringLiteral("다른 언어 서버용 이름으로 바꾸지 못해 아이템 언어 서버로 검색합니다.");
            r.lang = it.lang;
        } else {
            r.lang = it.lang;
        }
    }
    r.byCategory = st.byCategory;
    r.useName = st.useName;
    r.useType = st.useType;
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
    r.twiceCorrupted = st.corrupted == 2 ? 0 : st.twiceCorrupted;
    r.mutated = st.mutated;
    // 기본 제외 필터 (APT/EE2): 고유 아닌 장비는 복제된/분할된 매물 제외, 정확 프리셋은 분열 아닌 템이면 분열 매물 제외
    {
        const bool gear = it.isGear || it.isJewel || it.isFlask;
        const bool nonUnique = it.rarityId != QStringLiteral("unique");
        const bool permanent = ctx.league.contains(QStringLiteral("Standard"), Qt::CaseInsensitive) ||
                               ctx.league.compare(QStringLiteral("Hardcore"), Qt::CaseInsensitive) == 0 ||
                               ctx.league.compare(QStringLiteral("Ruthless"), Qt::CaseInsensitive) == 0;
        r.notMirrored = gear && nonUnique && !it.mirrored;
        r.notSplit = gear && nonUnique && it.game == Game::Poe1 && !it.split && (!permanent || st.exact);
        r.notFractured = gear && st.exact && !it.fractured;
    }
    r.synthesised = st.synthesised;
    r.blighted = st.blighted;
    r.blightRavaged = st.blightRavaged;
    r.heistObjective = st.heistObjective;
    r.ultimatumReward = st.ultimatumReward;
    r.ultimatumOutput = st.ultimatumOutput;
    for (const PropFilterState& p : st.props) {
        if (p.use && (p.min || p.max))
            r.props.push_back({p.key, p.min, p.max});
    }

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
