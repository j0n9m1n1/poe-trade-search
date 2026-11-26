// SPDX-License-Identifier: GPL-3.0-or-later
//
// 코어 테스트. 파서 테스트는 실제 거래소 데이터가 필요함:
//   PTS_TEST_DATA=<dir>  (안에 cache/poe1/en/stats.json 등)
// 없으면 해당 테스트는 SKIP.
#include "core/CheckedStore.h"
#include "core/ItemParser.h"
#include "core/JsonUtil.h"
#include "core/Keywords.h"
#include "core/PriceSummary.h"
#include "core/QueryBuilder.h"
#include "core/TextUtil.h"
#include "net/DataManager.h"
#include "net/Endpoints.h"
#include "net/RateLimiter.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace pts;

namespace {
QString fixture(const QString& name)
{
    QFile f(QStringLiteral(PTS_FIXTURE_DIR "/") + name);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll());
}

const ParsedMod* findMod(const ParsedItem& it, const QString& statKey)
{
    for (const auto& m : it.mods) {
        if (m.statKey == statKey)
            return &m;
    }
    return nullptr;
}
} // namespace

class TestCore : public QObject {
    Q_OBJECT

private:
    Keywords kw;
    GameDb dbs[2];
    CheckedStore checked;
    bool haveData = false;

    std::optional<ParsedItem> parse(const QString& fx, Game g, ParseOptions opt = {})
    {
        ItemParser p(kw, dbs, &checked);
        QString err;
        auto r = p.parse(fixture(fx), g, opt, &err);
        if (!r)
            qWarning() << "parse failed" << fx << err;
        return r;
    }

private slots:
    void initTestCase()
    {
        Q_INIT_RESOURCE(resources);
        QString err;
        QVERIFY2(kw.load(QStringLiteral(":/data/parser.json"), &err), qPrintable(err));
        const QString dir = qEnvironmentVariable("PTS_TEST_DATA");
        if (!dir.isEmpty()) {
            DataManager dm(dir, nullptr);
            haveData = dm.load(Game::Poe1, &dbs[0], kw, &err) && dm.load(Game::Poe2, &dbs[1], kw, &err);
        }
    }

    // ---------------- 데이터 없이 도는 테스트
    void normalize()
    {
        QCOMPARE(text::normalizeStat(QStringLiteral("+41 to maximum Life")), QStringLiteral("# to maximum life"));
        QCOMPARE(text::normalizeStat(QStringLiteral("+#% to Fire Resistance")), QStringLiteral("#% to fire resistance"));
        QCOMPARE(text::normalizeStat(QStringLiteral("Adds 10 to 20  Fire Damage")), QStringLiteral("adds # to # fire damage"));
        QCOMPARE(text::normalizeStat(QStringLiteral("a 1\nb  -2.5")), QStringLiteral("a #\nb #"));
        const auto t = text::tokens(QStringLiteral("Adds -5 to +10.5 #"));
        QCOMPARE(t.size(), 3);
        QCOMPARE(t[0].value, -5.0);
        QCOMPARE(t[1].value, 10.5);
        QVERIFY(t[2].hash);
        QCOMPARE(text::stripAdvancedRanges(QStringLiteral("+64(60-69) to maximum Life")), QStringLiteral("+64 to maximum Life"));
        QCOMPARE(text::stripAdvancedRanges(QStringLiteral("+38(36-41)% to Fire")), QStringLiteral("+38% to Fire"));
        QCOMPARE(text::stripDashSuffix(QStringLiteral("10% more Damage — Unscalable Value")), QStringLiteral("10% more Damage"));
        QCOMPARE(text::digitsOnly(QStringLiteral("+20% (augmented)")), QStringLiteral("20"));
        QCOMPARE(text::digitsOnly(QStringLiteral("20 (Max)")), QStringLiteral("20"));
    }

