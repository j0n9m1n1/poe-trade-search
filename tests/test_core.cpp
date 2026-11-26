// SPDX-License-Identifier: GPL-3.0-or-later
//
// 코어 테스트. 파서 테스트는 실제 거래소 데이터가 필요함:
//   PTS_TEST_DATA=<dir>  (안에 cache/poe1/en/stats.json 등)
// 없으면 해당 테스트는 SKIP.
#include "core/CheckedStore.h"
#include "core/Config.h"
#include "core/SearchDefaults.h"
#include "core/ItemParser.h"
#include "core/JsonUtil.h"
#include "core/Keywords.h"
#include "core/PriceEstimate.h"
#include "core/PriceSummary.h"
#include "core/QueryBuilder.h"
#include "core/TextUtil.h"
#include "net/DataManager.h"
#include "net/Endpoints.h"
#include "net/RateLimiter.h"
#include "net/Updater.h"

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

    void updaterVersions()
    {
        QVERIFY(Updater::isNewer(QStringLiteral("0.2.0"), QStringLiteral("v0.2.1")));
        QVERIFY(Updater::isNewer(QStringLiteral("0.2.0-ci.15"), QStringLiteral("v0.3.0")));
        QVERIFY(!Updater::isNewer(QStringLiteral("0.2.0"), QStringLiteral("v0.2.0")));
        QVERIFY(!Updater::isNewer(QStringLiteral("0.2.0-ci.15"), QStringLiteral("v0.2.0")));
        QVERIFY(!Updater::isNewer(QStringLiteral("0.10.0"), QStringLiteral("v0.9.9")));
        QVERIFY(!Updater::isNewer(QStringLiteral("0.2.0"), QStringLiteral("nightly")));

        // CI: 빌드에 박힌 버전이 태그 버전과 같아야 함 (pwsh 인자 쪼개짐 사고 방지)
        const QString expect = qEnvironmentVariable("PTS_EXPECT_VERSION");
        qInfo() << "built version" << Updater::currentVersion();
        if (!expect.isEmpty())
            QCOMPARE(Updater::currentVersion(), expect);
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
        for (double a : {1.0, 1.0, 2.0, 2.0, 2.0, 5.0}) {
            Listing l;
            l.amount = a;
            l.currency = QStringLiteral("chaos");
            v.push_back(l);
        }
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

    void queryPriceCurrency()
    {
        auto priceOf = [](const SearchRequest& r) {
            return QueryBuilder::build(r, nullptr)
                .value(QStringLiteral("query")).toObject()
                .value(QStringLiteral("filters")).toObject()
                .value(QStringLiteral("trade_filters")).toObject()
                .value(QStringLiteral("filters")).toObject()
                .value(QStringLiteral("price")).toObject();
        };
        SearchRequest r;
        r.type = QStringLiteral("Two-Stone Ring");
        QVERIFY(priceOf(r).isEmpty()); // 등가물 = 필터 없음
        r.priceOption = QStringLiteral("chaos_divine");
        QCOMPARE(priceOf(r), QJsonObject({{QStringLiteral("option"), QStringLiteral("chaos_divine")}}));
        r.priceMin = 5;
        QCOMPARE(priceOf(r).value(QStringLiteral("option")).toString(), QStringLiteral("chaos_divine"));
        QCOMPARE(priceOf(r).value(QStringLiteral("min")).toDouble(), 5.0);

        const Config d = Config::defaults();
        QCOMPARE(d.tradeStatus, QStringLiteral("securable"));
        QCOMPARE(d.priceCurrencyFor(Game::Poe1), QStringLiteral("chaos_divine"));
        QCOMPARE(d.priceCurrencyFor(Game::Poe2), QStringLiteral("exalted_divine"));
        for (Game g : {Game::Poe1, Game::Poe2}) {
            bool found = false;
            for (const PriceCurrency& pc : priceCurrencies(g))
                found = found || pc.id == d.priceCurrencyFor(g);
            QVERIFY(found);
        }
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

    void configMigration()
    {
        const QString path = QDir::temp().filePath(QStringLiteral("pts_cfg_test.txt"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("{\"options\":{\"league\":\"Scourge\",\"trade_status\":\"available\"},\"shortcuts\":["
                "{\"keycode\":0,\"modifiers\":0,\"value\":\"{Run}\"},"
                "{\"keycode\":72,\"modifiers\":2,\"value\":\"{Wiki} 위키\"},"
                "{\"keycode\":78,\"modifiers\":2,\"value\":\"{Link}https://poe.ninja/{Link}\"},"
                "{\"keycode\":113,\"modifiers\":0,\"value\":\"{Restart}\"}]}");
        f.close();
        Config c;
        QVERIFY(c.load(path));
        QVERIFY(c.migrated);
        QStringList vals;
        for (const auto& s : c.shortcuts)
            vals << s.value;
        QVERIFY(!vals.join(QLatin1Char('|')).contains(QStringLiteral("{Wiki}")));
        QVERIFY(!vals.join(QLatin1Char('|')).contains(QStringLiteral("poe.ninja")));
        QVERIFY(vals.contains(QStringLiteral("{Enter}/hideout{Enter}")));
        QVERIFY(vals.contains(QStringLiteral("{Enter}/exit{Enter}")));
        QVERIFY(vals.join(QLatin1Char('|')).contains(QStringLiteral("{Last}")));
        QVERIFY(c.autoPriceCheck); // v3: 뜨자마자 시세 조회
        QCOMPARE(c.tradeStatus, QStringLiteral("securable")); // v4: 예전 기본값 -> 즉시 구입만
        QCOMPARE(c.priceCurrency, QStringLiteral("chaos_divine"));
        QCOMPARE(c.configVersion, Config::kConfigVersion);
        for (const auto& s : c.shortcuts) {
            if (s.value == QStringLiteral("{Enter}/hideout{Enter}"))
                QCOMPARE(s.keycode, 0); // F2 는 이미 {Restart} 가 써서 비워둠
        }
        QVERIFY(c.save(path));
        Config c2;
        QVERIFY(c2.load(path));
        QVERIFY(!c2.migrated);
        QFile::remove(path);
    }

    void poe1KoreanUniqueClaw()
    {
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        auto it = parse(QStringLiteral("poe1_ko_unique_claw.txt"), Game::Poe1);
        QVERIFY(it);
        QCOMPARE(it->name, QStringLiteral("탐하는 어둠"));
        QCOMPARE(it->type, QStringLiteral("눈 후비개"));
        const ParsedMod* blind = findMod(*it, QStringLiteral("stat_2301191210"));
        QVERIFY2(blind, "명중 시 적 실명 유발 -> 별칭으로 100% 확률 stat");
        QCOMPARE(*blind->min, 100.0);
        const ParsedMod* phys = findMod(*it, QStringLiteral("stat_1940865751"));
        QVERIFY2(phys, "무기의 물리 피해 #~# 추가는 로컬");
        QCOMPARE(*phys->min, 47.5);
        QVERIFY(findMod(*it, QStringLiteral("stat_3653779134"))); // 2줄 옵션
        for (const auto& m : it->mods)
            QVERIFY2(!m.unmatched, qPrintable(m.raw));
        QVERIFY(it->hasDps);
    }

    void poe1EnglishBlindAndUnmatched()
    {
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        auto it = parse(QStringLiteral("poe1_en_blind.txt"), Game::Poe1);
        QVERIFY(it);
        const ParsedMod* blind = findMod(*it, QStringLiteral("stat_2301191210"));
        QVERIFY2(blind, "Blind Enemies on Hit -> #% chance to Blind Enemies on Hit");
        QCOMPARE(*blind->min, 100.0);
        QCOMPARE(it->mods.size(), 3);
        QVERIFY(it->mods[2].unmatched);
        QVERIFY(it->mods[2].lockedOff);

        // 매칭 못 한 줄은 검색 요청에 안 들어감
        const SearchRequest r = makeRequest(*it, defaultFilterState(*it), kw, &dbs[0], RequestContext{});
        QCOMPARE(r.stats.size(), 2);
    }

    void matcherNegateAndRoll()
    {
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        QVERIFY2(dbs[0].matchers[li(Lang::En)].isLoaded(), "matchers.ndjson 필요");
        auto it = parse(QStringLiteral("poe1_en_reduced.txt"), Game::Poe1);
        QVERIFY(it);
        const ParsedMod* attr = findMod(*it, QStringLiteral("stat_3639275092"));
        QVERIFY2(attr, "reduced -> increased 의 음수 (매처 negate)");
        QCOMPARE(*attr->value, -32.0);
        QCOMPARE(attr->better, -1);
        QVERIFY(!attr->min);
        QCOMPARE(*attr->max, -32.0);
        for (const auto& m : it->mods)
            QVERIFY2(!m.unmatched, qPrintable(m.raw));

        auto adv = parse(QStringLiteral("poe1_en_advanced.txt"), Game::Poe1);
        QVERIFY(adv);
        const ParsedMod* life = findMod(*adv, QStringLiteral("stat_3299347043"));
        QVERIFY(life && life->rollPct);
        QVERIFY(qAbs(*life->rollPct - 44.4) < 0.1); // 64 in 60-69
        QCOMPARE(*life->rollLo, 60.0);
        QCOMPARE(*life->rollHi, 69.0);
    }

    void statRanges()
    {
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        ParseOptions o;
        o.statRangePct = 10;
        auto it = parse(QStringLiteral("poe1_en_rare_ring.txt"), Game::Poe1, o);
        QVERIFY(it);
        QCOMPARE(*findMod(*it, QStringLiteral("stat_3299347043"))->min, 37.0); // 41 - floor(4.1)
        QCOMPARE(*findMod(*it, QStringLiteral("stat_3372524247"))->min, 35.0); // 38 - floor(3.8)
        QCOMPARE(*findMod(*it, QStringLiteral("stat_4277795662"))->min, 14.0);

        // 고유: 롤 범위 폭 기준
        ParsedMod m;
        m.value = 55;
        m.rollLo = 40;
        m.rollHi = 60;
        m.rollPct = 75;
        applyStatRange(m, 10, true);
        QCOMPARE(*m.min, 53.0);
        // 최대 롤 고유는 정확값
        m.value = 60;
        m.rollPct = 100;
        applyStatRange(m, 10, true);
        QCOMPARE(*m.min, 60.0);
        // 낮을수록 좋은 것은 max 쪽으로
        ParsedMod r;
        r.value = 40;
        r.better = -1;
        applyStatRange(r, 10, false);
        QVERIFY(!r.min);
        QCOMPARE(*r.max, 44.0);
    }

    void presets()
    {
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        auto it = parse(QStringLiteral("poe1_en_advanced.txt"), Game::Poe1);
        QVERIFY(it);
        ItemFilterState st;
        const ParsedItem ex = applyPreset(*it, Preset::Exact, 10, &st);
        const ParsedMod* ms = findMod(ex, QStringLiteral("stat_2250533757"));
        QVERIFY(ms->checked);                                    // T1
        QCOMPARE(ms->current()->group, QStringLiteral("explicit")); // pseudo 아님
        QVERIFY(!findMod(ex, QStringLiteral("stat_3299347043"))->checked); // T5
        QCOMPARE(*findMod(ex, QStringLiteral("stat_3299347043"))->min, 64.0); // 정확값

        const ParsedItem base = applyPreset(*it, Preset::Base, 10, &st);
        for (const auto& m : base.mods)
            QVERIFY(!m.checked);
        QVERIFY(st.useLevel);
        QCOMPARE(*st.levelMin, 86.0);
        QCOMPARE(st.rarity, QStringLiteral("nonunique"));
    }

    void estimate()
    {
        Rates r;
        r.base = QStringLiteral("chaos");
        r.toBase.insert(QStringLiteral("divine"), 200);
        QVector<Listing> v;
        for (double a : {150.0, 160.0, 170.0, 180.0})
            v.push_back([&] { Listing l; l.amount = a; l.currency = QStringLiteral("chaos"); return l; }());
        for (double a : {1.0, 1.0, 1.0, 1.1, 1.2, 2.0})
            v.push_back([&] { Listing l; l.amount = a; l.currency = QStringLiteral("divine"); return l; }());
        Listing unknown;
        unknown.amount = 3;
        unknown.currency = QStringLiteral("mirror-shard");
        v.push_back(unknown);
        const Estimate e = estimatePrice(v, r);
        QVERIFY(e.ok);
        QCOMPARE(e.used, 10);
        QCOMPARE(e.value, 200.0);

        // 덤핑 매물 제외: 1,1,2 카오스는 무시하고 10 근처
        QVector<Listing> dump;
        for (double a : {1.0, 1.0, 2.0, 5.0, 8.0, 10.0, 10.0, 10.0, 10.0, 10.0, 10.0, 10.0, 10.0, 11.0}) {
            Listing l;
            l.amount = a;
            l.currency = QStringLiteral("chaos");
            dump.push_back(l);
        }
        const Estimate d = estimatePrice(dump, r);
        QCOMPARE(d.value, 10.0);
        QVERIFY(d.lo >= 5.0);
        QCOMPARE(formatValue(e.value, r), QStringLiteral("1 디바인"));
        QCOMPARE(formatValue(150, r), QStringLiteral("150 카오스"));
        QVERIFY(formatEstimate(e, r, 54).startsWith(QStringLiteral("≈ 1 디바인")));
        // 실측 형태: 순서 뒤섞임 + 미끼 매물(1 카오스에 1 디바인)
        QVector<Listing> offers;
        for (double pay : {214.0, 170.0, 1.0, 190.0, 165.0, 170.0, 208.0, 170.0, 188.0, 170.0}) {
            Listing o;
            o.payAmount = pay;
            o.getAmount = 1;
            offers.push_back(o);
        }
        QCOMPARE(medianRatio(offers), 170.0); // 165,170,170,170,170

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
