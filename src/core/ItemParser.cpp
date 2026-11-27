// SPDX-License-Identifier: GPL-3.0-or-later
//
// 클립보드 아이템 텍스트 해석. 원본 PoeTradeSearch(phiDelPark) Methods.cs ItemTextParser 를
// 현행 PoE1 / PoE2 거래소 데이터 기준으로 다시 짠 것.
#include "core/ItemParser.h"

#include "core/CheckedStore.h"
#include "core/Keywords.h"
#include "core/TextUtil.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QtMath>

namespace pts {

namespace {

const QString kSep = QStringLiteral("--------");

// 옵션 종류별 색 키 (UI 에서 게임 글씨 색으로 바꿈). "" = 일반 옵션
QString colorForType(const QString& t)
{
    static const QSet<QString> colored = {QStringLiteral("implicit"), QStringLiteral("crafted"),
                                          QStringLiteral("enchant"),  QStringLiteral("scourge"),
                                          QStringLiteral("rune"),     QStringLiteral("desecrated"),
                                          QStringLiteral("fractured")};
    return colored.contains(t) ? t : QString();
}

double damageSum(const QStringList& values)
{
    // "55-103 (augmented), 10-20 (augmented)" -> (55+103)+(10+20)
    static const QRegularExpression re(QStringLiteral("(\\d+(?:\\.\\d+)?)\\s*-\\s*(\\d+(?:\\.\\d+)?)"));
    double sum = 0;
    for (const QString& v : values) {
        auto it = re.globalMatch(v);
        while (it.hasNext()) {
            const auto m = it.next();
            sum += m.captured(1).toDouble() + m.captured(2).toDouble();
        }
    }
    return sum;
}

} // namespace

ItemParser::ItemParser(const Keywords& kw, const GameDb* dbs, const CheckedStore* checked)
    : m_kw(kw), m_dbs(dbs), m_checked(checked)
{
}

bool ItemParser::looksLikeItem(const QString& text) const
{
    const QString first = text.trimmed().section(QLatin1Char('\n'), 0, 0).trimmed();
    return m_kw.detectLang(first, nullptr, nullptr) && text.contains(kSep);
}

Game ItemParser::guessGame(const QString& text, Game fallback) const
{
    // PoE2: "Requires: Level 33, 57 Str" 처럼 한 줄 요구사항, 룬/훼손 옵션 접미
    static const QRegularExpression poe2Req(QStringLiteral("^(Requires|요구 사항|요구사항): \\S"),
                                            QRegularExpression::MultilineOption);
    if (poe2Req.match(text).hasMatch() || text.contains(QStringLiteral(" (rune)\n")) ||
        text.contains(QStringLiteral(" (desecrated)\n")))
        return Game::Poe2;
    static const QRegularExpression poe1Req(QStringLiteral("^(Requirements|요구사항|요구 사항):\\s*$"),
                                            QRegularExpression::MultilineOption);
    if (poe1Req.match(text).hasMatch())
        return Game::Poe1;
    // PoE2 전용 아이템 종류
    static const QStringList poe2Classes = {QStringLiteral("Foci"),        QStringLiteral("Quarterstaves"),
                                            QStringLiteral("Crossbows"),   QStringLiteral("Spears"),
                                            QStringLiteral("Flails"),      QStringLiteral("Charms"),
                                            QStringLiteral("Waystones"),   QStringLiteral("Bucklers"),
                                            QStringLiteral("Talismans"),   QStringLiteral("Soul Cores")};
    const QString first = text.trimmed().section(QLatin1Char('\n'), 0, 0);
    QString cls;
    if (m_kw.detectLang(first.trimmed(), nullptr, &cls) && poe2Classes.contains(cls))
        return Game::Poe2;
    return fallback;
}

QString ItemParser::findBaseInName(const TradeDb& db, const QString& s) const
{
    if (db.hasType(s))
        return s;
    const QStringList w = s.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    // 가장 긴 연속 단어 구간부터
    for (int len = w.size() - 1; len >= 1; --len) {
        for (int st = 0; st + len <= w.size(); ++st) {
            const QString cand = w.mid(st, len).join(QLatin1Char(' '));
            if (db.hasType(cand))
                return cand;
        }
    }
    return {};
}

QString ItemParser::resolveCategory(const ParsedItem& it, const TradeDb& db) const
{
    QString cat = m_kw.classToCategory(it.game, it.lang, it.itemClass);
    if (!cat.isEmpty())
        return cat;

    // 거래소 카테고리 옵션 텍스트와 비교 (한국어는 거의 그대로, 영어는 복수형 -> 단수형)
    const QVector<FilterOption> opts = db.filterOptions(QStringLiteral("category"));
    QStringList wants;
    const QString c = it.itemClass;
    if (it.lang == Lang::Ko) {
        wants << c;
    } else {
        QString s = c;
        s.replace(QStringLiteral("One Hand "), QStringLiteral("One-Handed "));
        s.replace(QStringLiteral("Two Hand "), QStringLiteral("Two-Handed "));
        wants << s;
        if (s.endsWith(QStringLiteral("staves")))
            wants << s.left(s.size() - 6) + QStringLiteral("staff");
        if (s.endsWith(QStringLiteral("Staves")))
            wants << s.left(s.size() - 6) + QStringLiteral("Staff");
        if (s.endsWith(QStringLiteral("ies")))
            wants << s.left(s.size() - 3) + QStringLiteral("y");
        if (s.endsWith(QStringLiteral("es")))
            wants << s.left(s.size() - 2);
        if (s.endsWith(QLatin1Char('s')))
            wants << s.left(s.size() - 1);
    }
    static const QStringList prefixes = {QStringLiteral("임의의 "), QStringLiteral("모든 "), QStringLiteral("Any ")};
    for (const QString& w : wants) {
        const QString wk = text::looseKey(w);
        for (const auto& o : opts) {
            if (o.id.isEmpty())
                continue;
            QString t = o.text;
            for (const QString& p : prefixes) {
                if (t.startsWith(p))
                    t = t.mid(p.size());
            }
            if (text::looseKey(t) == wk)
                return o.id;
        }
    }

    // 마지막: 베이스가 속한 items 그룹 (대분류만)
    const QVector<int> v = db.itemsByType(it.type);
    if (!v.isEmpty())
        return db.item(v.first()).group;
    return {};
}

ParsedMod ItemParser::buildMod(const ParsedItem& it, const TradeDb& db, const MatcherDb& md,
                               const QVector<StatMatch>& ms, const CleanLine& first, const QString& raw,
                               const QString& rangeSrc, const ParseOptions& opt, bool* optionStatSeen) const
{
    const bool weapon = it.top == QStringLiteral("weapon");
    const bool armour = it.top == QStringLiteral("armour") && it.category != QStringLiteral("armour.quiver");
    const bool localContext = weapon || armour;
    // 방어 계열 로컬(방어도/회피/에너지 보호막/막기)은 방어구 전용, 나머지 로컬(공격 속도/명중/추가 피해/흡수...)은 무기 전용.
    // 무기에 붙은 "회피 +#" 은 전역 stat (꿈의 깃털 등). id 는 PoE1/PoE2, 언어 공통
    static const QSet<QString> armourLocal = {
        QStringLiteral("stat_4052037485"), QStringLiteral("stat_124859000"),  QStringLiteral("stat_53045048"),
        QStringLiteral("stat_1062208444"), QStringLiteral("stat_3484657501"), QStringLiteral("stat_4015621042"),
        QStringLiteral("stat_1999113824"), QStringLiteral("stat_3321629045"), QStringLiteral("stat_2451402625"),
        QStringLiteral("stat_3523867985"), QStringLiteral("stat_2481353198"),
    };
    auto localFits = [&](const StatEntry& e) {
        if (!localContext)
            return false;
        const bool def = armourLocal.contains(e.id.mid(e.id.indexOf(QLatin1Char('.')) + 1));
        return def ? armour : weapon;
    };

    auto scopeScore = [&](const StatEntry& e) {
        const QString& s = e.scope;
        if (s.isEmpty())
            return 1;
        if (s == QStringLiteral("local"))
            return localFits(e) ? 3 : 0;
        if (s == QStringLiteral("global"))
            return localContext ? 0 : 2;
        if (s == QStringLiteral("legacy"))
            return -1;
        if (s == QStringLiteral("staff"))
            return it.category.contains(QStringLiteral("staff")) ? 3 : 0;
        if (s == QStringLiteral("shield"))
            return it.category == QStringLiteral("armour.shield") ? 3 : 0;
        if (s == QStringLiteral("map"))
            return it.isMap ? 3 : 0;
        if (s == QStringLiteral("jewel"))
            return it.isJewel ? 3 : 0;
        if (s == QStringLiteral("charm"))
            return it.category == QStringLiteral("flask.charm") ? 3 : 0;
        if (s == QStringLiteral("flask"))
            return it.isFlask ? 3 : 0;
        if (s == QStringLiteral("tablet"))
            return it.category == QStringLiteral("map.tablet") ? 3 : 0;
        return 1;
    };

    // 그룹별로 가장 맞는 항목 하나씩 (DB 그룹 순서 유지)
    QHash<QString, int> bestByGroup; // group -> index into ms
    QStringList groupOrder;
    for (int i = 0; i < ms.size(); ++i) {
        const StatEntry& e = db.stat(ms[i].entry);
        auto f = bestByGroup.find(e.group);
        if (f == bestByGroup.end()) {
            bestByGroup.insert(e.group, i);
            groupOrder << e.group;
        } else if (scopeScore(e) > scopeScore(db.stat(ms[*f].entry))) {
            *f = i;
        }
    }
    QStringList ordered;
    for (const auto& g : db.groups()) {
        if (bestByGroup.contains(g.id))
            ordered << g.id;
    }
    for (const QString& g : groupOrder) {
        if (!ordered.contains(g))
            ordered << g;
    }

    ParsedMod mod;
    mod.raw = raw;
    mod.modType = first.modType;
    mod.tier = first.tier;

    bool localChosen = false;
    for (const QString& g : ordered) {
        const StatMatch& m = ms[bestByGroup.value(g)];
        const StatEntry& e = db.stat(m.entry);
        // 고정/제작 등 접미가 있으면 해당 그룹만 쓰는 게 맞지만, 원본처럼 후보는 다 보여주고 선택만 맞춤
        ModCandidate c;
        c.entry = m.entry;
        c.group = e.group;
        c.label = db.groupLabel(e.group);
        c.id = e.id;
        c.option = e.option;
        mod.cands.push_back(c);
        if (e.scope == QStringLiteral("local"))
            localChosen = localChosen || localFits(e);
    }

    // 기준 후보: 접미 그룹 > explicit > pseudo 아닌 첫 후보
    int primary = -1;
    auto findGroup = [&](const QString& g) {
        for (int i = 0; i < mod.cands.size(); ++i) {
            if (mod.cands[i].group == g)
                return i;
        }
        return -1;
    };
    if (!mod.modType.isEmpty())
        primary = findGroup(mod.modType);
    if (primary < 0)
        primary = findGroup(QStringLiteral("explicit"));
    if (primary < 0) {
        for (int i = 0; i < mod.cands.size(); ++i) {
            if (mod.cands[i].group != QStringLiteral("pseudo")) {
                primary = i;
                break;
            }
        }
    }
    if (primary < 0)
        primary = 0;

    const StatEntry& pe = db.stat(mod.cands[primary].entry);
    QVector<double> values;
    QVector<int> hashIdx;
    for (const auto& m : ms) {
        if (m.entry == mod.cands[primary].entry) {
            values = m.values;
            hashIdx = m.hashIdx;
            break;
        }
    }
    mod.statKey = pe.key;
    // 좋은 방향: 매처 데이터 > 원본 역방향 목록 > 높을수록 좋음
    if (const auto b = md.betterOf(pe.key))
        mod.better = *b;
    else
        mod.better = m_kw.isReverse(pe.key) ? -1 : 1;

    // pseudo 후보 추가
    if (findGroup(QStringLiteral("pseudo")) < 0) {
        QString pk = m_kw.pseudoFor(pe.key);
        if (!pk.isEmpty()) {
            static const QRegularExpression addsRe(QStringLiteral("^pseudo_adds_[a-z]+_damage$"));
            if (it.isWeapon && addsRe.match(pk).hasMatch())
                pk += QStringLiteral("_to_attacks");
            const int pi = db.findStat(QStringLiteral("pseudo.") + pk);
            if (pi >= 0) {
                ModCandidate c;
                c.entry = pi;
                c.group = QStringLiteral("pseudo");
                c.label = db.groupLabel(c.group);
                c.id = db.stat(pi).id;
                mod.cands.prepend(c);
                ++primary;
            }
        }
    }

    // 선택
    if (mod.cands.size() == 1) {
        mod.selected = 0;
    } else {
        const bool hasPseudo = findGroup(QStringLiteral("pseudo")) >= 0;
        int sel = -1;
        // 분열 옵션은 분열 그룹이 먼저 (유사로 바꾸면 분열 조건이 빠져서 분열 안 된 매물까지 섞임)
        if (mod.modType == QStringLiteral("fractured"))
            sel = findGroup(QStringLiteral("fractured"));
        if (sel < 0 && !localChosen && opt.autoSelectPseudo && hasPseudo)
            sel = findGroup(QStringLiteral("pseudo"));
        // PoE2 훼손 옵션은 수정 가능한 아이템이면 일반(explicit) id 로 검색 (EE2).
        // 분열 옵션은 분열 id 그대로: 분열 자체가 가치라서 일반 id 로 찾으면 분열 안 된 매물까지 섞임
        // (카카오 실측: 분열 28개 vs 일반 2152개)
        const bool asExplicit = !it.corrupted && !it.mirrored && mod.modType == QStringLiteral("desecrated");
        if (sel < 0 && !mod.modType.isEmpty() && !asExplicit)
            sel = findGroup(mod.modType);
        if (sel < 0)
            sel = findGroup(QStringLiteral("explicit"));
        if (sel < 0)
            sel = findGroup(QStringLiteral("fractured"));
        mod.selected = sel < 0 ? primary : sel;
    }

    // 값
    if (!pe.option.isUndefined() && !pe.option.isNull()) {
        mod.optionStat = true;
        mod.checked = true;
        if (optionStatSeen)
            *optionStatSeen = true;
    } else if (!values.isEmpty()) {
        // 고급 설명 범위 (줄의 n번째 숫자 -> 범위)
        const QVector<text::NumRange> ranges = rangeSrc.isEmpty() ? QVector<text::NumRange>() : text::numberRanges(rangeSrc);
        auto rangeAt = [&](int k) -> const text::NumRange* {
            if (k < 0 || k >= hashIdx.size())
                return nullptr;
            const int ti = hashIdx[k];
            return ti >= 0 && ti < ranges.size() && ranges[ti].has ? &ranges[ti] : nullptr;
        };
        static const QRegularExpression rangeRe(QStringLiteral("#\\s*(?:to|~|-)\\s*#"));
        if (values.size() >= 2 && rangeRe.match(pe.text).hasMatch()) {
            // "# to #" / "#~#" 범위형은 평균
            mod.value = std::trunc((values[0] + values[1]) / 2.0 * 10.0) / 10.0;
            const auto* a = rangeAt(0);
            const auto* b = rangeAt(1);
            if (a && b) {
                mod.rollLo = (a->lo + b->lo) / 2.0;
                mod.rollHi = (a->hi + b->hi) / 2.0;
            }
        } else {
            // 첫 번째 # 이 검색 값 (나머지 #은 원본처럼 max 로 쓰지 않음 - 다른 의미의 수치라서)
            mod.value = values[0];
            if (const auto* a = rangeAt(0)) {
                // 매처 negate 로 부호가 뒤집힌 경우 범위도 뒤집음
                const bool neg = values[0] < 0 && a->lo >= 0;
                mod.rollLo = neg ? -a->hi : a->lo;
                mod.rollHi = neg ? -a->lo : a->hi;
            }
        }
        if (mod.value && mod.rollLo && mod.rollHi) {
            const double lo = *mod.rollLo, hi = *mod.rollHi;
            double pct = hi > lo ? (*mod.value - lo) / (hi - lo) * 100.0 : 100.0;
            pct = qBound(0.0, pct, 100.0);
            mod.rollPct = mod.better < 0 ? 100.0 - pct : pct;
        }
    }

    QString t = pe.text;
    t.replace(QLatin1Char('\n'), QStringLiteral(" / "));
    mod.text = (mod.tier > 0 ? QStringLiteral("T%1 ").arg(mod.tier) : QString()) + t;

    mod.color = colorForType(mod.modType);
    if (mod.color.isEmpty() && mod.modType.isEmpty()) {
        const QString& sg = mod.cands[mod.selected].group;
        if (sg != QStringLiteral("explicit") && sg != QStringLiteral("pseudo"))
            mod.color = QStringLiteral("ambiguous");
    }

    mod.resistance = m_kw.isResistance(pe.key);
    mod.totalRes = mod.resistance && opt.autoCheckTotalRes;
    mod.lockedOff = m_kw.isDisabled(pe.key);
    if (mod.lockedOff) {
        mod.checked = false;
    } else if (m_checked && mod.modType != QStringLiteral("implicit") && (mod.tier == 0 || mod.tier < 3)) {
        const QString key = mod.cands[mod.selected].group == QStringLiteral("pseudo")
                                ? db.stat(mod.cands[mod.selected].entry).key
                                : mod.statKey;
        if (m_checked->isChecked(mod.statKey, it.top) || m_checked->isChecked(key, it.top)) {
            mod.checked = true;
            mod.autoChecked = true;
        }
    }

    return mod;
}

std::optional<ParsedItem> ItemParser::parse(const QString& input, Game game, const ParseOptions& opt,
                                            QString* error) const
{
    auto fail = [&](const QString& e) -> std::optional<ParsedItem> {
        if (error)
            *error = e;
        return std::nullopt;
    };

    QString textIn = input;
    textIn.remove(QLatin1Char('\r'));
    QStringList allLines = textIn.trimmed().split(QLatin1Char('\n'));
    for (QString& l : allLines)
        l = l.trimmed();
    if (allLines.size() < 3)
        return fail(QStringLiteral("not an item"));

    ParsedItem it;
    it.game = game;
    if (!m_kw.detectLang(allLines[0], &it.lang, &it.itemClass))
        return fail(QStringLiteral("not an item"));
    const Lang L = it.lang;

    const GameDb& gdb = m_dbs[gi(game)];
    const TradeDb& db = gdb[L];
    const MatcherDb& md = gdb.matchers[li(L)];
    if (!db.isLoaded())
        return fail(QStringLiteral("data not loaded"));

    // 섹션 분리
    QVector<QStringList> sections(1);
    for (const QString& l : allLines) {
        if (l == kSep)
            sections.push_back({});
        else
            sections.back() << l;
    }

    // ---- 헤더
    const QStringList& head = sections[0];
    QStringList nameLines;
    for (int i = 1; i < head.size(); ++i) {
        const QString& l = head[i];
        bool isRarity = false;
        for (const QString& k : m_kw.kw(QStringLiteral("rarity")).t[li(L)]) {
            const QString p = k + QStringLiteral(": ");
            if (l.startsWith(p)) {
                it.rarityText = l.mid(p.size()).trimmed();
                it.rarityId = m_kw.rarityId(L, it.rarityText);
                isRarity = true;
                break;
            }
        }
        if (!isRarity && !l.isEmpty())
            nameLines << text::stripSetTags(l);
    }
    if (nameLines.size() >= 2) {
        it.rawName = nameLines[0];
        it.rawType = nameLines[1];
    } else if (!nameLines.isEmpty()) {
        it.rawType = nameLines[0];
    }

    // ---- 속성 / 플래그
    const QStringList propKeys = {
        QStringLiteral("quality"),          QStringLiteral("level"),           QStringLiteral("item_level"),
        QStringLiteral("talisman_tier"),    QStringLiteral("map_tier"),        QStringLiteral("sockets"),
        QStringLiteral("heist_target"),     QStringLiteral("ultimatum_challenge"), QStringLiteral("ultimatum_reward"),
        QStringLiteral("monster_genus"),    QStringLiteral("monster_group"),   QStringLiteral("physical_damage"),
        QStringLiteral("elemental_damage"), QStringLiteral("chaos_damage"),    QStringLiteral("attacks_per_second"),
        QStringLiteral("stack_size"),       QStringLiteral("armour"),          QStringLiteral("evasion"),
        QStringLiteral("energy_shield"),    QStringLiteral("ward"),            QStringLiteral("block"),
        QStringLiteral("spirit"),           QStringLiteral("monster_level"),   QStringLiteral("map_iiq"),
        QStringLiteral("map_iir"),          QStringLiteral("map_packsize"),    QStringLiteral("map_bonus")};
    QHash<QString, QString> keyOf; // 실제 텍스트 -> 키
    for (const QString& k : propKeys) {
        for (const QString& s : m_kw.kw(k).t[li(L)])
            keyOf.insert(s, k);
    }
    QHash<QString, QString> props;
    QHash<QString, QStringList> multiProps; // 원소 피해처럼 여러 줄
    QSet<QString> flags;
    int modStartSection = -1;
    const QStringList reqWords = m_kw.kw(QStringLiteral("requirements")).t[li(L)];

    for (int s = 1; s < sections.size(); ++s) {
        const QStringList& sec = sections[s];
        if (sec.isEmpty())
            continue;
        bool reqSection = false;
        for (const QString& r : reqWords) {
            if (sec.first() == r + QLatin1Char(':'))
                reqSection = true;
        }
        for (const QString& l : sec) {
            const QString f = m_kw.flagOf(L, l);
            if (!f.isEmpty()) {
                flags.insert(f);
                continue;
            }
            if (reqSection)
                continue;
            const int c = l.indexOf(QStringLiteral(": "));
            if (c <= 0)
                continue;
            const QString k = keyOf.value(l.left(c));
            if (k.isEmpty())
                continue;
            const QString v = l.mid(c + 2).trimmed();
            multiProps[k] << v;
            if (!props.contains(k))
                props.insert(k, v);
            if ((k == QStringLiteral("item_level") || k == QStringLiteral("ultimatum_challenge")) && modStartSection < 0)
                modStartSection = s + 1;
        }
    }

    it.twiceCorrupted = flags.contains(QStringLiteral("twice_corrupted"));
    it.corrupted = it.twiceCorrupted || flags.contains(QStringLiteral("corrupted"));
    it.unidentified = flags.contains(QStringLiteral("unidentified"));
    it.mirrored = flags.contains(QStringLiteral("mirrored"));
    it.split = flags.contains(QStringLiteral("split"));
    it.imbued = flags.contains(QStringLiteral("imbued"));
    it.fractured = flags.contains(QStringLiteral("fractured_item"));
    it.synthesised = flags.contains(QStringLiteral("synthesised_item"));
    it.searing = flags.contains(QStringLiteral("searing_item"));
    it.tangled = flags.contains(QStringLiteral("tangled_item"));
    const auto& infl = m_kw.influences();
    for (int i = 0; i < infl.size(); ++i) {
        if (flags.contains(infl[i].flag))
            it.influences << i;
    }

    auto propInt = [&](const QString& k) {
        const QString d = text::digitsOnly(props.value(k));
        return d.isEmpty() ? -1 : d.toInt();
    };
    it.quality = propInt(QStringLiteral("quality"));
    it.itemLevel = propInt(QStringLiteral("item_level"));
    it.level = propInt(QStringLiteral("level"));
    it.mapTier = propInt(QStringLiteral("map_tier"));
    {
        double st = 0;
        if (text::firstNumber(props.value(QStringLiteral("stack_size")), &st))
            it.stackSize = static_cast<int>(st);
    }
    for (const QString& m : m_kw.kw(QStringLiteral("max")).t[li(L)]) {
        if (props.value(QStringLiteral("level")).contains(QStringLiteral("(") + m))
            it.levelMax = true;
    }

    // ---- 이름 / 베이스 정규화
    QString type = it.rawType;
    QString name = it.rawName;
    auto stripPrefix = [&](QString& s, const QString& key) {
        for (const QString& p : m_kw.kw(key).t[li(L)]) {
            if (s.startsWith(p + QLatin1Char(' '))) {
                s = s.mid(p.size() + 1);
                return true;
            }
        }
        return false;
    };

    if (it.rarityId == QStringLiteral("normal") || it.unidentified)
        stripPrefix(type, QStringLiteral("superior"));
    if (it.synthesised)
        stripPrefix(type, QStringLiteral("synthesised"));
    if (stripPrefix(type, QStringLiteral("blight_ravaged")))
        it.blightRavaged = true;
    else if (stripPrefix(type, QStringLiteral("blighted")))
        it.blighted = true;
    if (it.rarityId == QStringLiteral("unique") && stripPrefix(name, QStringLiteral("foulborn")))
        it.foulborn = true;
    // 새 지도 체계: 베이스가 "지도 (16등급)" / "Map (Tier 16)" -> 등급은 이름에서, 베이스는 "지도"
    // (PoE2 경로석처럼 등급별 베이스가 거래소 목록에 따로 있으면 이름은 그대로 둠)
    {
        static const QRegularExpression tierInType(
            QStringLiteral("^(.*?)\\s*\\((?:Tier\\s*(\\d+)|(\\d+)\\s*등급)\\)$"));
        const auto tm = tierInType.match(type);
        if (tm.hasMatch()) {
            if (it.mapTier <= 0)
                it.mapTier = (tm.captured(2).isEmpty() ? tm.captured(3) : tm.captured(2)).toInt();
            if (!db.hasType(type) && db.hasType(tm.captured(1)))
                type = tm.captured(1);
        }
    }

    if (it.rarityId == QStringLiteral("gem")) {
        const QStringList vaalWords = m_kw.kw(QStringLiteral("vaal")).t[li(L)];
        auto isVaalName = [&](const QString& s) {
            for (const QString& v : vaalWords) {
                if (s.startsWith(v + QLatin1Char(' ')))
                    return true;
            }
            return false;
        };
        // 변형 젬: items 의 text 가 이름, type + disc 로 검색
        for (int i : db.itemsByText(type)) {
            const ItemEntry& e = db.item(i);
            if (!e.disc.isEmpty()) {
                it.disc = e.disc;
                it.transfigured = true;
                type = e.type;
                break;
            }
        }
        // 바알 젬: 헤더는 기본 젬("가속"/"Arc of Oscillating"), 바알 스킬 이름("바알 가속"/"Vaal Arc")은 뒤쪽 한 줄 섹션.
        // 거래소에 같은 구분자(변형 젬이면 alt_x 등, 아니면 없음)의 바알 젬이 있을 때만 바꿈
        if (!isVaalName(type)) {
            for (int s = 1; s < sections.size() && !isVaalName(type); ++s) {
                const QStringList& sec = sections[s];
                if (sec.size() != 1 || !isVaalName(sec.first()))
                    continue;
                for (const QVector<int>& cands : {db.itemsByType(sec.first()), db.itemsByText(sec.first())}) {
                    for (int i : cands) {
                        const ItemEntry& e = db.item(i);
                        if (e.disc == it.disc && isVaalName(e.type)) {
                            type = e.type;
                            break;
                        }
                    }
                    if (isVaalName(type))
                        break;
                }
            }
        }
        it.vaalGem = isVaalName(type);
    }

    if (!db.hasType(type)) {
        const QString base = findBaseInName(db, type);
        if (!base.isEmpty())
            type = base;
    }
    it.type = type;
    it.name = it.rarityId == QStringLiteral("unique") ? name : QString();
    // 같은 이름 베이스가 여러 개면 거래소는 구분자가 있어야 찾음 (새 지도 "지도" + map 등)
    if (it.disc.isEmpty()) {
        for (int i : db.itemsByType(type)) {
            const ItemEntry& e = db.item(i);
            if (!e.disc.isEmpty() && e.text == e.type && e.name.isEmpty() && !e.unique) { // 변형 젬처럼 이름이 따로 있는 건 제외
                it.disc = e.disc;
                break;
            }
        }
    }

    // ---- 카테고리 / 종류
    if (!props.value(QStringLiteral("monster_genus")).isEmpty() && !props.value(QStringLiteral("monster_group")).isEmpty()) {
        it.isBeast = true;
        it.category = QStringLiteral("monster.beast");
    } else {
        it.category = resolveCategory(it, db);
    }
    if (it.category.isEmpty()) {
        if (it.rarityId == QStringLiteral("gem"))
            it.category = QStringLiteral("gem");
        else if (it.rarityId == QStringLiteral("currency"))
            it.category = QStringLiteral("currency");
        else if (it.rarityId == QStringLiteral("card"))
            it.category = QStringLiteral("card");
    }
    it.top = it.category.section(QLatin1Char('.'), 0, 0);
    if (it.top == QStringLiteral("jewel") && game == Game::Poe1 &&
        (type.contains(QStringLiteral("Cluster Jewel")) || type.contains(QStringLiteral("군 주얼"))))
        it.category = QStringLiteral("jewel.cluster");

    it.isGem = it.rarityId == QStringLiteral("gem") || it.top == QStringLiteral("gem");
    it.isCurrency = it.rarityId == QStringLiteral("currency");
    it.isCard = it.rarityId == QStringLiteral("card") || it.top == QStringLiteral("card");
    it.isMapFragment = it.category == QStringLiteral("map.fragment");
    it.isWaystone = it.category == QStringLiteral("map.waystone");
    it.isMap = (it.category == QStringLiteral("map") || it.isWaystone) && it.mapTier > 0;
    it.isGear = it.top == QStringLiteral("weapon") || it.top == QStringLiteral("armour") ||
                it.top == QStringLiteral("accessory");
    it.isWeapon = it.top == QStringLiteral("weapon");
    it.isJewel = it.top == QStringLiteral("jewel");
    it.isFlask = it.top == QStringLiteral("flask") || it.top == QStringLiteral("tincture");
    it.isHeist = props.contains(QStringLiteral("heist_target")) || it.top == QStringLiteral("heistmission");
    it.isUltimatum = props.contains(QStringLiteral("ultimatum_challenge"));
    it.detail = it.isGem || it.isMapFragment || (it.isCurrency && !it.isUltimatum) || it.isCard;

    if (const StaticEntry* se = db.staticByText(it.rawType.isEmpty() ? type : it.rawType))
        it.exchangeId = se->id;
    else if (const StaticEntry* se2 = db.staticByText(type))
        it.exchangeId = se2->id;
    if (it.isGear || it.rarityId == QStringLiteral("unique") || it.rarityId == QStringLiteral("rare"))
        it.exchangeId.clear();

    // ---- 옵션 줄
    bool optionStatSeen = false;
    int prefixes = 0, suffixes = 0;
    if (modStartSection > 0 && !it.detail) {
        static const QRegularExpression suffixRe(QStringLiteral("\\s\\(([a-z]+)\\)$"));
        static const QRegularExpression headerRe(QStringLiteral("^\\{\\s*(.*?)\\s*\\}$"));
        QStringList tierWords = m_kw.kw(QStringLiteral("option_tier")).t[li(L)];
        QString tierPat;
        for (const QString& w : tierWords)
            tierPat += (tierPat.isEmpty() ? QString() : QStringLiteral("|")) + QRegularExpression::escape(w);
        const QRegularExpression tierRe(QStringLiteral("\\((?:%1):\\s*(\\d+)\\)").arg(tierPat));

        for (int s = modStartSection; s < sections.size(); ++s) {
            QVector<CleanLine> cl;
            QString hdrType;
            QString hdrAffix;
            int hdrTier = 0;
            for (const QString& l : sections[s]) {
                if (l.isEmpty())
                    continue;
                const auto hm = headerRe.match(l);
                if (hm.hasMatch()) {
                    it.advanced = true;
                    const QString h = hm.captured(1);
                    hdrTier = 0;
                    const auto tm = tierRe.match(h);
                    if (tm.hasMatch())
                        hdrTier = tm.captured(1).toInt();
                    const QString kind = h.section(QLatin1Char('"'), 0, 0).section(QChar(0x2014), 0, 0);
                    hdrAffix = kind.contains(QStringLiteral("Prefix")) || kind.contains(QStringLiteral("접두어"))
                                    ? QStringLiteral("prefix")
                                    : (kind.contains(QStringLiteral("Suffix")) || kind.contains(QStringLiteral("접미어"))
                                           ? QStringLiteral("suffix")
                                           : QString());
                    if (kind.contains(QStringLiteral("Implicit")) || kind.contains(QStringLiteral("고정")))
                        hdrType = QStringLiteral("implicit");
                    else if (kind.contains(QStringLiteral("Crafted")) || kind.contains(QStringLiteral("제작")))
                        hdrType = QStringLiteral("crafted");
                    else if (kind.contains(QStringLiteral("Enchant")) || kind.contains(QStringLiteral("인챈트")))
                        hdrType = QStringLiteral("enchant");
                    else if (kind.contains(QStringLiteral("Fractured")) || kind.contains(QStringLiteral("분열")))
                        hdrType = QStringLiteral("fractured");
                    else
                        hdrType.clear();
                    // 혼합 옵션이나 검색 불가 옵션도 머리줄 하나당 한 칸
                    if (hdrType != QStringLiteral("implicit") && hdrType != QStringLiteral("enchant")) {
                        prefixes += hdrAffix == QStringLiteral("prefix");
                        suffixes += hdrAffix == QStringLiteral("suffix");
                    }
                    continue;
                }
                if (!m_kw.flagOf(L, l).isEmpty())
                    continue;
                if (l.startsWith(QLatin1Char('(')) && l.endsWith(QLatin1Char(')')))
                    continue; // 설명 문구
                const int c = l.indexOf(QStringLiteral(": "));
                if (c > 0 && keyOf.contains(l.left(c)))
                    continue;

                CleanLine x;
                x.raw = l;
                QString t = l;
                const auto sm = suffixRe.match(t);
                if (sm.hasMatch()) {
                    const QString g = sm.captured(1);
                    if (db.hasGroup(g) || g == QStringLiteral("implicit") || g == QStringLiteral("crafted") ||
                        g == QStringLiteral("enchant") || g == QStringLiteral("fractured")) {
                        x.modType = g;
                        t = t.left(sm.capturedStart(0));
                    }
                }
                if (x.modType.isEmpty())
                    x.modType = hdrType;
                x.tier = hdrTier;
                x.affix = hdrAffix;
                const QString withRanges = text::stripDashSuffix(t).trimmed();
                t = text::stripAdvancedRanges(withRanges).trimmed();
                x.display = t;
                const QString al = m_kw.alias(L, t);
                x.text = al.isEmpty() ? t : al;
                x.rangeSrc = al.isEmpty() ? withRanges : QString();
                cl.push_back(x);
            }

            QStringList texts;
            for (const auto& x : cl)
                texts << x.text;
            // 섹션 안에 매칭된 옵션이 하나라도 있어야 못 찾은 줄도 옵션으로 취급 (설명문/메모 섹션 제외)
            QVector<ParsedMod> secMods;
            int matched = 0;
            for (int i = 0; i < cl.size();) {
                int consumed = 0;
                QVector<StatMatch> ms = db.match(texts, i, &consumed);
                if (ms.isEmpty() && md.isLoaded()) {
                    // 매처: 숫자 없는 100% 문구, "감소" 표기 등 게임 문구 -> 거래소 stat
                    const QVector<MatcherHit> hits = md.match(texts, i, &consumed);
                    if (!hits.isEmpty()) {
                        const MatcherHit& h = hits.first();
                        const MatcherStat& st = md.stat(h.stat);
                        QVector<double> vals = h.values;
                        if (st.inverted) {
                            for (double& v : vals)
                                v = -v;
                        }
                        for (const auto& grp : st.ids) {
                            for (const QString& id : grp.second) {
                                const int e = db.findStat(id);
                                if (e >= 0) {
                                    ms.push_back({e, vals, h.hashIdx});
                                    break;
                                }
                            }
                        }
                    }
                }
                if (ms.isEmpty() && L == Lang::En && !texts[i].contains(QRegularExpression(QStringLiteral("\\d")))) {
                    // "Blind Enemies on Hit" 처럼 100% 라 숫자가 빠진 줄 -> "#% chance to ..."
                    QStringList t2 = texts;
                    t2[i] = QStringLiteral("100% chance to ") + texts[i];
                    ms = db.match(t2, i, &consumed);
                }
                if (ms.isEmpty() || consumed <= 0) {
                    it.unmatched << cl[i].raw;
                    ParsedMod u;
                    u.raw = cl[i].raw;
                    u.text = cl[i].text;
                    u.modType = cl[i].modType;
                    u.tier = cl[i].tier;
                    u.affix = cl[i].affix;
                    u.shown = cl[i].display;
                    u.unmatched = true;
                    u.lockedOff = true;
                    u.color = colorForType(u.modType);
                    secMods.push_back(u);
                    ++i;
                    continue;
                }
                QStringList raws, rsrc, shown;
                bool rangesOk = true;
                for (int k = i; k < i + consumed; ++k) {
                    raws << cl[k].raw;
                    rsrc << cl[k].rangeSrc;
                    shown << cl[k].display;
                    rangesOk = rangesOk && !cl[k].rangeSrc.isEmpty();
                }
                secMods.push_back(buildMod(it, db, md, ms, cl[i], raws.join(QLatin1Char('\n')),
                                           rangesOk ? rsrc.join(QLatin1Char('\n')) : QString(), opt, &optionStatSeen));
                secMods.back().shown = shown.join(QStringLiteral(" / "));
                secMods.back().affix = cl[i].affix;
                ++matched;
                i += consumed;
            }
            for (const ParsedMod& m : secMods) {
                if (!m.unmatched || matched > 0)
                    it.mods.push_back(m);
            }
        }
    }
    if (it.advanced && (it.isGear || it.isJewel) && !it.unidentified && !it.corrupted && !it.mirrored) {
        int limit = 0;
        if (it.rarityId == QStringLiteral("magic"))
            limit = 1;
        else if (it.rarityId == QStringLiteral("rare"))
            limit = it.isJewel ? 2 : 3;
        if (limit > 0) {
            int prefixLimit = limit, suffixLimit = limit;
            // 강탈 반지나 PoE2 제작 주얼처럼 개수 상한을 바꾸는 옵션
            for (const ParsedMod& mod : it.mods) {
                if (mod.statKey == QStringLiteral("stat_3182714256") && mod.value)
                    prefixLimit += qRound(*mod.value);
                else if (mod.statKey == QStringLiteral("stat_718638445") && mod.value)
                    suffixLimit += qRound(*mod.value);
            }
            if (prefixes <= qMax(0, prefixLimit) && suffixes <= qMax(0, suffixLimit)) {
                it.emptyPrefixes = qMax(0, prefixLimit) - prefixes;
                it.emptySuffixes = qMax(0, suffixLimit) - suffixes;
            }
        }
    }
    // 성단 주얼: 인챈트(옵션형)가 있으면 패시브 개수 줄도 자동 체크 (원본 동작)
    if (optionStatSeen) {
        for (auto& m : it.mods) {
            if (!m.unmatched) {
                if (!m.lockedOff)
                    m.checked = true;
                break;
            }
        }
    }

    // ---- 설명 텍스트
    if (it.detail) {
        QStringList out;
        const QStringList unstack = m_kw.kw(QStringLiteral("unstack")).t[li(L)];
        for (int s = 1; s < sections.size(); ++s) {
            for (const QString& l : sections[s]) {
                if (l.isEmpty())
                    continue;
                bool skip = false;
                for (const QString& u : unstack)
                    skip = skip || l.startsWith(u);
                if (!skip)
                    out << text::stripMarkup(l);
            }
            if (out.size() > 14)
                break;
        }
        it.detailText = out.join(QLatin1Char('\n'));
    }

    // ---- 소켓
    const QString sock = props.value(QStringLiteral("sockets")).trimmed();
    if (!sock.isEmpty()) {
        int total = 0, maxLink = 0;
        for (const QString& grp : sock.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            const int n = grp.split(QLatin1Char('-'), Qt::SkipEmptyParts).size();
            total += n;
            maxLink = qMax(maxLink, n);
        }
        it.sockets = total;
        it.links = (game == Game::Poe1 && maxLink >= 2) ? maxLink : -1;
        it.checkSockets = it.links > 4;
    }

    // ---- 강탈 / 결전
    if (it.isHeist) {
        static const QRegularExpression paren(QStringLiteral("\\(([^()]+)\\)\\s*$"));
        const auto pm = paren.match(props.value(QStringLiteral("heist_target")));
        const QString v = pm.hasMatch() ? pm.captured(1) : props.value(QStringLiteral("heist_target"));
        for (const auto& o : db.filterOptions(QStringLiteral("heist_objective_value"))) {
            if (!o.id.isEmpty() && o.text == v)
                it.heistValue = o.id;
        }
        it.checkLevel = true;
    }
    if (it.isUltimatum) {
        const QString r = props.value(QStringLiteral("ultimatum_reward"));
        for (const auto& u : m_kw.ultimatumRewards()) {
            if (u.text.has(L, r))
                it.ultimatumReward = u.id;
        }
        if (it.ultimatumReward.isEmpty() && !r.isEmpty()) {
            it.ultimatumReward = QStringLiteral("ExchangeUnique");
            it.ultimatumOutput = r;
        }
    }

    // ---- 기본 체크값
    if (it.isGem) {
        // APT/EE2: 레벨은 최대 레벨일 때만(PoE2 는 19 이상), 품질은 16 이상 (변형 젬은 20)
        it.checkLevel = it.levelMax || (it.game == Game::Poe2 && it.level >= 19);
        it.checkQuality = it.quality >= (it.transfigured ? 20 : 16);
    } else if (it.isMap && !it.isWaystone) {
        it.checkLevel = true;
    }
    if (it.isGear || it.isFlask) {
        const int qmax = it.top == QStringLiteral("accessory") ? 4 : 20;
        it.highlightQuality = it.quality > qmax;
        // 20 넘는 퀄리티(30% 등)는 값이 크게 달라서 기본으로 검색에 넣음 (APT: 20 초과면 켬)
        if (it.top != QStringLiteral("accessory") && it.quality > 20)
            it.checkQuality = true;
    }
    // 타락: 원본 auto_select_corrupt 설정이 있으면 그거, 없으면 아이템 상태 그대로 (APT 와 같음)
    //  타락 아님 -> 타락 아닌 매물만 / 한 번 타락 -> 두 번 타락 제외 / 두 번 타락 -> 두 번 타락만
    if (it.isGear || it.isFlask || it.isGem || it.isMap) {
        if (opt.autoSelectCorrupt == QStringLiteral("yes"))
            it.corruptDefault = 1;
        else if (opt.autoSelectCorrupt == QStringLiteral("no"))
            it.corruptDefault = 2;
        else
            it.corruptDefault = it.corrupted ? 1 : 2;
        if (it.corruptDefault == 1 && it.corrupted)
            it.twiceCorruptDefault = it.twiceCorrupted ? 1 : 2;
    }
    // 삿된 고유: 삿된이면 삿된만, 아니면 삿된 제외 (PoE2 의 같은 필터는 함양된 바알 고유라 건드리지 않음)
    if (it.game == Game::Poe1 && it.rarityId == QStringLiteral("unique") && !it.unidentified)
        it.mutatedDefault = it.foulborn ? 1 : 2;
    if (it.isGear && !it.checkLevel && m_checked && it.itemLevel > 0) {
        QStringList names;
        for (int i : it.influences)
            names << infl[i].name.t[0] << infl[i].name.t[1];
        const int need = m_checked->baseMinLevel(it.type, names);
        if (need > 0 && need <= it.itemLevel) {
            it.checkLevel = true;
            it.highlightLevel = true;
        }
    }
    // PoE1 마법 분열 장비 = 제작 베이스: 아이템 레벨(최소)로 상위 옵션 등급이 갈려서 기본으로 켬
    if (it.game == Game::Poe1 && it.isGear && it.fractured && it.rarityId == QStringLiteral("magic") && it.itemLevel > 0)
        it.checkLevel = true;
    for (const QString& b : opt.autoSelectByType) {
        const QString t = b.trimmed().toLower();
        if (!t.isEmpty() && (t == it.category || t == it.top))
            it.byCategory = true;
    }
    if (it.rarityId == QStringLiteral("unique")) {
        it.rarityFilter = QStringLiteral("unique");
        it.checkAllUnique = opt.autoCheckUnique;
        if (it.checkAllUnique) {
            for (auto& m : it.mods) {
                // 고급 설명에서 범위가 없는 수치 = 고정 롤 -> 모든 매물이 같으니 검색에서 뺌 (APT)
                const bool fixedRoll = it.advanced && m.value && !m.rollLo && !m.optionStat;
                if (!m.lockedOff && m.color.isEmpty() && !fixedRoll)
                    m.checked = true;
            }
        }
    } else if (it.rarityId == QStringLiteral("rare") || it.rarityId == QStringLiteral("magic")) {
        it.rarityFilter = QStringLiteral("nonunique");
    }

    // ---- DPS
    if (it.isWeapon && !it.unidentified) {
        const double phys = damageSum(multiProps.value(QStringLiteral("physical_damage")));
        const double ele = damageSum(multiProps.value(QStringLiteral("elemental_damage")));
        const double chaos = damageSum(multiProps.value(QStringLiteral("chaos_damage")));
        double aps = 0;
        text::firstNumber(props.value(QStringLiteral("attacks_per_second")), &aps);
        double speedIncr = 0, physIncr = 0;
        for (const auto& m : it.mods) {
            if (!m.value || m.unmatched)
                continue;
            if (m.statKey == m_kw.localAttackSpeed())
                speedIncr += *m.value;
            else if (m.statKey == m_kw.physicalDamageIncrease())
                physIncr += *m.value;
        }
        if (speedIncr > 0 && aps > 0) {
            double base = aps / (speedIncr / 100.0 + 1.0);
            const double mod = std::fmod(base, 0.05);
            base += mod > 0.025 ? (0.05 - mod) : -mod;
            aps = base * (speedIncr / 100.0 + 1.0);
        }
        double p = phys / 2.0 * aps;
        const double q = it.quality < 0 ? 0 : it.quality;
        if (q < 20)
            p = p * (physIncr + 120.0) / (physIncr + q + 100.0); // Q20 기준
        it.pdps = p;
        it.edps = ele / 2.0 * aps;
        it.cdps = chaos / 2.0 * aps;
        it.hasDps = aps > 0 && (phys + ele + chaos) > 0;
        if (it.hasDps) {
            it.props.push_back({QStringLiteral("dps"), it.pdps + it.edps + it.cdps});
            if (it.pdps > 0)
                it.props.push_back({QStringLiteral("pdps"), it.pdps});
            if (it.edps > 0)
                it.props.push_back({QStringLiteral("edps"), it.edps});
        }
    }

    // 방어 속성: 거래소는 퀄리티 20% 기준 값으로 검색 (APT calc-q20 와 같음)
    if (it.isGear && !it.unidentified) {
        // 로컬 "% 증가" 옵션 (방어구 전용, 혼합형 포함)
        static const QHash<QString, QString> incrOf = {
            {QStringLiteral("stat_1062208444"), QStringLiteral("ar")},  {QStringLiteral("stat_124859000"), QStringLiteral("ev")},
            {QStringLiteral("stat_4015621042"), QStringLiteral("es")},  {QStringLiteral("stat_2451402625"), QStringLiteral("ar,ev")},
            {QStringLiteral("stat_3321629045"), QStringLiteral("ar,es")}, {QStringLiteral("stat_1999113824"), QStringLiteral("ev,es")},
            {QStringLiteral("stat_3523867985"), QStringLiteral("ar,ev,es")},
        };
        QHash<QString, double> incr;
        for (const auto& m : it.mods) {
            if (!m.value || m.unmatched)
                continue;
            const QString t = incrOf.value(m.statKey);
            for (const QString& k : t.split(QLatin1Char(','), Qt::SkipEmptyParts))
                incr[k] += *m.value;
        }
        const double q = it.quality < 0 ? 0 : it.quality;
        const QList<QPair<QString, QString>> defs = {{QStringLiteral("armour"), QStringLiteral("ar")},
                                                     {QStringLiteral("evasion"), QStringLiteral("ev")},
                                                     {QStringLiteral("energy_shield"), QStringLiteral("es")},
                                                     {QStringLiteral("ward"), QStringLiteral("ward")}};
        for (const auto& d : defs) {
            double v = 0;
            if (!text::firstNumber(props.value(d.first), &v) || v <= 0)
                continue;
            if (q < 20) {
                const double inc = incr.value(d.second);
                v = v * (inc + 120.0) / (inc + q + 100.0);
            }
            it.props.push_back({d.second, v});
        }
        for (const auto& d : {qMakePair(QStringLiteral("block"), QStringLiteral("block")),
                              qMakePair(QStringLiteral("spirit"), QStringLiteral("spirit"))}) {
            double v = 0;
            if (text::firstNumber(props.value(d.first), &v) && v > 0)
                it.props.push_back({d.second, v});
        }
    }

    // ---- 분열된 아이템: 분열 옵션이 가치의 핵심이라 기본으로 켬
    if (it.fractured) {
        for (ParsedMod& m : it.mods) {
            if (m.modType == QStringLiteral("fractured") && !m.lockedOff && !m.unmatched)
                m.checked = true;
        }
    }

    // ---- 지도 속성 (아이템 수량/희귀도/무리 규모/아틀라스 획득물) + 아이템 레벨: 기본 꺼짐, 켜서 최소/최대 조절
    if (it.isMap) {
        for (const char* k : {"map_iiq", "map_iir", "map_packsize", "map_bonus"}) {
            for (const QString& v : multiProps.value(QLatin1String(k))) {
                double x = 0;
                if (text::firstNumber(v, &x) && x > 0) {
                    it.props.push_back({QLatin1String(k), x});
                    break;
                }
            }
        }
        if (it.itemLevel > 0)
            it.props.push_back({QStringLiteral("ilvl"), static_cast<double>(it.itemLevel)});
    }

    // ---- 속성 자동 체크 (APT item-property.ts): 고유 아닌 장비만
    if (!it.props.isEmpty() && it.rarityId != QStringLiteral("unique")) {
        auto prop = [&](const char* k) -> ItemProp* {
            for (ItemProp& p : it.props) {
                if (p.key == QLatin1String(k))
                    return &p;
            }
            return nullptr;
        };
        // 방어: 방어 종류가 하나뿐인 베이스면 그 속성을 켬
        QVector<ItemProp*> defs;
        for (const char* k : {"ar", "ev", "es"}) {
            if (ItemProp* p = prop(k))
                defs << p;
        }
        if (defs.size() == 1)
            defs[0]->use = true;
        // 무기: 물리 위주(67% 이상)면 물리 DPS, 원소 위주면 원소 DPS, 섞였으면 전체 DPS. 마법 무기(완드/셉터)는 안 켬
        const bool caster = it.category.contains(QStringLiteral("wand")) || it.category.contains(QStringLiteral("sceptre"));
        ItemProp* dps = prop("dps");
        if (dps && dps->value > 0 && !caster) {
            ItemProp* pd = prop("pdps");
            ItemProp* ed = prop("edps");
            if (pd && pd->value / dps->value >= 0.67)
                pd->use = true;
            else if (ed && ed->value / dps->value >= 0.67)
                ed->use = true;
            else
                dps->use = true;
        }
        // 켠 속성에 이미 들어간 로컬 옵션은 체크 해제 (같은 걸 두 번 거는 것 방지)
        static const QSet<QString> defLocal = {
            QStringLiteral("stat_4052037485"), QStringLiteral("stat_124859000"),  QStringLiteral("stat_53045048"),
            QStringLiteral("stat_1062208444"), QStringLiteral("stat_3484657501"), QStringLiteral("stat_4015621042"),
            QStringLiteral("stat_1999113824"), QStringLiteral("stat_3321629045"), QStringLiteral("stat_2451402625"),
            QStringLiteral("stat_3523867985")};
        static const QSet<QString> physLocal = {QStringLiteral("stat_1940865751")}; // 물리 피해 추가 (특정)
        static const QSet<QString> eleLocal = {QStringLiteral("stat_709508406"), QStringLiteral("stat_1037193709"),
                                               QStringLiteral("stat_3336890334")}; // 화염/냉기/번개 피해 추가 (특정)
        bool defOn = false;
        for (ItemProp* p : defs)
            defOn = defOn || p->use;
        const bool pdOn = prop("pdps") && prop("pdps")->use, edOn = prop("edps") && prop("edps")->use,
                   dOn = dps && dps->use;
        for (ParsedMod& m : it.mods) {
            if (m.modType == QStringLiteral("fractured"))
                continue; // 분열 옵션은 그 자체가 가치라 속성에 들어가도 그대로 둠
            const QString& k = m.statKey;
            const bool speed = k == m_kw.localAttackSpeed(), physInc = k == m_kw.physicalDamageIncrease();
            if ((defOn && defLocal.contains(k)) || ((pdOn || dOn) && (physLocal.contains(k) || physInc || speed)) ||
                ((edOn || dOn) && (eleLocal.contains(k) || speed)))
                m.checked = false;
        }
    }

    // ---- 클러스터 주얼 (APT applyClusterJewelRules)
    if (it.category == QStringLiteral("jewel.cluster")) {
        for (ParsedMod& m : it.mods) {
            if (m.statKey == QStringLiteral("stat_3086156145") && m.value) { // 패시브 # 개 추가
                const int v = static_cast<int>(std::lround(*m.value));
                m.checked = true;
                m.rangeLocked = true;
                m.min.reset();
                m.max.reset();
                if (v == 4) {
                    m.min = 4;
                    m.max = 5;
                } else if (v == 2 || v == 8 || v == 9 || (v == 5 && it.rarityId == QStringLiteral("rare"))) {
                    m.max = v; // 적을수록 좋은 구간 (노트 비중이 큼)
                } else {
                    m.min = v;
                }
            } else if (m.statKey == QStringLiteral("stat_4079888060")) { // 추가 패시브 중 주얼 홈 수
                m.checked = false;
            }
        }
        // 아이템 레벨 구간 (노트 목록이 바뀌는 경계): 1 / 50 / 68 / 75 / 84
        if (it.itemLevel > 0) {
            static const int bp[] = {1, 50, 68, 75, 84};
            for (int i = 0; i < 5; ++i) {
                if (it.itemLevel >= bp[i]) {
                    it.ilvlFloor = bp[i];
                    it.ilvlCeil = i + 1 < 5 ? bp[i + 1] - 1 : -1;
                }
            }
            it.checkLevel = true;
        }
    }

    applyStatRanges(it, opt.statRangePct);
    return it;
}

bool exactValuesOnly(const ParsedItem& it)
{
    // APT: 미러/타락 매직은 더 못 바꾸니 정확값
    return it.mirrored || (it.corrupted && it.rarityId == QStringLiteral("magic"));
}

void applyStatRange(ParsedMod& m, int pct, bool unique)
{
    if (!m.value || m.optionStat || m.unmatched || m.rangeLocked)
        return;
    const double v = *m.value;
    double delta = 0;
    if (pct > 0) {
        if (unique && m.rollLo && m.rollHi && *m.rollHi > *m.rollLo)
            delta = (*m.rollHi - *m.rollLo) * pct / 100.0; // 고유: 롤 범위 폭 기준
        else
            delta = std::fabs(v) * pct / 100.0;
        const bool integral = std::fabs(v - std::round(v)) < 1e-9;
        delta = integral ? std::floor(delta) : std::floor(delta * 10.0) / 10.0; // 작은 정수(소켓 수 등)는 그대로
        if (unique && m.rollPct && *m.rollPct >= 99.999)
            delta = 0; // 최대 롤 고유는 정확값
    }
    m.min.reset();
    m.max.reset();
    if (m.better > 0) {
        m.min = v - delta;
    } else if (m.better < 0) {
        m.max = v + delta;
    } else {
        m.min = v - delta;
        m.max = v + delta;
    }
}

void applyStatRanges(ParsedItem& it, int pct)
{
    const bool unique = it.rarityId == QStringLiteral("unique");
    const int p = exactValuesOnly(it) ? 0 : pct;
    for (ParsedMod& m : it.mods)
        applyStatRange(m, p, unique);
}

} // namespace pts