    void lenientJson()
    {
        const QByteArray in = "\xEF\xBB\xBF{\"a\": [1, 2,], // c\n \"b\": \"x,]\",}";
        QString err;
        const QJsonDocument d = json::parseLenient(in, &err);
        QVERIFY2(!d.isNull(), qPrintable(err));
        QCOMPARE(d.object().value(QStringLiteral("a")).toArray().size(), 2);
        QCOMPARE(d.object().value(QStringLiteral("b")).toString(), QStringLiteral("x,]"));
    }

    void rateLimiter()
    {
        RateLimiter rl;
        const auto P = RateLimiter::Policy::Search;
        qint64 t = 1000000;
        for (int i = 0; i < 5; ++i) {
            QCOMPARE(rl.waitMs(P, t), qint64(0));
            rl.record(P, t);
            t += 100;
        }
        QVERIFY(rl.waitMs(P, t) > 9000); // 5:10 꽉 참
        QHash<QByteArray, QByteArray> h{{"X-Rate-Limit-Rules", "Ip"},
                                        {"X-Rate-Limit-Ip", "8:10:60"},
                                        {"X-Rate-Limit-Ip-State", "1:10:0"}};
        RateLimiter r2;
        r2.update(P, [&](const QByteArray& k) { return h.value(k); }, t);
        for (int i = 0; i < 8; ++i)
            r2.record(P, t + i);
        QVERIFY(r2.waitMs(P, t + 10) > 0);
        QHash<QByteArray, QByteArray> h3{{"X-Rate-Limit-Rules", "Ip"},
                                         {"X-Rate-Limit-Ip", "8:10:60"},
                                         {"X-Rate-Limit-Ip-State", "9:10:60"}};
        RateLimiter r3;
        r3.update(P, [&](const QByteArray& k) { return h3.value(k); }, t);
        QVERIFY(r3.waitMs(P, t) >= 59000);
    }

    void endpointsUrls()
    {
        QCOMPARE(endpoints::search(Game::Poe1, Lang::En, QStringLiteral("Hardcore Covens")).toEncoded(),
                 QByteArray("https://www.pathofexile.com/api/trade/search/Hardcore%20Covens"));
        QCOMPARE(endpoints::search(Game::Poe2, Lang::Ko, QStringLiteral("Standard")).toString(),
                 QStringLiteral("https://poe.kakaogames.com/api/trade2/search/poe2/Standard"));
        QCOMPARE(endpoints::fetch(Game::Poe2, Lang::En, {QStringLiteral("a"), QStringLiteral("b")}, QStringLiteral("Q")).toString(),
                 QStringLiteral("https://www.pathofexile.com/api/trade2/fetch/a,b?query=Q&realm=poe2"));
        QCOMPARE(endpoints::webSearch(Game::Poe1, Lang::Ko, QStringLiteral("Standard"), QStringLiteral("abc")).toString(),
                 QStringLiteral("https://poe.kakaogames.com/trade/search/Standard/abc"));
    }

    void priceSummary()
    {
        QVector<Listing> v;
        for (double a : {1.0, 1.0, 2.0, 2.0, 2.0, 5.0})
            v.push_back({a, QStringLiteral("chaos"), {}, {}});
        const QString s = PriceSummary::summarize(v, [](const QString&) { return QStringLiteral("카오스"); });
        QCOMPARE(s, QStringLiteral("1 카오스 ~ 5 카오스 = 2 카오스[3], 1 카오스[2]"));
        const QDateTime now = QDateTime::fromString(QStringLiteral("2026-11-26T12:00:00Z"), Qt::ISODate);
        QCOMPARE(PriceSummary::ago(now.addSecs(-300), now), QStringLiteral("5분 전"));
        QCOMPARE(PriceSummary::ago(QDateTime::fromString(QStringLiteral("2026-11-26T09:00:00+00:00"), Qt::ISODate), now),
                 QStringLiteral("3시간 전"));
    }

