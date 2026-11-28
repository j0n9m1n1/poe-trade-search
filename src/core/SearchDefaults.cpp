// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/SearchDefaults.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>
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
    s.mirrored = it.mirrored ? 1 : 0;
    // 분열 안 된 장비는 분열된 매물(제작 베이스라 비쌈)을 빼야 시세가 맞음
    s.notFractured = (it.isGear || it.isJewel) && it.rarityId != QStringLiteral("unique") && !it.fractured;
    s.rarity = it.rarityFilter;
    s.useName = !it.byCategory; // auto_select_bytype 대상이면 처음부터 유형 검색
    s.useType = !it.byCategory;
    s.influences = it.influences;
    s.synthesised = it.synthesised;
    s.searing = it.searing;
    s.tangled = it.tangled;
    s.blighted = it.blighted;
    s.blightRavaged = it.blightRavaged;
    s.heistObjective = it.heistValue;
    s.ultimatumReward = it.ultimatumReward;
    s.ultimatumOutput = it.ultimatumOutput;
    for (const ItemProp& p : it.props)
        s.props.push_back({p.key, p.use, std::floor(p.value), std::nullopt});
    if (it.emptyPrefixes > 0)
        s.emptyPrefixes.min = it.emptyPrefixes;
    if (it.emptySuffixes > 0)
        s.emptySuffixes.min = it.emptySuffixes;
    return s;
}

ParsedItem applyPreset(const ParsedItem& parsed, Preset p, int rangePct, ItemFilterState* st)
{
    if (parsed.isWarrant) { // 용병 소환장은 범위 콤보가 보조 기술 등급 모드 (0/1/2)
        ParsedItem it = parsed;
        it.warrantMode = qBound(0, rangePct, 2);
        if (st)
            *st = defaultFilterState(it);
        return it;
    }
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
            if (m.unmatched || it.isWarrant) // 용병 기술/보조 기술은 체크 그대로
                continue;
            m.totalRes = false;
            m.resistance = false;
            const bool modifiableDesecrated = m.modType == QStringLiteral("desecrated") &&
                                             !it.corrupted && !it.mirrored && !it.sanctified &&
                                             m.current() && m.current()->group == QStringLiteral("explicit");
            const QString group = modifiableDesecrated || m.modType.isEmpty() ? QStringLiteral("explicit") : m.modType;
            // pseudo 말고 원래 그룹
            int sel = -1;
            for (int i = 0; i < m.cands.size(); ++i) {
                const QString& g = m.cands[i].group;
                if (g == group) {
                    sel = i;
                    break;
                }
                if (sel < 0 && g != QStringLiteral("pseudo"))
                    sel = i;
            }
            if (sel >= 0)
                m.selected = sel;
            const bool explicitLike = m.modType.isEmpty() || m.modType == QStringLiteral("fractured") ||
                                      m.modType == QStringLiteral("desecrated");
            if (m.lockedOff)
                m.checked = false;
            else if (m.checked && (m.modType == QStringLiteral("fractured") || m.rangeLocked ||
                                   (it.category == QStringLiteral("jewel.cluster") && m.optionStat)))
                continue;
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
            if (pr.min && pr.key != QStringLiteral("ilvl")) // 아이템 레벨은 구간이 의미라 그대로
                pr.min = std::floor(*pr.min * (100 - rangePct) / 100.0);
        }
    }
    return it;
}