    void queryBuilderNoDb()
    {
        SearchRequest r;
        r.game = Game::Poe2;
        r.type = QStringLiteral("Conqueror Plate");
        r.rarity = QStringLiteral("nonunique");
        r.useQuality = true;
        r.qualityMin = 20;
        r.corrupted = 2;
        r.stats.push_back({QStringLiteral("explicit.stat_3299347043"), 80.0, std::nullopt, {}, false});
        r.stats.push_back({QStringLiteral("explicit.stat_1"), std::nullopt, std::nullopt, {}, true});
        r.includeDisabledStats = false;
        const QJsonObject q = QueryBuilder::build(r, nullptr).value(QStringLiteral("query")).toObject();
        QCOMPARE(q.value(QStringLiteral("type")).toString(), QStringLiteral("Conqueror Plate"));
        const QJsonObject tf = q.value(QStringLiteral("filters")).toObject().value(QStringLiteral("type_filters")).toObject();
        QCOMPARE(tf.value(QStringLiteral("filters")).toObject().value(QStringLiteral("quality")).toObject().value(QStringLiteral("min")).toDouble(), 20.0);
        const QJsonObject mf = q.value(QStringLiteral("filters")).toObject().value(QStringLiteral("misc_filters")).toObject();
        QCOMPARE(mf.value(QStringLiteral("filters")).toObject().value(QStringLiteral("corrupted")).toObject().value(QStringLiteral("option")).toString(), QStringLiteral("false"));
        const QJsonArray sf = q.value(QStringLiteral("stats")).toArray()[0].toObject().value(QStringLiteral("filters")).toArray();
        QCOMPARE(sf.size(), 1);
        QVERIFY(!q.contains(QStringLiteral("name")));
    }

    // ---------------- 실데이터 테스트
    void poe1RareRing()
    {
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        auto it = parse(QStringLiteral("poe1_en_rare_ring.txt"), Game::Poe1);
        QVERIFY(it);
        QCOMPARE(it->lang, Lang::En);
        QCOMPARE(it->category, QStringLiteral("accessory.ring"));
        QCOMPARE(it->type, QStringLiteral("Two-Stone Ring"));
        QCOMPARE(it->rarityId, QStringLiteral("rare"));
        QCOMPARE(it->itemLevel, 81);
        QVERIFY(it->corrupted);
        QCOMPARE(it->mods.size(), 5);

        const ParsedMod* imp = findMod(*it, QStringLiteral("stat_4277795662"));
        QVERIFY(imp);
        QCOMPARE(imp->modType, QStringLiteral("implicit"));
        QCOMPARE(imp->current()->id, QStringLiteral("implicit.stat_4277795662"));
        QCOMPARE(*imp->min, 15.0);
        QVERIFY(imp->resistance);

        const ParsedMod* life = findMod(*it, QStringLiteral("stat_3299347043"));
        QVERIFY(life);
        QCOMPARE(life->current()->id, QStringLiteral("pseudo.pseudo_total_life"));
        QCOMPARE(*life->min, 41.0);
        QVERIFY(!life->max);

        const ParsedMod* fire = findMod(*it, QStringLiteral("stat_3372524247"));
        QVERIFY(fire && fire->resistance && fire->totalRes);
        QCOMPARE(*fire->min, 38.0);
        QVERIFY(it->unmatched.isEmpty());
    }

    void poe1RareSwordLocalAndDps()
    {
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        auto it = parse(QStringLiteral("poe1_en_rare_sword.txt"), Game::Poe1);
        QVERIFY(it);
        QCOMPARE(it->category, QStringLiteral("weapon.rapier"));
        QVERIFY(it->isWeapon);
        QCOMPARE(it->sockets, 4);
        QCOMPARE(it->links, 3);
        QCOMPARE(it->quality, 20);

        const ParsedMod* fire = findMod(*it, QStringLiteral("stat_709508406"));
        QVERIFY2(fire, "Adds # to # Fire Damage (Local) 로 잡혀야 함");
        QCOMPARE(fire->current()->id, QStringLiteral("explicit.stat_709508406"));
        QCOMPARE(*fire->min, 15.0); // (10+20)/2
        QVERIFY(!fire->max);

        const ParsedMod* as = findMod(*it, QStringLiteral("stat_210067635"));
        QVERIFY2(as, "공속은 로컬(stat_210067635)이어야 함");
        QCOMPARE(as->current()->id, QStringLiteral("explicit.stat_210067635"));

        QVERIFY(it->hasDps);
        QVERIFY(it->pdps > 100 && it->pdps < 200);
        QVERIFY(it->edps > 20 && it->edps < 40);
    }

    void poe1ClusterJewel()
    {
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        auto it = parse(QStringLiteral("poe1_en_cluster.txt"), Game::Poe1);
        QVERIFY(it);
        QCOMPARE(it->category, QStringLiteral("jewel.cluster"));
        QCOMPARE(it->mods.size(), 4);
        const ParsedMod* passives = findMod(*it, QStringLiteral("stat_3086156145"));
        QVERIFY(passives);
        QCOMPARE(passives->current()->id, QStringLiteral("enchant.stat_3086156145"));
        QVERIFY(passives->checked);
        QCOMPARE(*passives->max, 8.0); // reverse 목록이라 min/max 뒤집힘 (원본 동작)

        const ParsedMod* ench = findMod(*it, QStringLiteral("stat_3948993189"));
        QVERIFY(ench);
        QVERIFY(ench->optionStat);
        QCOMPARE(ench->current()->id, QStringLiteral("enchant.stat_3948993189"));
        QCOMPARE(ench->current()->option.toInt(), 13);
        QVERIFY(ench->checked);

        QVERIFY(findMod(*it, QStringLiteral("stat_1705633890")));
    }

    void poe1TransfiguredGem()
    {
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        auto it = parse(QStringLiteral("poe1_en_gem_transfigured.txt"), Game::Poe1);
        QVERIFY(it);
        QVERIFY(it->isGem);
        QVERIFY(it->detail);
        QCOMPARE(it->type, QStringLiteral("Arc"));
        QCOMPARE(it->disc, QStringLiteral("alt_y"));
        QCOMPARE(it->level, 20);
        QVERIFY(it->levelMax && it->checkLevel && it->checkQuality);
        QCOMPARE(it->category, QStringLiteral("gem.activegem"));
    }

    void poe1Unique()
    {
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        auto it = parse(QStringLiteral("poe1_en_unique.txt"), Game::Poe1);
        QVERIFY(it);
        QCOMPARE(it->name, QStringLiteral("Kaom's Heart"));
        QCOMPARE(it->type, QStringLiteral("Glorious Plate"));
        QCOMPARE(it->rarityFilter, QStringLiteral("unique"));
        const ParsedMod* life = findMod(*it, QStringLiteral("stat_3299347043"));
        QVERIFY(life && life->checked);

        SearchRequest r;
        r.game = Game::Poe1;
        r.name = it->name;
        r.type = it->type;
        r.rarity = it->rarityFilter;
        const QJsonObject q = QueryBuilder::build(r, &dbs[0][Lang::En]).value(QStringLiteral("query")).toObject();
        QCOMPARE(q.value(QStringLiteral("name")).toString(), QStringLiteral("Kaom's Heart"));
    }

    void poe1Currency()
    {
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        auto it = parse(QStringLiteral("poe1_en_currency.txt"), Game::Poe1);
        QVERIFY(it);
        QVERIFY(it->isCurrency && it->detail);
        QCOMPARE(it->exchangeId, QStringLiteral("divine"));
        QCOMPARE(it->stackSize, 3);
        QVERIFY(!it->detailText.contains(QStringLiteral("Shift click")));
    }