// 용병 소환장 count 그룹
//  - 문구가 같은 id 가 여럿인 기술: 그중 하나 (min 1)
//  - 보조 기술: 모드 0 은 그 등급 그대로, 1·2 는 같은 계열 그 등급 이상 아무거나. 체크한 보조 기술 수만큼 (모드 2 는 2개 덜)
//  거래소 쿼리는 보조 기술이 어느 기술에 붙었는지는 못 거름. 비로그인은 count 그룹이 많으면 거부돼서 보조 기술은 하나로 묶음
static void addWarrantGroups(const ParsedItem& it, SearchRequest& r)
{
    QSet<QString> andIds;
    for (const StatQuery& q : r.stats) {
        if (!q.disabled)
            andIds.insert(q.id);
    }
    QSet<QString> seen;
    StatCountGroup sup;
    sup.min = 0;
    for (const ParsedMod& m : it.mods) {
        if (!m.checked || m.lockedOff || m.unmatched)
            continue;
        if (m.tier == 0) {
            if (m.exactIds.size() > 1) {
                QStringList key = m.exactIds;
                key.sort();
                if (!seen.contains(QStringLiteral("s:") + key.join(QLatin1Char(',')))) {
                    seen.insert(QStringLiteral("s:") + key.join(QLatin1Char(',')));
                    r.countGroups.push_back({m.exactIds, 1});
                }
            }
            continue;
        }
        const QStringList& ids = it.warrantMode == 0 || m.anyOf.isEmpty() ? m.exactIds : m.anyOf;
        QStringList key = ids;
        key.sort();
        const QString k = key.join(QLatin1Char(','));
        bool covered = false;
        for (const QString& id : ids)
            covered = covered || andIds.contains(id);
        if (ids.isEmpty() || covered || seen.contains(k))
            continue;
        seen.insert(k);
        ++sup.min;
        for (const QString& id : ids) {
            if (!sup.ids.contains(id))
                sup.ids << id;
        }
    }
    if (it.warrantMode == 2)
        sup.min = sup.min > 0 ? qMax(1, sup.min - 2) : 0;
    if (sup.min > 0)
        r.countGroups.push_back(sup);
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
    // 이름도 베이스도 안 보내는 분류 검색이면 번역이 필요 없음 -> 번역 실패로 서버를 바꾸지 않음
    // 용병 소환장 type 은 언어와 무관한 빌드 id
    const bool sendsText = !st.byCategory && (st.useName || st.useType || it.category.isEmpty()) && !it.isWarrant;
    if (ctx.server != it.lang && sendsText) {
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
    r.uniqueName = it.rarityId == QStringLiteral("unique");
    r.useType = st.useType;
    r.category = it.category;
    r.rarity = st.rarity;

    r.useSockets = st.useSockets;
    r.gemSockets = it.game == Game::Poe2 && it.isGem;
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
        // 복제: 복제된 아이템이면 사용자가 고른 대로(기본 복제만), 아니면 고유 아닌 장비는 복제 제외
        r.mirrored = st.mirrored == 1 ? 1 : (st.mirrored == 2 || (gear && nonUnique && !it.mirrored) ? 2 : 0);
        r.notSplit = gear && nonUnique && it.game == Game::Poe1 && !it.split && (!permanent || st.exact);
        // 일반 고유로 검색하면 반짝이 버전(비쌈)이 섞이므로 뺌. 반짝이 버전 템을 "고유"로 바꿔 검색하면 둘 다
        r.notFoil = it.game == Game::Poe1 && !it.foil && st.rarity == QStringLiteral("unique");
        // 분열 속성 없음 줄이 보이는 아이템(고유 아닌 장비·주얼)은 그 줄 체크를 그대로 따름 (정확 프리셋에서도 끌 수 있게)
        const bool notFracRow = (it.isGear || it.isJewel) && nonUnique;
        r.notFractured = !it.fractured && (st.notFractured || (gear && st.exact && !notFracRow));
        // 부여된 젬(보조 젬 내장)은 같은 젬이라도 시세가 따로라 부여 여부를 맞춤 (안 하면 싼 부여된 매물이 섞임)
        if (it.isGem && it.game == Game::Poe1)
            r.gemImbued = it.imbued ? 1 : 2;
        // 수정 가능: 타락 아님 + 복제 아님 (거래소에 전용 필터가 없어 두 필터로, PoE1/PoE2 공통)
        if (st.modifiable) {
            r.corrupted = 2;
            r.twiceCorrupted = 0;
            r.mirrored = 2;
        }
    }
    r.synthesised = st.synthesised;
    r.searing = st.searing;
    r.tangled = st.tangled;
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
        if (!st.exact && !m.lockedOff && m.checked && m.totalRes && m.resistance && m.min && !m.max &&
            c->group != QStringLiteral("fractured") && c->group != QStringLiteral("desecrated")) {
            resSum += *m.min * kw.resistanceWeight(m.statKey);
            resAny = true;
            continue;
        }
        if (it.isWarrant && (m.tier > 0 || m.exactIds.size() > 1)) // 아래에서 count 그룹으로
            continue;
        StatQuery q;
        q.id = c->id;
        q.disabled = !m.checked || m.lockedOff;
        if (m.optionStat) {
            q.option = c->option;
        } else {
            q.min = m.min;
            q.max = m.max;
        }
        if (it.isWarrant) { // 같은 보조 기술이 두 기술에 붙어 있으면 한 번만 (같은 id 두 번이면 거래소가 거부)
            auto same = std::find_if(r.stats.begin(), r.stats.end(), [&](const StatQuery& o) { return o.id == q.id; });
            if (same != r.stats.end()) {
                same->disabled = same->disabled && q.disabled;
                continue;
            }
        }
        r.stats.push_back(q);
    }
    if (it.isWarrant)
        addWarrantGroups(it, r);
    if (resAny) {
        StatQuery q;
        q.id = QStringLiteral("pseudo.pseudo_total_resistance");
        q.min = resSum;
        r.stats.prepend(q);
    }
    if (it.emptyPrefixes > 0 && st.emptyPrefixes.use && st.emptyPrefixes.min > 0)
        r.stats.push_back({QStringLiteral("pseudo.pseudo_number_of_empty_prefix_mods"), st.emptyPrefixes.min, std::nullopt, {}});
    if (it.emptySuffixes > 0 && st.emptySuffixes.use && st.emptySuffixes.min > 0)
        r.stats.push_back({QStringLiteral("pseudo.pseudo_number_of_empty_suffix_mods"), st.emptySuffixes.min, std::nullopt, {}});
    return r;
}

QString warrantEnglishText(const ParsedItem& it, const TradeDb& en)
{
    if (!it.isWarrant || !en.isLoaded())
        return QString();
    QString build;
    for (int i : en.itemsByType(it.type)) {
        const ItemEntry& e = en.item(i);
        const int a = e.text.indexOf(QLatin1Char('('));
        if (e.disc == it.disc && a > 0 && e.text.endsWith(QLatin1Char(')')))
            build = e.text.mid(a + 1, e.text.size() - a - 2);
    }
    if (build.isEmpty())
        return QString();
    static const QRegularExpression tierRe(QStringLiteral("\\(Tier (\\d)\\)$"));
    QStringList out{QStringLiteral("Item Class: Map Fragments"), QStringLiteral("Rarity: Normal"),
                    QStringLiteral("Mercenary Warrant"), QStringLiteral("--------"),
                    QStringLiteral("Build: ") + build};
    if (it.mercenaryLevel > 0)
        out << QStringLiteral("Mercenary Level: %1").arg(it.mercenaryLevel);
    bool any = false;
    for (const ParsedMod& m : it.mods) {
        const ModCandidate* c = m.current();
        const int si = c ? en.findStat(c->id) : -1;
        if (si < 0) {
            if (m.tier == 0) // 기술을 못 찾으면 보조 기술이 엉뚱한 기술에 붙으므로 포기
                return QString();
            continue;
        }
        QString text = en.stat(si).text;
        if (m.tier == 0) {
            out << QStringLiteral("--------");
            any = true;
        } else {
            text.replace(tierRe, QStringLiteral("(Tier: \\1)"));
        }
        out << text;
    }
    if (!any)
        return QString();
    return out.join(QLatin1Char('\n'));
}

} // namespace pts