    void poe1KoreanRing()
    {
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        auto it = parse(QStringLiteral("poe1_ko_rare_ring.txt"), Game::Poe1);
        QVERIFY(it);
        QCOMPARE(it->lang, Lang::Ko);
        QCOMPARE(it->category, QStringLiteral("accessory.ring"));
        QCOMPARE(it->type, QStringLiteral("쌍보석 반지"));
        QCOMPARE(it->mods.size(), 5);
        const ParsedMod* life = findMod(*it, QStringLiteral("stat_3299347043"));
        QVERIFY(life);
        QCOMPARE(life->current()->id, QStringLiteral("pseudo.pseudo_total_life"));
        QCOMPARE(life->current()->label, QStringLiteral("유사"));

        QString t, n;
        QVERIFY(dbs[0].translateItem(Lang::Ko, Lang::En, it->type, QString(), &t, &n));
        QCOMPARE(t, QStringLiteral("Two-Stone Ring"));
    }

    void poe1AdvancedTiers()
    {
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        auto it = parse(QStringLiteral("poe1_en_advanced.txt"), Game::Poe1);
        QVERIFY(it);
        QCOMPARE(it->category, QStringLiteral("armour.boots"));
        QCOMPARE(it->mods.size(), 4);
        QCOMPARE(it->mods[0].modType, QStringLiteral("implicit"));
        const ParsedMod* ms = findMod(*it, QStringLiteral("stat_2250533757"));
        QVERIFY(ms);
        QCOMPARE(ms->tier, 1);
        const ParsedMod* life = findMod(*it, QStringLiteral("stat_3299347043"));
        QVERIFY(life);
        QCOMPARE(life->tier, 5);
        QCOMPARE(*life->min, 64.0);
        QVERIFY(life->text.startsWith(QStringLiteral("T5 ")));
    }

    void poe2RareArmour()
    {
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        ItemParser p(kw, dbs, &checked);
        const QString txt = fixture(QStringLiteral("poe2_en_rare_armour.txt"));
        QCOMPARE(p.guessGame(txt, Game::Poe1), Game::Poe2);
        auto it = parse(QStringLiteral("poe2_en_rare_armour.txt"), Game::Poe2);
        QVERIFY(it);
        QCOMPARE(it->category, QStringLiteral("armour.chest"));
        QCOMPARE(it->type, QStringLiteral("Conqueror Plate"));
        QCOMPARE(it->sockets, 2);
        QCOMPARE(it->links, -1);
        QVERIFY(it->corrupted);
        QCOMPARE(it->mods.size(), 4);
        QCOMPARE(it->mods[0].modType, QStringLiteral("rune"));
        bool hasRune = false;
        for (const auto& c : it->mods[0].cands)
            hasRune = hasRune || (c.group == QStringLiteral("rune") && c.id == QStringLiteral("rune.stat_4220027924"));
        QVERIFY(hasRune);
        // pseudo 우선 옵션 끄면 rune 그룹이 선택돼야 함
        ParseOptions noPseudo;
        noPseudo.autoSelectPseudo = false;
        auto it2 = parse(QStringLiteral("poe2_en_rare_armour.txt"), Game::Poe2, noPseudo);
        QVERIFY(it2);
        QCOMPARE(it2->mods[0].current()->group, QStringLiteral("rune"));
        const ParsedMod* armour = nullptr;
        for (const auto& m : it->mods) {
            if (m.modType.isEmpty() && m.statKey == QStringLiteral("stat_1062208444"))
                armour = &m;
        }
        QVERIFY2(armour, "갑옷의 #% increased Armour 는 로컬(stat_1062208444)");

        SearchRequest r;
        r.game = Game::Poe2;
        r.type = it->type;
        r.useQuality = true;
        r.qualityMin = 20;
        r.useSockets = true;
        r.socketsMin = 2;
        const QJsonObject q = QueryBuilder::build(r, &dbs[1][Lang::En]).value(QStringLiteral("query")).toObject();
        const QJsonObject f = q.value(QStringLiteral("filters")).toObject();
        QVERIFY(f.contains(QStringLiteral("type_filters")));
        QVERIFY(f.value(QStringLiteral("equipment_filters")).toObject().value(QStringLiteral("filters")).toObject().contains(QStringLiteral("rune_sockets")));
    }
};

QTEST_GUILESS_MAIN(TestCore)
#include "test_core.moc"
