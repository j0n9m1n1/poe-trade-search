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
#include "net/TradeClient.h"
#include "net/Updater.h"
#include "ui/FilterValueStep.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtTest>
#include <limits>

using namespace pts;

namespace {
QString fixture(const QString& name)
{
    // 저장소는 LF 지만 Windows(core.autocrlf)는 CRLF 로 체크아웃함 -> 테스트의 "\n" 치환이 안 먹지 않게 LF 로 통일
    // (실제 CRLF 클립보드 파싱은 corruptedFoulbornDefaults 에서 따로 확인)
    QFile f(QStringLiteral(PTS_FIXTURE_DIR "/") + name);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll()).remove(QLatin1Char('\r'));
}

const ParsedMod* findMod(const ParsedItem& it, const QString& statKey)
{
    for (const auto& m : it.mods) {
        if (m.statKey == statKey)
            return &m;
    }
    return nullptr;
}

QJsonObject searchFilters(const QJsonObject& query, const char* group)
{
    return query.value(QStringLiteral("filters")).toObject().value(QLatin1String(group)).toObject()
        .value(QStringLiteral("filters")).toObject();
}

QJsonObject activeStat(const QJsonObject& query, const char* id)
{
    const auto groups = query.value(QStringLiteral("stats")).toArray();
    for (const auto& group : groups) {
        for (const auto& value : group.toObject().value(QStringLiteral("filters")).toArray()) {
            const QJsonObject f = value.toObject();
            if (f.value(QStringLiteral("id")).toString() == QLatin1String(id) && !f.value(QStringLiteral("disabled")).toBool())
                return f;
        }
    }
    return {};
}
} // namespace

class TestCore : public QObject {
    Q_OBJECT

private:
    Keywords kw;
    GameDb dbs[2];
    CheckedStore checked;
    bool haveData = false;

    std::optional<ParsedItem> parseText(const QString& text, Game g, ParseOptions opt = {})
    {
        ItemParser p(kw, dbs, &checked);
        QString err;
        auto r = p.parse(text, g, opt, &err);
        if (!r)
            qWarning() << "parse failed" << err;
        return r;
    }

    std::optional<ParsedItem> parse(const QString& fx, Game g, ParseOptions opt = {})
    {
        ItemParser p(kw, dbs, &checked);
        QString err;
        auto r = p.parse(fixture(fx), g, opt, &err);
        if (!r)
            qWarning() << "parse failed" << fx << err;
        return r;
    }

    QJsonObject searchQuery(const ParsedItem& item, const ItemFilterState& state, bool useDb = true)
    {
        RequestContext ctx;
        ctx.server = item.lang;
        ctx.league = item.game == Game::Poe1 ? QStringLiteral("Covens") : QStringLiteral("Standard");
        ctx.forBrowser = false;
        const GameDb& gameDb = dbs[gi(item.game)];
        return QueryBuilder::build(makeRequest(item, state, kw, &gameDb, ctx), useDb ? &gameDb[item.lang] : nullptr)
            .value(QStringLiteral("query")).toObject();
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
    void filterWheelSteps()
    {
        QCOMPARE(filterValueStep(26.0, 40.0, 1, false), 27.0);
        QCOMPARE(filterValueStep(26.0, 40.0, -2, true), 6.0);
        QCOMPARE(filterValueStep(std::nullopt, 40.0, -1, false), 39.0);
        QCOMPARE(filterValueStep(std::nullopt, 1.25, 1, false), 1.35);
        QCOMPARE(filterValueStep(2.0, 1.25, -1, true), 1.0);
        QCOMPARE(filterValueStep(1.25, 0.0, 1, true), 2.25);
        QCOMPARE(filterValueStep(-0.25, std::nullopt, -1, false), -0.35);
        QCOMPARE(filterValueStep(std::nullopt, std::nullopt, 1, false), 1.0);
    }

    void configAutoPriceRefresh()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("Config.txt"));
        QVERIFY(json::writeFile(path, QJsonDocument(QJsonObject{
                                          {QStringLiteral("config_version"), Config::kConfigVersion}}),
                                true, false, nullptr));
        Config config;
        QVERIFY(config.load(path));
        QVERIFY(config.autoPriceRefresh);
        QVERIFY(config.exchangeSearch);
        QVERIFY(!config.migrated);

        config.autoPriceRefresh = false;
        config.exchangeSearch = false;
        QVERIFY(config.save(path));
        Config reloaded;
        QVERIFY(reloaded.load(path));
        QVERIFY(!reloaded.autoPriceRefresh);
        QVERIFY(!reloaded.exchangeSearch);
    }

    // 일괄 교환 시세에서 낼 화폐: 기준 화폐, 기준 화폐 자신이면 신성
    void exchangePayCurrency()
    {
        QCOMPARE(Rates::exchangePayFor(Game::Poe1, QStringLiteral("alch")), QStringLiteral("chaos"));
        QCOMPARE(Rates::exchangePayFor(Game::Poe1, QStringLiteral("screaming-invitation")), QStringLiteral("chaos"));
        QCOMPARE(Rates::exchangePayFor(Game::Poe1, QStringLiteral("chaos")), QStringLiteral("divine"));
        QCOMPARE(Rates::exchangePayFor(Game::Poe2, QStringLiteral("chaos")), QStringLiteral("exalted"));
        QCOMPARE(Rates::exchangePayFor(Game::Poe2, QStringLiteral("exalted")), QStringLiteral("divine"));
    }

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

    // 코드 점검(v0.0.48)에서 찾은 코어 버그 회귀 테스트
    void auditCoreFixes()
    {
        // 끝 콤마 뒤 주석
        QString err;
        const QJsonDocument lj = json::parseLenient("{ \"x\": true, // c\n}", &err);
        QVERIFY2(!lj.isNull(), qPrintable(err));
        QVERIFY(lj.object().value(QStringLiteral("x")).toBool());

        // 희귀도를 꺼도 고유 아이템 이름은 보냄
        SearchRequest u;
        u.game = Game::Poe1;
        u.name = QStringLiteral("Headhunter");
        u.type = QStringLiteral("Leather Belt");
        u.useName = u.useType = true;
        u.uniqueName = true;
        QCOMPARE(QueryBuilder::build(u, nullptr).value(QStringLiteral("query")).toObject()
                     .value(QStringLiteral("name")).toString(), QStringLiteral("Headhunter"));
        u.uniqueName = false;
        QVERIFY(!QueryBuilder::build(u, nullptr).value(QStringLiteral("query")).toObject().contains(QStringLiteral("name")));

        // 베이스 기준 레벨: 부분 문자열(Silk Gloves ⊂ Fingerless Silk Gloves)로 맞추지 않음
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString cp = tmp.filePath(QStringLiteral("checked.json"));
        {
            QFile f(cp);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("{\"bases\":[{\"id\":\"all\",\"key\":\"Fingerless Silk Gloves(84)/Silk Slippers(80)/\"}]}");
        }
        CheckedStore cs;
        QVERIFY(cs.load(cp));
        QCOMPARE(cs.baseMinLevel(QStringLiteral("Silk Gloves"), {}), -1);
        QCOMPARE(cs.baseMinLevel(QStringLiteral("Fingerless Silk Gloves"), {}), 84);
        QCOMPARE(cs.baseMinLevel(QStringLiteral("Silk Slippers"), {}), 80);

        // 한국어 경로석은 PoE2
        const QString way = QStringLiteral("아이템 종류: 경로석\n아이템 희귀도: 일반\n경로석 (15등급)\n--------\n경로석 등급: 15\n--------\n아이템 레벨: 79\n--------\n");
        ItemParser gp(kw, dbs, &checked);
        QCOMPARE(gp.guessGame(way, Game::Poe1), Game::Poe2);

        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        // 분류 검색(이름·베이스 안 보냄)이면 번역 실패로 서버를 바꾸지 않음
        ParsedItem it;
        it.game = Game::Poe1;
        it.lang = Lang::Ko;
        it.rarityId = QStringLiteral("rare");
        it.type = QStringLiteral("없는 베이스");
        it.category = QStringLiteral("armour.gloves");
        it.isGear = true;
        ItemFilterState st = defaultFilterState(it);
        st.byCategory = true;
        RequestContext ctx;
        ctx.server = Lang::En;
        QCOMPARE(makeRequest(it, st, kw, &dbs[0], ctx).lang, Lang::En);
        st.byCategory = false;
        st.useType = true;
        QCOMPARE(makeRequest(it, st, kw, &dbs[0], ctx).lang, Lang::Ko); // 베이스를 보내야 하면 그대로 아이템 언어로

        // 정확 프리셋에서도 분열 속성 없음 줄을 끄면 분열 제외 안 넣음
        auto gl = parse(QStringLiteral("poe1_ko_rare_gloves_q30.txt"), Game::Poe1);
        QVERIFY(gl);
        ItemFilterState es;
        const ParsedItem ex = applyPreset(*gl, Preset::Exact, 0, &es);
        QVERIFY(es.exact);
        ctx.server = Lang::Ko;
        QVERIFY(makeRequest(ex, es, kw, &dbs[0], ctx).notFractured);
        es.notFractured = false;
        QVERIFY(!makeRequest(ex, es, kw, &dbs[0], ctx).notFractured);

        // 경로석: 아이템 레벨 줄이 하나만
        auto w = parseText(way, Game::Poe2);
        QVERIFY(w);
        QVERIFY(w->isWaystone);
        int ilvlRows = 0;
        for (const auto& pr : w->props)
            ilvlRows += pr.key == QStringLiteral("ilvl");
        QCOMPARE(ilvlRows, 0);
    }

    // 감사(Neon) 지적: 교환 응답 검증, fetch 이름 태그, 브라우저 리다이렉트 인코딩
    void auditNetFixes()
    {
        const auto html = TradeClient::parseExchangeReply(QByteArray("<html>Cloudflare</html>"));
        QVERIFY(!html.error.isEmpty());
        const auto noResult = TradeClient::parseExchangeReply(QByteArray("{\"id\":\"x\"}"));
        QVERIFY(!noResult.error.isEmpty());
        const auto ok = TradeClient::parseExchangeReply(QByteArray("{\"id\":\"abc\",\"result\":{},\"total\":0}"));
        QVERIFY2(ok.error.isEmpty(), qPrintable(ok.error));
        QCOMPARE(ok.id, QStringLiteral("abc"));

        QJsonObject price{{QStringLiteral("amount"), 1}, {QStringLiteral("currency"), QStringLiteral("chaos")}};
        QJsonObject listing{{QStringLiteral("price"), price}};
        QJsonObject item{{QStringLiteral("name"), QStringLiteral("<<set:MS>><<set:M>><<set:S>>공포의 손아귀")},
                         {QStringLiteral("baseType"), QStringLiteral("<<set:MS>>거미줄 장갑")}};
        const QJsonArray arr{QJsonObject{{QStringLiteral("listing"), listing}, {QStringLiteral("item"), item}}};
        const auto ls = TradeClient::parseFetchListings(arr);
        QCOMPARE(ls.size(), 1);
        QCOMPARE(ls[0].itemName, QStringLiteral("공포의 손아귀"));
        QCOMPARE(ls[0].baseType, QStringLiteral("거미줄 장갑"));

        const QUrl u = endpoints::redirectSearch(Game::Poe1, Lang::En, QStringLiteral("Standard"),
                                                 QByteArray("{\"a\":\"1+1 50%\"}"));
        const QString q = u.toString(QUrl::FullyEncoded);
        QVERIFY2(q.contains(QStringLiteral("%2B")), qPrintable(q));
        QVERIFY2(q.contains(QStringLiteral("%25")), qPrintable(q));
        QVERIFY2(!q.contains(QLatin1Char('+')), qPrintable(q));
        QCOMPARE(QUrlQuery(u).queryItemValue(QStringLiteral("q"), QUrl::FullyDecoded),
                 QStringLiteral("{\"a\":\"1+1 50%\"}"));
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
        QVERIFY(Updater::isNewer(QStringLiteral("0.0.9"), QStringLiteral("v0.0.10"))); // 문자열 비교면 거꾸로 됨
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

    void priceNoteAndDisplayPrice()
    {
        QCOMPARE(PriceSummary::formatAmount(13.0), QStringLiteral("13"));
        QCOMPARE(PriceSummary::formatAmount(1.5), QStringLiteral("1.5"));
        QCOMPARE(PriceSummary::formatAmount(10.04), QStringLiteral("10"));
        QCOMPARE(PriceSummary::formatAmount(0.25), QStringLiteral("0.25"));

        QCOMPARE(PriceSummary::priceNote(13, QStringLiteral("divine")), QStringLiteral("~b/o 13 divine"));
        QCOMPARE(PriceSummary::priceNote(1.5, QStringLiteral("chaos")), QStringLiteral("~b/o 1.5 chaos"));
        QCOMPARE(PriceSummary::priceNote(10.04, QStringLiteral("chaos")), QStringLiteral("~b/o 10.04 chaos"));
        QCOMPARE(PriceSummary::priceNote(12.25, QStringLiteral("divine")), QStringLiteral("~b/o 12.25 divine")); // 반올림 금지
        QCOMPARE(PriceSummary::priceNote(0.25, QStringLiteral("exalted")), QStringLiteral("~b/o 0.25 exalted"));
        QCOMPARE(PriceSummary::priceNote(12, QStringLiteral(" divine ")), QStringLiteral("~b/o 12 divine"));
        QVERIFY(PriceSummary::priceNote(0, QStringLiteral("chaos")).isEmpty());
        QVERIFY(PriceSummary::priceNote(-3, QStringLiteral("chaos")).isEmpty());
        QVERIFY(PriceSummary::priceNote(5, QString()).isEmpty());
        QVERIFY(PriceSummary::priceNote(5, QStringLiteral("   ")).isEmpty());
        QVERIFY(PriceSummary::priceNote(std::numeric_limits<double>::quiet_NaN(), QStringLiteral("chaos")).isEmpty());

        Rates r;
        r.base = QStringLiteral("chaos");
        r.toBase.insert(QStringLiteral("divine"), 200);
        const PriceTag pDiv = displayPrice(260, r);
        QVERIFY(pDiv.valid());
        QCOMPARE(pDiv.currency, QStringLiteral("divine"));
        QCOMPARE(PriceSummary::priceNote(pDiv.amount, pDiv.currency), QStringLiteral("~b/o 1.3 divine"));
        const PriceTag pChaos = displayPrice(34.6, r);
        QCOMPARE(PriceSummary::priceNote(pChaos.amount, pChaos.currency), QStringLiteral("~b/o 35 chaos"));
        const PriceTag pSmall = displayPrice(3.46, r);
        QCOMPARE(PriceSummary::priceNote(pSmall.amount, pSmall.currency), QStringLiteral("~b/o 3.5 chaos"));
        QVERIFY(!displayPrice(0, r).valid());
        QVERIFY(!displayPrice(10, Rates{}).valid());

        Rates poe2;
        poe2.base = QStringLiteral("exalted");
        poe2.toBase.insert(QStringLiteral("divine"), 150);
        const PriceTag pEx = displayPrice(45, poe2);
        QCOMPARE(PriceSummary::priceNote(pEx.amount, pEx.currency), QStringLiteral("~b/o 45 exalted"));
    }

    void listingFetchAndTooltip()
    {
        const QByteArray raw = R"JSON([
            {
                "listing": {
                    "indexed": "2026-11-26T10:00:00Z",
                    "account": { "name": "sellerA", "online": { "status": "afk" } },
                    "price": { "amount": 13, "currency": "divine" }
                },
                "item": {
                    "name": "<<set:MS>>공포의 손아귀",
                    "typeLine": "거미줄 장갑",
                    "baseType": "거미줄 장갑",
                    "frameType": 2,
                    "ilvl": 85,
                    "corrupted": true,
                    "fractured": true,
                    "properties": [
                        { "name": "퀄리티", "type": 6, "values": [["+30%", 1]] }
                    ],
                    "enchantMods": ["명중 시 칙령 발동"],
                    "implicitMods": ["총주교 고정 옵션"],
                    "fracturedMods": ["생명력 최대치 +119"],
                    "explicitMods": ["냉기 저항 +42%", "공격 속도 14% 증가"],
                    "craftedMods": ["화염 및 번개 저항 +15%"]
                }
            },
            {
                "listing": {
                    "indexed": "2026-11-26T11:00:00Z",
                    "account": { "name": "sellerB" },
                    "price": { "amount": 1.5, "currency": "chaos" }
                },
                "item": {
                    "typeLine": "Two-Stone Ring",
                    "baseType": "Two-Stone Ring",
                    "frameType": 1,
                    "ilvl": 78,
                    "duplicated": true,
                    "identified": false,
                    "explicitMods": ["+10 <Test> & Fire"]
                }
            }
        ])JSON";
        const QJsonArray arr = QJsonDocument::fromJson(raw).array();
        const QVector<Listing> ls = TradeClient::parseFetchListings(arr);
        QCOMPARE(ls.size(), 2);

        const Listing& a = ls[0];
        QCOMPARE(a.amount, 13.0);
        QCOMPARE(a.currency, QStringLiteral("divine"));
        QCOMPARE(a.ilvl, 85);
        QCOMPARE(a.quality, 30);
        QCOMPARE(a.online, 2);
        QCOMPARE(a.frameType, 2);
        QVERIFY(a.corrupted && a.fractured && a.identified);
        QCOMPARE(a.enchantMods, QStringList{QStringLiteral("명중 시 칙령 발동")});
        QCOMPARE(a.implicitMods, QStringList{QStringLiteral("총주교 고정 옵션")});
        QCOMPARE(a.fracturedMods, QStringList{QStringLiteral("생명력 최대치 +119")});
        QCOMPARE(a.explicitMods.size(), 2);
        QCOMPARE(a.craftedMods, QStringList{QStringLiteral("화염 및 번개 저항 +15%")});

        const QString tipA = PriceSummary::listingTooltip(a);
        QVERIFY(tipA.startsWith(QStringLiteral("<div>")));
        QVERIFY(!tipA.contains(QStringLiteral("<<set:")));
        QVERIFY(tipA.contains(QStringLiteral("공포의 손아귀")));
        QVERIFY(tipA.contains(QStringLiteral("거미줄 장갑")));
        QVERIFY(tipA.contains(QStringLiteral("아이템 레벨: 85")));
        QVERIFY(tipA.contains(QStringLiteral("퀄리티: +30%")));
        QVERIFY(tipA.contains(QStringLiteral("타락")));
        QVERIFY(tipA.contains(QStringLiteral("분열")));
        QVERIFY(tipA.contains(QStringLiteral("#a29162"))); // 분열 색
        QVERIFY(tipA.contains(QStringLiteral("#b4b4ff"))); // 제작/인챈트 색
        QVERIFY(tipA.contains(QStringLiteral("#8888ff"))); // 일반/고정 색
        QVERIFY(tipA.contains(QStringLiteral("생명력 최대치 +119")));
        QVERIFY(tipA.contains(QStringLiteral("화염 및 번개 저항 +15%")));

        const Listing& b = ls[1];
        QVERIFY(b.mirrored && !b.identified);
        const QString tipB = PriceSummary::listingTooltip(b);
        QVERIFY(tipB.contains(QStringLiteral("복제")));
        QVERIFY(tipB.contains(QStringLiteral("미확인")));
        QVERIFY(tipB.contains(QStringLiteral("&lt;Test&gt; &amp; Fire")));

        QVERIFY(PriceSummary::listingTooltip(Listing{}).isEmpty());
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

    void queryCorruptMutated()
    {
        auto misc = [](const SearchRequest& r) {
            return QueryBuilder::build(r, nullptr)
                .value(QStringLiteral("query")).toObject()
                .value(QStringLiteral("filters")).toObject()
                .value(QStringLiteral("misc_filters")).toObject()
                .value(QStringLiteral("filters")).toObject();
        };
        auto opt = [](const QJsonObject& o, const char* k) {
            return o.value(QLatin1String(k)).toObject().value(QStringLiteral("option")).toString();
        };
        SearchRequest r;
        r.type = QStringLiteral("Eternal Sword");
        r.corrupted = 1;
        r.twiceCorrupted = 1;
        r.mutated = 2;
        QJsonObject m = misc(r);
        QCOMPARE(opt(m, "corrupted"), QStringLiteral("true"));
        QCOMPARE(opt(m, "twice_corrupted"), QStringLiteral("true"));
        QCOMPARE(opt(m, "mutated"), QStringLiteral("false"));
        r.game = Game::Poe2;
        r.twiceCorrupted = 2;
        r.mutated = 0;
        m = misc(r);
        QCOMPARE(opt(m, "twice_corrupted"), QStringLiteral("false"));
        QVERIFY(!m.contains(QStringLiteral("mutated")));

        // 등록 기간
        QCOMPARE(QueryBuilder::indexedFromDays(0), QString());
        QCOMPARE(QueryBuilder::indexedFromDays(7), QStringLiteral("1week"));
        QCOMPARE(QueryBuilder::indexedFromDays(30), QStringLiteral("1month"));
        for (int d : {0, 1, 3, 7, 14, 30, 60}) {
            bool found = false;
            for (const TradeChoice& c : indexedChoices())
                found = found || c.id == QueryBuilder::indexedFromDays(d);
            QVERIFY(found);
        }
    }

    void corruptedFoulbornDefaults()
    {
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        const QString ring = fixture(QStringLiteral("poe1_en_rare_ring.txt"));
        QVERIFY(ring.contains(QStringLiteral("\nCorrupted")));
        auto once = parseText(ring, Game::Poe1);
        QVERIFY(once);
        QVERIFY(once->corrupted && !once->twiceCorrupted);
        QCOMPARE(once->corruptDefault, 1);
        QCOMPARE(once->twiceCorruptDefault, 2); // 한 번 타락 -> 두 번 타락 제외
        QCOMPARE(once->mutatedDefault, 0);      // 고유 아님
        // 게임 클립보드는 CRLF: 줄 끝만 다르고 결과는 같아야 함
        auto crlf = parseText(QString(ring).replace(QLatin1Char('\n'), QStringLiteral("\r\n")), Game::Poe1);
        QVERIFY(crlf);
        QCOMPARE(crlf->type, once->type);
        QCOMPARE(crlf->mods.size(), once->mods.size());
        QVERIFY(crlf->corrupted && !crlf->twiceCorrupted);
        QCOMPARE(crlf->corruptDefault, 1);

        QString t = ring;
        t.replace(QStringLiteral("\nCorrupted"), QStringLiteral("\nTwice Corrupted"));
        auto twice = parseText(t, Game::Poe1);
        QVERIFY(twice);
        QVERIFY(twice->corrupted && twice->twiceCorrupted);
        QCOMPARE(twice->twiceCorruptDefault, 1);

        t = ring;
        t.remove(QStringLiteral("\n--------\nCorrupted"));
        auto clean = parseText(t, Game::Poe1);
        QVERIFY(clean);
        QVERIFY(!clean->corrupted);
        QCOMPARE(clean->corruptDefault, 2); // 타락 아님 -> 타락 제외
        QCOMPARE(clean->twiceCorruptDefault, 0);

        const QString uni = fixture(QStringLiteral("poe1_en_unique.txt"));
        auto u = parseText(uni, Game::Poe1);
        QVERIFY(u);
        QVERIFY(!u->foulborn);
        QCOMPARE(u->mutatedDefault, 2);
        const QString name = u->rawName;
        QVERIFY(!name.isEmpty());
        t = uni;
        t.replace(name, QStringLiteral("Foulborn ") + name);
        auto fb = parseText(t, Game::Poe1);
        QVERIFY(fb);
        QVERIFY(fb->foulborn);
        QCOMPARE(fb->name, name);
        QCOMPARE(fb->mutatedDefault, 1);
    }

    void queryUseNameType()
    {
        SearchRequest r;
        r.rarity = QStringLiteral("unique");
        r.name = QStringLiteral("꿈의 깃털");
        r.type = QStringLiteral("영원의 검");
        r.category = QStringLiteral("weapon.onesword");
        auto q = [&]() { return QueryBuilder::build(r, nullptr).value(QStringLiteral("query")).toObject(); };
        auto cat = [&]() {
            return q().value(QStringLiteral("filters")).toObject().value(QStringLiteral("type_filters")).toObject()
                .value(QStringLiteral("filters")).toObject().value(QStringLiteral("category")).toObject()
                .value(QStringLiteral("option")).toString();
        };
        QCOMPARE(q().value(QStringLiteral("name")).toString(), r.name);
        QCOMPARE(q().value(QStringLiteral("type")).toString(), r.type);
        QVERIFY(cat().isEmpty());
        r.useType = false; // 베이스 끄면 유형으로
        QVERIFY(!q().contains(QStringLiteral("type")));
        QCOMPARE(cat(), r.category);
        QCOMPARE(q().value(QStringLiteral("name")).toString(), r.name);
        r.useName = false;
        QVERIFY(!q().contains(QStringLiteral("name")));
        r.category.clear(); // 유형 모르면 베이스 유지
        QCOMPARE(q().value(QStringLiteral("type")).toString(), r.type);
    }

    void poe1KoreanTwiceCorruptedWeapon()
    {
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        auto it = parse(QStringLiteral("poe1_ko_unique_twice_corrupted.txt"), Game::Poe1);
        QVERIFY(it);
        QCOMPARE(it->name, QStringLiteral("꿈의 깃털"));
        QVERIFY(it->corrupted && it->twiceCorrupted);
        QCOMPARE(it->twiceCorruptDefault, 1);
        QCOMPARE(it->mutatedDefault, 2);
        // 무기에 붙은 회피는 전역 stat (로컬 회피는 방어구 전용) -> 로컬로 잡으면 매물 0개
        QVERIFY2(findMod(*it, QStringLiteral("stat_2144192055")), "회피 +# 전역");
        QVERIFY(!findMod(*it, QStringLiteral("stat_53045048")));
        // 무기 로컬은 그대로
        QVERIFY(findMod(*it, QStringLiteral("stat_210067635"))); // 공격 속도 (특정)
        QVERIFY(findMod(*it, QStringLiteral("stat_691932474"))); // 정확도 (특정)
        QVERIFY(findMod(*it, QStringLiteral("stat_1940865751"))); // 물리 피해 추가 (특정)
    }

    void itemPropsAndAffix()
    {
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        auto propOf = [](const ParsedItem& it, const char* k) {
            for (const ItemProp& p : it.props) {
                if (p.key == QLatin1String(k))
                    return p.value;
            }
            return -1.0;
        };
        // 퀄 20: 표시값 그대로
        const QString gull = fixture(QStringLiteral("poe1_ko_unique_gull.txt"));
        auto g = parseText(gull, Game::Poe1);
        QVERIFY(g);
        QCOMPARE(propOf(*g, "ev"), 301.0);
        QCOMPARE(propOf(*g, "es"), 150.0);
        QCOMPARE(propOf(*g, "ar"), -1.0);
        // "적 처치 시 1레벨 하위 성소 창조 발동 — 변경이 불가능한 값" 도 매칭
        QVERIFY(findMod(*g, QStringLiteral("stat_1010340836")));
        // 품질은 로컬 141% 증가와 별도로 곱연산.
        QString q10 = gull;
        q10.replace(QStringLiteral("퀄리티: +20%"), QStringLiteral("퀄리티: +10%"));
        auto g10 = parseText(q10, Game::Poe1);
        QVERIFY(g10);
        QVERIFY(qAbs(propOf(*g10, "ev") - 301.0 * 120.0 / 110.0) < 0.01);
        QVERIFY(qAbs(propOf(*g10, "es") - 150.0 * 120.0 / 110.0) < 0.01);
        // 쿼리: 켠 속성만 armour_filters 로
        ItemFilterState st = defaultFilterState(*g);
        QCOMPARE(st.props.size(), 2);
        st.props[0].use = true;
        RequestContext ctx;
        ctx.server = Lang::Ko;
        const SearchRequest r = makeRequest(*g, st, kw, &dbs[0], ctx);
        const QJsonObject af = QueryBuilder::build(r, nullptr).value(QStringLiteral("query")).toObject()
                                   .value(QStringLiteral("filters")).toObject()
                                   .value(QStringLiteral("armour_filters")).toObject()
                                   .value(QStringLiteral("filters")).toObject();
        QCOMPARE(af.value(QStringLiteral("ev")).toObject().value(QStringLiteral("min")).toDouble(), 301.0);
        QVERIFY(!af.contains(QStringLiteral("es")));

        // 고급 설명: 접두/접미 + 화면용 문구(수치 포함, 범위 제거)
        auto adv = parse(QStringLiteral("poe1_en_advanced.txt"), Game::Poe1);
        QVERIFY(adv);
        int pre = 0, suf = 0;
        for (const ParsedMod& m : adv->mods) {
            pre += m.affix == QStringLiteral("prefix");
            suf += m.affix == QStringLiteral("suffix");
            if (!m.unmatched) {
                QVERIFY2(!m.shown.isEmpty() && !m.shown.contains(QLatin1Char('#')), qPrintable(m.shown));
                QVERIFY2(!m.shown.contains(QLatin1Char('(')), qPrintable(m.shown)); // "35(30-35)" 범위 제거
            }
        }
        QVERIFY(pre >= 2);
        QVERIFY(suf >= 1);
    }

    void listedDefaultAndExclusions()
    {
        // 등록일 자동: 즉시 구입만 -> 언제든, 직접 거래 포함 -> 3일, 직접 지정은 그대로
        QCOMPARE(QueryBuilder::indexedDefault(-1, QStringLiteral("securable")), QString());
        QCOMPARE(QueryBuilder::indexedDefault(-1, QStringLiteral("available")), QStringLiteral("3days"));
        QCOMPARE(QueryBuilder::indexedDefault(7, QStringLiteral("securable")), QStringLiteral("1week"));
        QCOMPARE(Config::defaults().searchBeforeDay, -1);

        // 기본 제외 필터
        SearchRequest r;
        r.type = QStringLiteral("Two-Stone Ring");
        r.mirrored = 2;
        r.notSplit = r.notFractured = true;
        const QJsonObject mf = QueryBuilder::build(r, nullptr).value(QStringLiteral("query")).toObject()
                                   .value(QStringLiteral("filters")).toObject()
                                   .value(QStringLiteral("misc_filters")).toObject()
                                   .value(QStringLiteral("filters")).toObject();
        for (const char* k : {"mirrored", "split", "fractured_item"})
            QCOMPARE(mf.value(QLatin1String(k)).toObject().value(QStringLiteral("option")).toString(), QStringLiteral("false"));
        r.game = Game::Poe2; // PoE2 엔 분할 없음
        QVERIFY(!QueryBuilder::build(r, nullptr).value(QStringLiteral("query")).toObject()
                     .value(QStringLiteral("filters")).toObject().value(QStringLiteral("misc_filters")).toObject()
                     .value(QStringLiteral("filters")).toObject().contains(QStringLiteral("split")));

        // 설정 v5 마이그레이션: 예전 기본값 7일 -> 자동, 직접 고른 3일은 그대로
        const QString path = QDir::temp().filePath(QStringLiteral("pts_cfg_v5.txt"));
        for (int days : {7, 3}) {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(QStringLiteral("{\"config_version\":4,\"options\":{\"search_before_day\":%1}}").arg(days).toUtf8());
            f.close();
            Config c;
            QVERIFY(c.load(path));
            QCOMPARE(c.searchBeforeDay, days == 7 ? -1 : 3);
        }
        QFile::remove(path);
    }

    void aptRules()
    {
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        auto propUse = [](const ParsedItem& it, const char* k) {
            for (const ItemProp& p : it.props) {
                if (p.key == QLatin1String(k))
                    return p.use ? 1 : 0;
            }
            return -1;
        };
        // 분열 옵션: 분열 id 그대로 + 분열된 아이템이면 기본으로 켬 (일반 id 로 찾으면 분열 안 된 매물까지 섞임)
        const QString gl = fixture(QStringLiteral("poe1_ko_rare_gloves_synthetic.txt"));
        auto g = parseText(gl, Game::Poe1);
        QVERIFY(g);
        const ParsedMod* fr = nullptr;
        for (const ParsedMod& m : g->mods) {
            if (m.modType == QStringLiteral("fractured"))
                fr = &m;
        }
        QVERIFY(fr && fr->current());
        QVERIFY2(fr->current()->id.startsWith(QStringLiteral("fractured.")), qPrintable(fr->current()->id));
        QVERIFY(fr->checked);
        ItemFilterState st;
        const ParsedItem ex = applyPreset(*g, Preset::Exact, 0, &st);
        for (const ParsedMod& m : ex.mods) {
            if (m.modType == QStringLiteral("fractured"))
                QVERIFY2(m.current()->id.startsWith(QStringLiteral("fractured.")), qPrintable(m.current()->id));
        }
        QVERIFY(st.exact);
        RequestContext ctx;
        ctx.server = Lang::Ko;
        ctx.league = QStringLiteral("Covens");
        SearchRequest req = makeRequest(ex, st, kw, &dbs[0], ctx);
        QVERIFY(req.mirrored == 2 && req.notSplit);
        QVERIFY(g->fractured);
        QVERIFY(!req.notFractured); // 분열된 아이템이면 분열 제외 필터 안 넣음
        ctx.league = QStringLiteral("Standard");
        req = makeRequest(*g, defaultFilterState(*g), kw, &dbs[0], ctx);
        QVERIFY(req.mirrored == 2 && !req.notSplit); // 영구 리그는 분할 매물 허용

        // 방어 종류가 둘(회피+에보)이면 속성 자동 체크 안 함
        QCOMPARE(propUse(*g, "ev"), 0);
        QCOMPARE(propUse(*g, "es"), 0);
        // 실제 분열 갑옷 (마법, 분열 "방어도 및 회피 % 증가"): 분열 id + 기본 켬
        auto fa = parse(QStringLiteral("poe1_ko_magic_armour_fractured.txt"), Game::Poe1);
        QVERIFY(fa && fa->fractured);
        const ParsedMod* fam = findMod(*fa, QStringLiteral("stat_2451402625"));
        QVERIFY(fam && fam->checked);
        QCOMPARE(fam->current()->id, QStringLiteral("fractured.stat_2451402625"));

        // 유사(pseudo) 후보가 있는 분열 옵션도 분열 그룹으로 (심연 주얼 "에너지 보호막 최대치 +31")
        auto aj = parse(QStringLiteral("poe1_ko_abyss_jewel_fractured.txt"), Game::Poe1);
        QVERIFY(aj && aj->fractured);
        // 주얼도 타락 아닌 아이템이면 타락 매물 제외 (빠져 있어서 타락된 싼 매물이 시세를 끌어내렸음)
        QCOMPARE(aj->corruptDefault, 2);
        QCOMPARE(defaultFilterState(*aj).corrupted, 2);
        const ParsedMod* ajm = findMod(*aj, QStringLiteral("stat_3489782002"));
        if (!ajm) {
            for (const ParsedMod& m : aj->mods) {
                if (m.modType == QStringLiteral("fractured"))
                    ajm = &m;
            }
        }
        QVERIFY(ajm && ajm->current());
        QVERIFY2(ajm->current()->id.startsWith(QStringLiteral("fractured.")), qPrintable(ajm->current()->id));
        QVERIFY(ajm->checked);
        const ParsedItem ajEx = applyPreset(*aj, Preset::Default, 0, nullptr); // 프리셋 다시 적용해도 그대로
        for (const ParsedMod& m : ajEx.mods) {
            if (m.modType == QStringLiteral("fractured"))
                QVERIFY2(m.current()->id.startsWith(QStringLiteral("fractured.")), qPrintable(m.current()->id));
        }

        // 하나뿐이어도 Q20 방어 수치는 켜지 않고, 로컬 방어 옵션은 다른 옵션처럼 그대로 둠
        QString evOnly = gl;
        evOnly.remove(QStringLiteral("에너지 보호막: 25 (augmented)\n"));
        evOnly.replace(QStringLiteral("생명력 최대치 +119(100-119)"), QStringLiteral("회피 +40(30-50)"));
        auto e = parseText(evOnly, Game::Poe1);
        QVERIFY(e);
        QCOMPARE(propUse(*e, "ev"), 0);
        const ParsedMod* evLocal = nullptr;
        for (const ParsedMod& m : e->mods) {
            if (m.statKey == QStringLiteral("stat_53045048"))
                evLocal = &m;
        }
        QVERIFY(evLocal); // 분열 아이템이라 기본 체크는 분열 옵션뿐, 속성 때문에 따로 끄지도 않음

        // 클러스터 주얼: 패시브 8개 -> 최대 8 (적을수록 노트 비중이 큼), 주얼 홈 수 해제, ilvl 84 구간
        auto cj = parse(QStringLiteral("poe1_en_cluster.txt"), Game::Poe1);
        QVERIFY(cj);
        const ParsedMod* passives = findMod(*cj, QStringLiteral("stat_3086156145"));
        QVERIFY(passives);
        QVERIFY(passives->checked && passives->rangeLocked);
        QVERIFY(!passives->min);
        QCOMPARE(*passives->max, 8.0);
        const ParsedMod* sockets = findMod(*cj, QStringLiteral("stat_4079888060"));
        QVERIFY(sockets && !sockets->checked);
        const ItemFilterState cs = defaultFilterState(*cj);
        QVERIFY(cs.useLevel);
        QCOMPARE(*cs.levelMin, 84.0);
        QVERIFY(!cs.levelMax);
        // 범위 완화(-10%)가 덮어쓰지 않음
        const ParsedItem cj10 = applyPreset(*cj, Preset::Default, 10, nullptr);
        QCOMPARE(*findMod(cj10, QStringLiteral("stat_3086156145"))->max, 8.0);

        // 젬: 변형 젬은 품질 20 이상일 때만 품질 체크
        auto gem = parse(QStringLiteral("poe1_en_gem_transfigured.txt"), Game::Poe1);
        QVERIFY(gem);
        QVERIFY(gem->checkQuality); // 20%

        // 고유: 범위 없는 고정 롤은 체크 안 함 (갈매기 "1레벨 하위 성소 창조")
        auto gull = parse(QStringLiteral("poe1_ko_unique_gull.txt"), Game::Poe1);
        QVERIFY(gull);
        const ParsedMod* shrine = findMod(*gull, QStringLiteral("stat_1010340836"));
        QVERIFY(shrine && !shrine->checked);
        QVERIFY(findMod(*gull, QStringLiteral("stat_1999113824"))->checked); // 범위 있는 건 체크
    }

    void emptyAffixes_data()
    {
        QTest::addColumn<QString>("itemText");
        QTest::addColumn<bool>("poe2");
        QTest::addColumn<int>("prefixes");
        QTest::addColumn<int>("suffixes");

        const QString armour = fixture(QStringLiteral("poe1_ko_magic_armour_fractured.txt"));
        const QString boots = fixture(QStringLiteral("poe1_en_advanced.txt"));
        QTest::newRow("rare-ko") << fixture(QStringLiteral("poe1_ko_rare_gloves_q30.txt")) << false << 1 << 0;
        QTest::newRow("magic-ko-full") << armour << false << 0 << 0;
        QTest::newRow("magic-ko-open-suffix") << QString(armour).remove(QRegularExpression(
            QStringLiteral("\\{ 접미어[^\\n]*\\n[^\\n]*\\n"))) << false << 0 << 1;
        QTest::newRow("rare-en") << boots << false << 1 << 2;
        QTest::newRow("rare-en-hybrid-unmatched") << QString(boots).replace(
            QStringLiteral("+64(60-69) to maximum Life"), QStringLiteral("Unknown modifier line\nSecond modifier line"))
                                                << false << 1 << 2;
        QTest::newRow("crafted-ko") << fixture(QStringLiteral("poe1_ko_rare_gloves_synthetic.txt")) << false << 0 << 1;
        QTest::newRow("abyss-jewel-ko") << fixture(QStringLiteral("poe1_ko_abyss_jewel_fractured.txt")) << false << 0 << 0;
        QTest::newRow("helical-ring-ko") << QStringLiteral(
            "아이템 종류: 반지\n아이템 희귀도: 희귀\n시험 반지\n나선형 반지\n--------\n아이템 레벨: 85\n--------\n"
            "{ 고정 속성 부여 }\n접두어 속성 부여의 최대 개수 -2개 (implicit)\n접미어 속성 부여의 최대 개수 +1개 (implicit)\n"
            "--------\n{ 접두어 속성 부여 }\n생명력 최대치 +40\n") << false << 0 << 4;
        QTest::newRow("map-ko") << fixture(QStringLiteral("poe1_ko_rare_map_t16.txt")) << false << -1 << -1;
        QTest::newRow("basic-en") << QString(boots).remove(QRegularExpression(QStringLiteral("\\{[^\\n]*\\n")))
                                 << false << -1 << -1;
        QTest::newRow("corrupted-en") << boots + QStringLiteral("\n--------\nCorrupted") << false << -1 << -1;
        QTest::newRow("unidentified-en") << boots + QStringLiteral("\n--------\nUnidentified") << false << -1 << -1;
        QTest::newRow("mirrored-ko") << fixture(QStringLiteral("poe1_ko_rare_ring_mirrored.txt")) << false << -1 << -1;

        QString poe2Armour = fixture(QStringLiteral("poe2_en_rare_armour.txt"));
        poe2Armour.remove(QStringLiteral("--------\nCorrupted"));
        poe2Armour.replace(QStringLiteral("+85 to maximum Life"), QStringLiteral("{ Prefix Modifier }\n+85 to maximum Life"));
        poe2Armour.replace(QStringLiteral("+35% to Fire Resistance"), QStringLiteral("{ Suffix Modifier }\n+35% to Fire Resistance"));
        poe2Armour.replace(QStringLiteral("42% increased Armour"), QStringLiteral("{ Prefix Modifier }\n42% increased Armour"));
        QTest::newRow("poe2-rare-en") << poe2Armour << true << 1 << 2;
        QTest::newRow("poe2-jewel-en") << QStringLiteral(
            "Item Class: Jewels\nRarity: Rare\nTest Jewel\nRuby\n--------\nItem Level: 80\n--------\n"
            "{ Prefix Modifier }\nUnknown modifier line\n{ Suffix Modifier }\nSecond modifier line\n") << true << 1 << 1;
        QTest::newRow("poe2-crafted-jewel-en") << QStringLiteral(
            "Item Class: Jewels\nRarity: Rare\nTest Jewel\nRuby\n--------\nItem Level: 80\n--------\n"
            "{ Crafted Prefix Modifier }\n+1 Suffix Modifier allowed (crafted)\n") << true << 1 << 3;
    }

    void emptyAffixes()
    {
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        QFETCH(QString, itemText);
        QFETCH(bool, poe2);
        QFETCH(int, prefixes);
        QFETCH(int, suffixes);
        const auto it = parseText(itemText, poe2 ? Game::Poe2 : Game::Poe1);
        QVERIFY(it);
        QCOMPARE(it->emptyPrefixes, prefixes);
        QCOMPARE(it->emptySuffixes, suffixes);

        ItemFilterState st;
        applyPreset(*it, Preset::Default, 10, &st);
        QVERIFY(!st.emptyPrefixes.use && !st.emptySuffixes.use);
        QCOMPARE(st.emptyPrefixes.min.value_or(-1), static_cast<double>(prefixes > 0 ? prefixes : -1));
        QCOMPARE(st.emptySuffixes.min.value_or(-1), static_cast<double>(suffixes > 0 ? suffixes : -1));

        const QString prefixId = QStringLiteral("pseudo.pseudo_number_of_empty_prefix_mods");
        const QString suffixId = QStringLiteral("pseudo.pseudo_number_of_empty_suffix_mods");
        const GameDb* gdb = &dbs[gi(it->game)];
        const TradeDb* db = &(*gdb)[it->lang];
        QVERIFY(db->findStat(prefixId) >= 0 && db->findStat(suffixId) >= 0);
        RequestContext ctx;
        ctx.server = it->lang;
        auto queryStat = [&](const QString& id) {
            const SearchRequest req = makeRequest(*it, st, kw, gdb, ctx);
            const QJsonArray group = QueryBuilder::build(req, db).value(QStringLiteral("query")).toObject()
                                         .value(QStringLiteral("stats")).toArray();
            const QJsonArray filters = group.first().toObject().value(QStringLiteral("filters")).toArray();
            for (const QJsonValue& filter : filters) {
                if (filter.toObject().value(QStringLiteral("id")).toString() == id)
                    return filter.toObject();
            }
            return QJsonObject{};
        };
        QVERIFY(queryStat(prefixId).isEmpty());
        QVERIFY(queryStat(suffixId).isEmpty());

        // 최소칸을 수정해 켰을 때 빈 칸이 있는 쪽만 활성 stat 으로 전송
        st.emptyPrefixes = {true, 2};
        st.emptySuffixes = {true, 1};
        for (bool browser : {false, true}) {
            ctx.forBrowser = browser;
            const QJsonObject prefixBound{{QStringLiteral("min"), 2}};
            const QJsonObject suffixBound{{QStringLiteral("min"), 1}};
            QCOMPARE(queryStat(prefixId), prefixes > 0 ? QJsonObject({
                {QStringLiteral("id"), prefixId}, {QStringLiteral("value"), prefixBound}}) : QJsonObject());
            QCOMPARE(queryStat(suffixId), suffixes > 0 ? QJsonObject({
                {QStringLiteral("id"), suffixId}, {QStringLiteral("value"), suffixBound}}) : QJsonObject());
        }
    }

    void estimatePerSellerCap()
    {
        // 한 판매자가 8카오스로 7개 도배 + 다른 판매자들 10카오스 6개.
        // 상한 없으면 싼 쪽 10개 중 7개가 도배라 추정가 8, 상한(판매자당 2개) 있으면 10
        Rates rates;
        rates.base = QStringLiteral("chaos");
        rates.toBase.insert(QStringLiteral("divine"), 200);
        QVector<Listing> ls;
        auto add = [&](double amount, const QString& account) {
            Listing l;
            l.amount = amount;
            l.currency = QStringLiteral("chaos");
            l.account = account;
            ls.push_back(l);
        };
        for (int i = 0; i < 7; ++i)
            add(8, QStringLiteral("spammer"));
        for (int i = 0; i < 6; ++i)
            add(10, QStringLiteral("seller%1").arg(i));
        const Estimate e = estimatePrice(ls, rates);
        QVERIFY(e.ok);
        QCOMPARE(e.used, 8); // spammer 2개 + 6개
        QVERIFY2(e.value >= 9.9, qPrintable(QString::number(e.value)));
    }

    void newMapSystem()
    {
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        // "지도 (16등급)": 베이스 "지도" + 구분자 map, 등급 16. 구분자 없으면 거래소가 0개를 돌려줌
        auto m = parse(QStringLiteral("poe1_ko_rare_map_t16.txt"), Game::Poe1);
        QVERIFY(m);
        QVERIFY(m->isMap);
        QCOMPARE(m->mapTier, 16);
        QCOMPARE(m->type, QStringLiteral("지도"));
        QCOMPARE(m->disc, QStringLiteral("map"));
        QHash<QString, double> pv;
        for (const ItemProp& p : m->props)
            pv.insert(p.key, p.value);
        QCOMPARE(pv.value(QStringLiteral("map_iir")), 124.0);
        QCOMPARE(pv.value(QStringLiteral("map_packsize")), 33.0);
        QCOMPARE(pv.value(QStringLiteral("map_bonus")), 87.0);
        QCOMPARE(pv.value(QStringLiteral("ilvl")), 84.0);
        for (const ParsedMod& x : m->mods)
            QVERIFY2(!x.raw.startsWith(QStringLiteral("몬스터 레벨")), qPrintable(x.raw)); // 속성 줄은 옵션 아님

        ItemFilterState st;
        const ParsedItem it = applyPreset(*m, Preset::Default, 10, &st);
        for (PropFilterState& p : st.props) {
            if (p.key == QStringLiteral("ilvl")) {
                QCOMPARE(*p.min, 84.0); // 아이템 레벨은 완화 안 함
                p.use = true;
            }
            if (p.key == QStringLiteral("map_iir")) {
                QCOMPARE(*p.min, 111.0); // 124 -10%
                p.use = true;
            }
        }
        RequestContext ctx;
        ctx.server = Lang::Ko;
        const QJsonObject q = QueryBuilder::build(makeRequest(it, st, kw, &dbs[0], ctx), &dbs[0][Lang::Ko])
                                  .value(QStringLiteral("query")).toObject();
        const QJsonObject type = q.value(QStringLiteral("type")).toObject();
        QCOMPARE(type.value(QStringLiteral("option")).toString(), QStringLiteral("지도"));
        QCOMPARE(type.value(QStringLiteral("discriminator")).toString(), QStringLiteral("map"));
        const QJsonObject mf = q.value(QStringLiteral("filters")).toObject().value(QStringLiteral("map_filters")).toObject()
                                   .value(QStringLiteral("filters")).toObject();
        QCOMPARE(mf.value(QStringLiteral("map_tier")).toObject().value(QStringLiteral("min")).toDouble(), 16.0);
        QCOMPARE(mf.value(QStringLiteral("map_iir")).toObject().value(QStringLiteral("min")).toDouble(), 111.0);
        const QJsonObject misc = q.value(QStringLiteral("filters")).toObject().value(QStringLiteral("misc_filters")).toObject()
                                     .value(QStringLiteral("filters")).toObject();
        QCOMPARE(misc.value(QStringLiteral("ilvl")).toObject().value(QStringLiteral("min")).toDouble(), 84.0);

        // 변형 젬 구분자는 그대로, 일반 젬엔 구분자 안 붙음
        auto gem = parse(QStringLiteral("poe1_en_gem_transfigured.txt"), Game::Poe1);
        QVERIFY(gem && gem->transfigured && !gem->disc.isEmpty());
    }

    void mirroredAndExactDefault()
    {
        // 설정 v7: 예전 기본 10% -> 일치(0), 직접 고른 값은 그대로
        QCOMPARE(Config::defaults().searchStatRange, 0);
        const QString path = QDir::temp().filePath(QStringLiteral("pts_cfg_v7.txt"));
        for (int pct : {10, 20}) {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(QStringLiteral("{\"config_version\":6,\"options\":{\"search_stat_range\":%1}}").arg(pct).toUtf8());
            f.close();
            Config c;
            QVERIFY(c.load(path));
            QCOMPARE(c.searchStatRange, pct == 10 ? 0 : 20);
        }
        QFile::remove(path);

        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        // 한국어 "복제" 한 줄 -> 복제된 아이템, 기본은 복제된 매물만
        auto m = parse(QStringLiteral("poe1_ko_rare_ring_mirrored.txt"), Game::Poe1);
        QVERIFY(m);
        QVERIFY(m->mirrored);
        ItemFilterState st = defaultFilterState(*m);
        QCOMPARE(st.mirrored, 1);
        RequestContext ctx;
        ctx.server = Lang::Ko;
        auto misc = [&](const ItemFilterState& s) {
            return QueryBuilder::build(makeRequest(*m, s, kw, &dbs[0], ctx), nullptr)
                .value(QStringLiteral("query")).toObject().value(QStringLiteral("filters")).toObject()
                .value(QStringLiteral("misc_filters")).toObject().value(QStringLiteral("filters")).toObject();
        };
        QCOMPARE(misc(st).value(QStringLiteral("mirrored")).toObject().value(QStringLiteral("option")).toString(), QStringLiteral("true"));
        st.mirrored = 0; // 복제 줄 끔 -> 조건 없음
        QVERIFY(!misc(st).contains(QStringLiteral("mirrored")));
    }

    void qualityOverTwentyAndNotFractured()
    {
        // 설정 v8: 창 위치 기본 화면 가운데
        QCOMPARE(Config::defaults().popupPosition, QStringLiteral("center"));
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        // 퀄 30 장갑: 퀄리티 기본 켬(최소 30), 분열 안 된 장비라 분열 매물 제외
        auto g = parse(QStringLiteral("poe1_ko_rare_gloves_q30.txt"), Game::Poe1);
        QVERIFY(g);
        QCOMPARE(g->quality, 30);
        QVERIFY(g->checkQuality);
        const ItemFilterState st = defaultFilterState(*g);
        QVERIFY(st.useQuality);
        QCOMPARE(*st.qualityMin, 30.0);
        QVERIFY(st.notFractured);
        RequestContext ctx;
        ctx.server = Lang::Ko;
        const QJsonObject f = QueryBuilder::build(makeRequest(*g, st, kw, &dbs[0], ctx), &dbs[0][Lang::Ko])
                                  .value(QStringLiteral("query")).toObject().value(QStringLiteral("filters")).toObject();
        QCOMPARE(f.value(QStringLiteral("misc_filters")).toObject().value(QStringLiteral("filters")).toObject()
                     .value(QStringLiteral("fractured_item")).toObject().value(QStringLiteral("option")).toString(),
                 QStringLiteral("false"));
        QJsonObject qf;
        for (const QString& grp : f.keys()) {
            const QJsonObject gf = f.value(grp).toObject().value(QStringLiteral("filters")).toObject();
            if (gf.contains(QStringLiteral("quality")))
                qf = gf.value(QStringLiteral("quality")).toObject();
        }
        QCOMPARE(qf.value(QStringLiteral("min")).toDouble(), 30.0);
        // 분열된 아이템엔 분열 제외 안 넣음
        auto fa = parse(QStringLiteral("poe1_ko_magic_armour_fractured.txt"), Game::Poe1);
        QVERIFY(fa && !defaultFilterState(*fa).notFractured);
    }

    void leaguesPcOnlyNoDuplicates()
    {
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        // 거래소 리그 목록은 PC/Xbox/PlayStation 용 같은 이름을 3번 줌 -> PC 만, 중복 없이
        QStringList ids;
        for (const auto& l : dbs[0][Lang::En].leagues())
            ids << l.id;
        QVERIFY(!ids.isEmpty());
        QCOMPARE(ids.removeDuplicates(), qsizetype(0));
        QVERIFY(ids.contains(QStringLiteral("Standard")));
    }

    void mapCitadelOriginator()
    {
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        // "지도에 바란의 시타델(...) 등장": (...) 를 빼고 선택지 옵션(1=바란)으로, 태초자의 기억과 함께 기본 켬
        auto m = parse(QStringLiteral("poe1_ko_rare_map_citadel_originator.txt"), Game::Poe1);
        QVERIFY(m && m->isMap);
        const ParsedMod* cit = nullptr;
        const ParsedMod* org = nullptr;
        for (const ParsedMod& x : m->mods) {
            if (x.unmatched || !x.current())
                continue;
            if (x.current()->id.startsWith(QStringLiteral("implicit.stat_2563183002")))
                cit = &x;
            if (x.current()->id == QStringLiteral("implicit.stat_2696470877"))
                org = &x;
        }
        QVERIFY(cit && cit->checked && cit->optionStat);
        QVERIFY(org && org->checked);
        RequestContext ctx;
        ctx.server = Lang::Ko;
        const QJsonArray stats = QueryBuilder::build(makeRequest(*m, defaultFilterState(*m), kw, &dbs[0], ctx), &dbs[0][Lang::Ko])
                                     .value(QStringLiteral("query")).toObject().value(QStringLiteral("stats")).toArray()
                                     .at(0).toObject().value(QStringLiteral("filters")).toArray();
        bool citOk = false, orgOk = false;
        for (const auto& v : stats) {
            const QJsonObject f = v.toObject();
            if (f.value(QStringLiteral("id")).toString() == QStringLiteral("implicit.stat_2563183002"))
                citOk = f.value(QStringLiteral("value")).toObject().value(QStringLiteral("option")).toInt() == 1;
            if (f.value(QStringLiteral("id")).toString() == QStringLiteral("implicit.stat_2696470877"))
                orgOk = true;
        }
        QVERIFY(citOk && orgOk);
    }

    void foilUnique()
    {
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        RequestContext ctx;
        ctx.server = Lang::Ko;
        auto miscOf = [&](const ParsedItem& it) {
            const QJsonObject f = QueryBuilder::build(makeRequest(it, defaultFilterState(it), kw, &dbs[0], ctx), &dbs[0][Lang::Ko])
                                      .value(QStringLiteral("query")).toObject().value(QStringLiteral("filters")).toObject();
            return f;
        };
        // 반짝이 버전 니미스: 반짝이 버전만 검색
        auto foil = parse(QStringLiteral("poe1_ko_unique_ring_foil.txt"), Game::Poe1);
        QVERIFY(foil && foil->foil);
        QCOMPARE(foil->rarityFilter, QStringLiteral("uniquefoil"));
        QJsonObject f = miscOf(*foil);
        QCOMPARE(f.value(QStringLiteral("type_filters")).toObject().value(QStringLiteral("filters")).toObject()
                     .value(QStringLiteral("rarity")).toObject().value(QStringLiteral("option")).toString(),
                 QStringLiteral("uniquefoil"));
        QVERIFY(!f.value(QStringLiteral("misc_filters")).toObject().value(QStringLiteral("filters")).toObject()
                     .contains(QStringLiteral("foil_variation")));
        // 일반 고유: 반짝이 버전 제외
        auto plain = parse(QStringLiteral("poe1_ko_unique_gull.txt"), Game::Poe1);
        QVERIFY(plain && !plain->foil);
        f = miscOf(*plain);
        QCOMPARE(f.value(QStringLiteral("misc_filters")).toObject().value(QStringLiteral("filters")).toObject()
                     .value(QStringLiteral("foil_variation")).toObject().value(QStringLiteral("option")).toString(),
                 QStringLiteral("none"));
    }

    void runHotkeyMigration()
    {
        // {Run} 키 없던 설정 -> Alt+D + Ctrl+C 감시 켬 / 직접 지정한 설정 -> 그대로 + 감시 끔(예전 동작)
        const QString path = QDir::temp().filePath(QStringLiteral("pts_cfg_v6.txt"));
        for (int key : {0, 0x77}) {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(QStringLiteral("{\"config_version\":5,\"options\":{},\"shortcuts\":[{\"keycode\":%1,\"modifiers\":0,\"value\":\"{Run}\"}]}")
                        .arg(key).toUtf8());
            f.close();
            Config c;
            QVERIFY(c.load(path));
            QCOMPARE(c.shortcuts[0].keycode, key == 0 ? 0x44 : key);
            QCOMPARE(c.shortcuts[0].modifiers, key == 0 ? 1 : 0);
            QCOMPARE(c.clipboardWatch, key == 0);
        }
        QFile::remove(path);
        QCOMPARE(Config::defaults().shortcuts[0].keycode, 0x44);
    }

    void hideoutHotkeyMigration()
    {
        // v9: 은신처가 예전 기본값 F2 면 F5 로, F5 를 이미 쓰거나 다른 키로 바꾼 설정은 그대로
        const QString path = QDir::temp().filePath(QStringLiteral("pts_cfg_v9.txt"));
        struct Case { QString extra; int hideoutKey; int expected; };
        const QVector<Case> cases = {
            {QString(), 113, 116},
            {QStringLiteral(",{\"keycode\":116,\"modifiers\":0,\"value\":\"{Pause}\"}"), 113, 113},
            {QString(), 120, 120},
        };
        for (const Case& t : cases) {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(QStringLiteral("{\"config_version\":8,\"options\":{},\"shortcuts\":["
                                   "{\"keycode\":%1,\"modifiers\":0,\"value\":\"{Enter}/hideout{Enter}\"},"
                                   "{\"keycode\":115,\"modifiers\":0,\"value\":\"{Enter}/exit{Enter}\"}%2]}")
                        .arg(t.hideoutKey).arg(t.extra).toUtf8());
            f.close();
            Config c;
            QVERIFY(c.load(path));
            QCOMPARE(c.shortcuts[0].keycode, t.expected);
            QCOMPARE(c.shortcuts[1].keycode, 115);
        }
        QFile::remove(path);
        const Config d = Config::defaults();
        for (const Shortcut& s : d.shortcuts) {
            if (s.value.contains(QStringLiteral("/hideout")))
                QCOMPARE(s.keycode, 116);
            if (s.value.contains(QStringLiteral("/exit")))
                QCOMPARE(s.keycode, 115);
        }
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
            for (const TradeChoice& pc : priceCurrencies(g))
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
        // 물리+원소가 섞이면 전체 DPS 줄도 있음
        auto hasProp = [](const ParsedItem& x, const char* key) {
            return std::any_of(x.props.begin(), x.props.end(), [&](const ItemProp& p) { return p.key == QLatin1String(key); });
        };
        QVERIFY(hasProp(*it, "dps"));
        // 물리만 있는 무기는 전체 DPS = 물리 DPS 라 물리 DPS 한 줄만, 그게 자동 체크
        const auto crossbow = parseText(QStringLiteral(
            "Item Class: Crossbows\nRarity: Rare\nDoom Bolt\nSiege Crossbow\n--------\n"
            "Quality: +29% (augmented)\nPhysical Damage: 414-1,043 (augmented)\nAttacks per Second: 2.07 (augmented)\n"
            "--------\nItem Level: 82\n"), Game::Poe2);
        QVERIFY(crossbow && crossbow->hasDps);
        QVERIFY(!hasProp(*crossbow, "dps"));
        QVERIFY(hasProp(*crossbow, "pdps"));
        QVERIFY(std::any_of(crossbow->props.begin(), crossbow->props.end(),
                            [](const ItemProp& p) { return p.key == QLatin1String("pdps") && p.use; }));
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

    void poe1VaalGemSkillSection()
    {
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        auto typeOf = [&](const ParsedItem& it, Lang server) {
            RequestContext ctx;
            ctx.server = server;
            const ItemFilterState st = defaultFilterState(it);
            return QueryBuilder::build(makeRequest(it, st, kw, &dbs[0], ctx), &dbs[0][server])
                .value(QStringLiteral("query")).toObject().value(QStringLiteral("type"));
        };
        // 헤더는 기본 젬 이름, 바알 스킬 이름은 뒤쪽 한 줄 섹션 -> 바알 젬으로 검색
        struct Case {
            const char* fx;
            Lang lang;
            const char* base;
            const char* vaal;
        };
        const Case cases[] = {
            {"poe1_ko_gem_vaal_haste.txt", Lang::Ko, "가속", "바알 가속"},
            {"poe1_en_gem_vaal_haste.txt", Lang::En, "Haste", "Vaal Haste"},
        };
        for (const Case& c : cases) {
            auto it = parse(QString::fromUtf8(c.fx), Game::Poe1);
            QVERIFY2(it, c.fx);
            QVERIFY(it->isGem && it->vaalGem && it->corrupted && !it->transfigured);
            QCOMPARE(it->rawType, QString::fromUtf8(c.base));
            QCOMPARE(it->type, QString::fromUtf8(c.vaal));
            QVERIFY(it->disc.isEmpty());
            QCOMPARE(it->category, QStringLiteral("gem.activegem"));
            QCOMPARE(it->level, 20);
            QVERIFY(it->levelMax && it->checkLevel);
            QCOMPARE(defaultFilterState(*it).corrupted, 1);
            QCOMPARE(typeOf(*it, c.lang).toString(), QString::fromUtf8(c.vaal));
        }
        // 한국어 아이템을 영어 서버로 검색해도 바알 젬
        auto ko = parse(QStringLiteral("poe1_ko_gem_vaal_haste.txt"), Game::Poe1);
        QVERIFY(ko);
        QCOMPARE(typeOf(*ko, Lang::En).toString(), QStringLiteral("Vaal Haste"));

        // 헤더에 바알 이름이 바로 있는 형식은 그대로
        auto hdr = parseText(QStringLiteral("Item Class: Skill Gems\nRarity: Gem\nVaal Haste\n--------\n"
                                            "Aura, Spell, AoE, Duration, Vaal\nLevel: 20 (Max)\n--------\nCorrupted"),
                             Game::Poe1);
        QVERIFY(hdr && hdr->vaalGem);
        QCOMPARE(hdr->type, QStringLiteral("Vaal Haste"));

        // 바알 변형 젬: 헤더 변형 이름 먼저 풀고, 같은 구분자의 바알 젬으로
        auto gemText = [](const QString& cls, const QString& head, const QString& vaal, const QString& corrupt) {
            return cls + QStringLiteral("\n") + head + QStringLiteral("\n--------\nLevel: 20 (Max)\n--------\n") + vaal +
                   QStringLiteral("\n--------\n") + corrupt;
        };
        const QString enCls = QStringLiteral("Item Class: Skill Gems\nRarity: Gem");
        const QString koCls = QStringLiteral("아이템 종류: 스킬 젬\n아이템 희귀도: 젬");
        struct TfCase {
            const char* head;
            const char* vaal;
            const char* disc;
            bool ko;
        };
        const TfCase tf[] = {
            {"Arc of Oscillating", "Vaal Arc", "alt_y", false},
            {"Arc of Surging", "Vaal Arc", "alt_x", false},
            {"뇌동의 연쇄 번개", "바알 연쇄 번개", "alt_y", true},
            {"쇄도의 연쇄 번개", "바알 연쇄 번개", "alt_x", true},
        };
        for (const TfCase& c : tf) {
            auto g = parseText(gemText(c.ko ? koCls : enCls, QString::fromUtf8(c.head), QString::fromUtf8(c.vaal),
                                       c.ko ? QStringLiteral("타락") : QStringLiteral("Corrupted")),
                               Game::Poe1);
            QVERIFY2(g, c.head);
            QVERIFY2(g->vaalGem && g->transfigured, c.head);
            QCOMPARE(g->type, QString::fromUtf8(c.vaal));
            QCOMPARE(g->disc, QString::fromUtf8(c.disc));
            const QJsonObject t = typeOf(*g, c.ko ? Lang::Ko : Lang::En).toObject();
            QCOMPARE(t.value(QStringLiteral("option")).toString(), QString::fromUtf8(c.vaal));
            QCOMPARE(t.value(QStringLiteral("discriminator")).toString(), QString::fromUtf8(c.disc));
        }
        // 구분자가 안 맞는 바알 줄이면 안 바꿈 (변형 구분자 유지)
        auto mis = parseText(gemText(enCls, QStringLiteral("Arc of Oscillating"), QStringLiteral("Vaal Haste"),
                                     QStringLiteral("Corrupted")),
                             Game::Poe1);
        QVERIFY(mis && mis->transfigured && !mis->vaalGem);
        QCOMPARE(mis->type, QStringLiteral("Arc"));
        QCOMPARE(mis->disc, QStringLiteral("alt_y"));
        // 일반 기본 젬 + 바알 줄 -> 구분자 없는 바알 젬 (변형 바알 젬으로 안 감)
        auto base = parseText(gemText(enCls, QStringLiteral("Arc"), QStringLiteral("Vaal Arc"), QStringLiteral("Corrupted")),
                              Game::Poe1);
        QVERIFY(base && base->vaalGem && !base->transfigured);
        QCOMPARE(base->type, QStringLiteral("Vaal Arc"));
        QVERIFY(base->disc.isEmpty());

        // 바알 섹션 없는 일반 타락 젬은 기본 젬 그대로
        auto plain = parseText(QStringLiteral("아이템 종류: 스킬 젬\n아이템 희귀도: 젬\n가속\n--------\n"
                                              "오라, 주문, 효과 범위, 지속시간\n레벨: 20 (최대)\n--------\n"
                                              "아이템 홈에 장착하여 스킬을 획득하십시오. 제거하려면 홈을 우클릭하십시오.\n"
                                              "--------\n타락"),
                               Game::Poe1);
        QVERIFY(plain && plain->isGem && plain->corrupted && !plain->vaalGem);
        QCOMPARE(plain->type, QStringLiteral("가속"));
        QCOMPARE(typeOf(*plain, Lang::Ko).toString(), QStringLiteral("가속"));
    }

    void modifiableFilter()
    {
        // 데이터 없이: 수정 가능 = 타락 아님 + 복제 아님, 기존 타락/두 번 타락/복제 선택보다 우선
        ParsedItem it;
        it.game = Game::Poe1;
        it.rarityId = QStringLiteral("rare");
        it.isGear = true;
        it.top = QStringLiteral("armour");
        it.corrupted = true;
        it.mirrored = true;
        ItemFilterState st;
        QCOMPARE(defaultFilterState(it).modifiable, false); // 기본 꺼짐
        st.corrupted = 1;
        st.twiceCorrupted = 1;
        st.mirrored = 1;
        auto misc = [&](Game g) {
            it.game = g;
            RequestContext ctx;
            const QJsonObject f = QueryBuilder::build(makeRequest(it, st, kw, nullptr, ctx), nullptr)
                                      .value(QStringLiteral("query")).toObject().value(QStringLiteral("filters")).toObject();
            QJsonObject out;
            for (const QString& grp : f.keys()) {
                const QJsonObject gf = f.value(grp).toObject().value(QStringLiteral("filters")).toObject();
                for (const QString& k : gf.keys())
                    out.insert(k, gf.value(k));
            }
            return out;
        };
        QJsonObject m = misc(Game::Poe1);
        QCOMPARE(m.value(QStringLiteral("corrupted")).toObject().value(QStringLiteral("option")).toString(), QStringLiteral("true"));
        QCOMPARE(m.value(QStringLiteral("mirrored")).toObject().value(QStringLiteral("option")).toString(), QStringLiteral("true"));
        st.modifiable = true;
        for (Game g : {Game::Poe1, Game::Poe2}) {
            m = misc(g);
            QCOMPARE(m.value(QStringLiteral("corrupted")).toObject().value(QStringLiteral("option")).toString(), QStringLiteral("false"));
            QCOMPARE(m.value(QStringLiteral("mirrored")).toObject().value(QStringLiteral("option")).toString(), QStringLiteral("false"));
            QVERIFY(!m.contains(QStringLiteral("twice_corrupted")));
        }
        if (!haveData)
            return;
        // 실제 아이템 (PoE1 두 번 타락 고유 / 복제 반지, PoE2 타락 갑옷), DB 필터 그룹으로도 같음
        struct Fx {
            const char* fx;
            Game g;
            Lang l;
        };
        for (const Fx& f : {Fx{"poe1_ko_unique_twice_corrupted.txt", Game::Poe1, Lang::Ko},
                            Fx{"poe1_ko_rare_ring_mirrored.txt", Game::Poe1, Lang::Ko},
                            Fx{"poe2_en_rare_armour.txt", Game::Poe2, Lang::En}}) {
            auto p = parse(QString::fromUtf8(f.fx), f.g);
            QVERIFY2(p, f.fx);
            ItemFilterState s = defaultFilterState(*p);
            s.modifiable = true;
            RequestContext ctx;
            ctx.server = f.l;
            const QJsonObject mf = QueryBuilder::build(makeRequest(*p, s, kw, &dbs[gi(f.g)], ctx), &dbs[gi(f.g)][f.l])
                                       .value(QStringLiteral("query")).toObject().value(QStringLiteral("filters")).toObject()
                                       .value(QStringLiteral("misc_filters")).toObject().value(QStringLiteral("filters")).toObject();
            QCOMPARE(mf.value(QStringLiteral("corrupted")).toObject().value(QStringLiteral("option")).toString(), QStringLiteral("false"));
            QCOMPARE(mf.value(QStringLiteral("mirrored")).toObject().value(QStringLiteral("option")).toString(), QStringLiteral("false"));
            QVERIFY2(!mf.contains(QStringLiteral("twice_corrupted")), f.fx);
        }
    }

    void poe1MagicFracturedItemLevel()
    {
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        auto query = [&](const ParsedItem& it) {
            RequestContext ctx;
            ctx.server = Lang::En;
            return QueryBuilder::build(makeRequest(it, defaultFilterState(it), kw, &dbs[0], ctx), &dbs[0][Lang::En])
                .value(QStringLiteral("query")).toObject();
        };
        auto ilvl = [](const QJsonObject& q) {
            return q.value(QStringLiteral("filters")).toObject().value(QStringLiteral("misc_filters")).toObject()
                .value(QStringLiteral("filters")).toObject().value(QStringLiteral("ilvl")).toObject();
        };
        // 마법 분열 바알 도끼 (ilvl 85, 분열 물리% + 명중): 아이템 레벨 최소 85 기본, 최대 없음, 분열 두 옵션 유지
        const QString txt = fixture(QStringLiteral("poe1_en_magic_axe_fractured.txt"));
        auto ax = parseText(txt, Game::Poe1);
        QVERIFY(ax && ax->fractured && ax->advanced && ax->rarityId == QStringLiteral("magic") && ax->isGear);
        QCOMPARE(ax->type, QStringLiteral("Vaal Axe"));
        QCOMPARE(ax->itemLevel, 85);
        QVERIFY(ax->checkLevel);
        const ItemFilterState st = defaultFilterState(*ax);
        QVERIFY(st.useLevel);
        QVERIFY(st.levelMin.has_value());
        QCOMPARE(*st.levelMin, 85.0);
        QVERIFY(!st.levelMax);
        const QJsonObject q = query(*ax);
        QCOMPARE(ilvl(q).value(QStringLiteral("min")).toDouble(), 85.0);
        QVERIFY(!ilvl(q).contains(QStringLiteral("max")));
        QStringList ids;
        for (const auto& f : q.value(QStringLiteral("stats")).toArray().first().toObject().value(QStringLiteral("filters")).toArray()) {
            if (!f.toObject().value(QStringLiteral("disabled")).toBool())
                ids << f.toObject().value(QStringLiteral("id")).toString();
        }
        QVERIFY2(ids.contains(QStringLiteral("fractured.stat_1509134228")), qPrintable(ids.join(QLatin1Char(','))));
        QVERIFY2(ids.contains(QStringLiteral("fractured.stat_691932474")), qPrintable(ids.join(QLatin1Char(','))));

        // 분열 없는 같은 마법 도끼 / 희귀 분열 도끼는 예전처럼 아이템 레벨 끔
        QString plain = txt;
        plain.replace(QStringLiteral("{ Fractured Prefix Modifier"), QStringLiteral("{ Prefix Modifier"));
        plain.remove(QStringLiteral("\n--------\nFractured Item"));
        QVERIFY(!plain.contains(QStringLiteral("Fractured")));
        auto p = parseText(plain, Game::Poe1);
        QVERIFY(p && !p->fractured && !p->checkLevel);
        for (const ParsedMod& m : p->mods)
            QVERIFY2(m.modType != QStringLiteral("fractured"), qPrintable(m.raw));
        QVERIFY(ilvl(query(*p)).isEmpty());
        QString rare = txt;
        rare.replace(QStringLiteral("Rarity: Magic\nChampion's Vaal Axe"), QStringLiteral("Rarity: Rare\nDoom Edge\nVaal Axe"));
        auto r = parseText(rare, Game::Poe1);
        QVERIFY(r && r->fractured && r->rarityId == QStringLiteral("rare") && !r->checkLevel);
    }

    void poe1TransfiguredGemImbued()
    {
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        auto query = [&](const ParsedItem& it, Lang server) {
            RequestContext ctx;
            ctx.server = server;
            return QueryBuilder::build(makeRequest(it, defaultFilterState(it), kw, &dbs[0], ctx), &dbs[0][server])
                .value(QStringLiteral("query")).toObject();
        };
        auto filter = [](const QJsonObject& q, const char* key) {
            const QJsonObject f = q.value(QStringLiteral("filters")).toObject();
            for (const QString& grp : f.keys()) {
                const QJsonObject gf = f.value(grp).toObject().value(QStringLiteral("filters")).toObject();
                if (gf.contains(QLatin1String(key)))
                    return gf.value(QLatin1String(key)).toObject();
            }
            return QJsonObject();
        };
        // 사용자 클립보드 (Covens): 변성 + 타락, 부여 안 됨. "부여된" 매물(보조 젬 내장, 1 div)이 섞이지 않게 부여 제외
        struct Case {
            const char* fx;
            Lang lang;
            bool imbued;
        };
        const Case cases[] = {
            {"poe1_ko_gem_transfigured_massacre.txt", Lang::Ko, false},
            {"poe1_ko_gem_transfigured_imbued.txt", Lang::Ko, true},
            {"poe1_en_gem_transfigured_imbued.txt", Lang::En, true},
        };
        for (const Case& c : cases) {
            auto it = parse(QString::fromUtf8(c.fx), Game::Poe1);
            QVERIFY2(it, c.fx);
            QVERIFY(it->isGem && it->transfigured && it->corrupted && !it->vaalGem);
            QCOMPARE(it->imbued, c.imbued);
            QCOMPARE(it->type, c.lang == Lang::Ko ? QStringLiteral("천상의 단도") : QStringLiteral("Ethereal Knives"));
            QCOMPARE(it->disc, QStringLiteral("alt_y"));
            QCOMPARE(it->level, 20);
            QCOMPARE(it->quality, 20);
            QVERIFY(it->levelMax && it->checkLevel && it->checkQuality);
            QCOMPARE(it->corruptDefault, 1);
            for (Lang server : {Lang::Ko, Lang::En}) {
                const QJsonObject q = query(*it, server);
                const QJsonObject type = q.value(QStringLiteral("type")).toObject();
                QCOMPARE(type.value(QStringLiteral("option")).toString(),
                         server == Lang::Ko ? QStringLiteral("천상의 단도") : QStringLiteral("Ethereal Knives"));
                QCOMPARE(type.value(QStringLiteral("discriminator")).toString(), QStringLiteral("alt_y"));
                QCOMPARE(filter(q, "gem_level").value(QStringLiteral("min")).toDouble(), 20.0);
                QCOMPARE(filter(q, "quality").value(QStringLiteral("min")).toDouble(), 20.0);
                QCOMPARE(filter(q, "corrupted").value(QStringLiteral("option")).toString(), QStringLiteral("true"));
                QCOMPARE(filter(q, "gem_imbued").value(QStringLiteral("option")).toString(),
                         c.imbued ? QStringLiteral("true") : QStringLiteral("false"));
            }
        }
        // 젬이 아니면 부여 필터 안 넣음
        auto ring = parse(QStringLiteral("poe1_ko_rare_ring.txt"), Game::Poe1);
        QVERIFY(ring && !ring->imbued);
        QVERIFY(filter(query(*ring, Lang::Ko), "gem_imbued").isEmpty());
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
                QCOMPARE(s.keycode, 116); // F5
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

    void fracturedResistanceQuery()
    {
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        const auto ring = parseText(QStringLiteral(
            "아이템 종류: 반지\n아이템 희귀도: 희귀\n파멸 고리\n토파즈 반지\n--------\n아이템 레벨: 85\n--------\n"
            "{ 분열된 접미어 속성 부여 \"- 화산\" (등급: 2) — 원소, 화염, 저항 }\n"
            "화염 저항 +40(36-41)%\n--------\n분열된 아이템\n"), Game::Poe1);
        QVERIFY(ring && ring->fractured);
        for (Preset preset : {Preset::Default, Preset::Exact}) {
            ItemFilterState st;
            auto item = applyPreset(*ring, preset, 10, &st);
            item.mods[0].totalRes = true;
            const auto query = searchQuery(item, st);
            const auto fire = activeStat(query, "fractured.stat_3372524247");
            QCOMPARE(fire.value(QStringLiteral("value")).toObject().value(QStringLiteral("min")).toDouble(),
                     preset == Preset::Default ? 36.0 : 40.0);
            QVERIFY(activeStat(query, "pseudo.pseudo_total_resistance").isEmpty());
        }
    }

    void resistanceWeightsAndExactQuery()
    {
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        QString clip = fixture(QStringLiteral("poe1_ko_rare_ring.txt"));
        clip.replace(QStringLiteral("힘 +12"), QStringLiteral("모든 원소 저항 +12%"));
        auto item = parseText(clip, Game::Poe1);
        QVERIFY(item);
        for (auto& mod : item->mods)
            mod.checked = mod.resistance;
        auto query = searchQuery(*item, defaultFilterState(*item));
        const auto res = activeStat(query, "pseudo.pseudo_total_resistance").value(QStringLiteral("value")).toObject();
        QCOMPARE(res.value(QStringLiteral("min")).toDouble(), 135.0); // 15×2 + 12×3 + 38 + 31
        QVERIFY(activeStat(query, "explicit.stat_2901986750").isEmpty());

        ItemFilterState st;
        const auto exact = applyPreset(*item, Preset::Exact, 10, &st);
        query = searchQuery(exact, st);
        QVERIFY(activeStat(query, "pseudo.pseudo_total_resistance").isEmpty());
        QVERIFY(!activeStat(query, "explicit.stat_3372524247").isEmpty());
        QVERIFY(!activeStat(query, "explicit.stat_2901986750").isEmpty());
    }

    void specialItemQuery()
    {
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        const auto glove = parseText(QStringLiteral(
            "아이템 종류: 장갑\n아이템 희귀도: 희귀\n맹공 발\n레비아탄 건틀릿\n--------\n아이템 레벨: 85\n--------\n"
            "작열의 총주교 아이템\n--------\n세계 포식자 아이템\n"), Game::Poe1);
        QVERIFY(glove && glove->searing && glove->tangled);
        auto st = defaultFilterState(*glove);
        for (bool useDb : {true, false}) {
            const auto misc = searchFilters(searchQuery(*glove, st, useDb), "misc_filters");
            for (const char* flag : {"searing_item", "tangled_item"})
                QCOMPARE(misc.value(QLatin1String(flag)).toObject().value(QStringLiteral("option")).toString(), QStringLiteral("true"));
        }
        st.searing = st.tangled = false;
        const auto misc = searchFilters(searchQuery(*glove, st), "misc_filters");
        QVERIFY(!misc.contains(QStringLiteral("searing_item")) && !misc.contains(QStringLiteral("tangled_item")));

        const auto influenced = parseText(QStringLiteral(
            "아이템 종류: 반지\n아이템 희귀도: 희귀\n파멸 고리\n토파즈 반지\n--------\n아이템 레벨: 85\n--------\n"
            "쉐이퍼 아이템\n"), Game::Poe1);
        QVERIFY(influenced);
        QVERIFY(!activeStat(searchQuery(*influenced, defaultFilterState(*influenced)), "pseudo.pseudo_has_shaper_influence").isEmpty());
        const auto synthesised = parseText(QStringLiteral(
            "아이템 종류: 반지\n아이템 희귀도: 희귀\n파멸 고리\n결합된 토파즈 반지\n--------\n아이템 레벨: 85\n--------\n"
            "결합된 아이템\n"), Game::Poe1);
        QVERIFY(synthesised);
        const auto synthQuery = searchQuery(*synthesised, defaultFilterState(*synthesised));
        QCOMPARE(synthQuery.value(QStringLiteral("type")).toString(), QStringLiteral("토파즈 반지"));
        QVERIFY(searchFilters(synthQuery, "misc_filters").contains(QStringLiteral("synthesised_item")));

        const auto flask = parseText(QStringLiteral(
            "아이템 종류: 특수 플라스크\n아이템 희귀도: 일반\n황금 플라스크\n--------\n아이템 레벨: 85\n"), Game::Poe1);
        QVERIFY(flask && flask->isFlask);
        const auto type = searchFilters(searchQuery(*flask, defaultFilterState(*flask)), "type_filters");
        QCOMPARE(type.value(QStringLiteral("rarity")).toObject().value(QStringLiteral("option")).toString(), QStringLiteral("nonunique"));
    }

    void exactRetainsStructuralStats()
    {
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        const auto axe = parse(QStringLiteral("poe1_en_magic_axe_fractured.txt"), Game::Poe1);
        QVERIFY(axe);
        ItemFilterState st;
        auto exact = applyPreset(*axe, Preset::Exact, 10, &st);
        auto query = searchQuery(exact, st);
        QVERIFY(!activeStat(query, "fractured.stat_1509134228").isEmpty());
        QVERIFY(!activeStat(query, "fractured.stat_691932474").isEmpty());

        const auto cluster = parse(QStringLiteral("poe1_en_cluster.txt"), Game::Poe1);
        QVERIFY(cluster);
        exact = applyPreset(*cluster, Preset::Exact, 10, &st);
        query = searchQuery(exact, st);
        const auto passives = activeStat(query, "enchant.stat_3086156145").value(QStringLiteral("value")).toObject();
        QCOMPARE(passives.value(QStringLiteral("max")).toDouble(), 8.0);
        QVERIFY(!activeStat(query, "enchant.stat_3948993189").isEmpty());
        QVERIFY(activeStat(query, "enchant.stat_4079888060").isEmpty());
    }

    void groupedNumbersAndQuality()
    {
        double value = 0;
        QVERIFY(text::firstNumber(QStringLiteral("1,043 (augmented)"), &value));
        QCOMPARE(value, 1043.0);
        QCOMPARE(text::damageRangeSum(QStringLiteral("414-1,043 (augmented), 1,202-1,555")), 4214.0);
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        const auto crossbow = parseText(QStringLiteral(
            "Item Class: Crossbows\nRarity: Rare\nDoom Bolt\nSiege Crossbow\n--------\n"
            "Quality: +29% (augmented)\nPhysical Damage: 414-1,043 (augmented)\nAttacks per Second: 2.07 (augmented)\n"
            "--------\nItem Level: 82\n--------\n25% increased Attack Speed (fractured)\n--------\nFractured Item\n"), Game::Poe2);
        QVERIFY(crossbow && crossbow->hasDps);
        ItemFilterState st;
        auto item = applyPreset(*crossbow, Preset::Default, 10, &st);
        auto props = searchFilters(searchQuery(item, st), "equipment_filters");
        QCOMPARE(props.value(QStringLiteral("pdps")).toObject().value(QStringLiteral("min")).toDouble(), 1351.0);

        const auto axe = parse(QStringLiteral("poe1_en_magic_axe_fractured.txt"), Game::Poe1);
        QVERIFY(axe && axe->hasDps);
        props = searchFilters(searchQuery(*axe, defaultFilterState(*axe)), "weapon_filters");
        QCOMPARE(props.value(QStringLiteral("pdps")).toObject().value(QStringLiteral("min")).toDouble(), 291.0);

        const QString clip = QStringLiteral(
            "아이템 종류: 장갑\n아이템 희귀도: 희귀\n맹공 발\n레비아탄 건틀릿\n--------\n방어도: 600 (augmented)\n"
            "--------\n아이템 레벨: 85\n--------\n방어도 100% 증가\n");
        const auto glove = parseText(clip, Game::Poe1);
        QVERIFY(glove);
        item = applyPreset(*glove, Preset::Default, 10, &st);
        for (PropFilterState& p : st.props) {
            QVERIFY(!p.use); // 방어 속성은 기본으로 안 켬
            if (p.key == QStringLiteral("ar"))
                p.use = true;
        }
        props = searchFilters(searchQuery(item, st), "armour_filters");
        QCOMPARE(props.value(QStringLiteral("ar")).toObject().value(QStringLiteral("min")).toDouble(), 648.0);
        const auto nonDefence = parseText(clip + QStringLiteral("--------\n퀄리티가 방어력을 증가시키지 않음 (enchant)\n"), Game::Poe1);
        QVERIFY(nonDefence);
        QCOMPARE(nonDefence->props.first().value, 600.0);

        QString armourClip = fixture(QStringLiteral("poe2_en_rare_armour.txt"));
        armourClip.replace(QStringLiteral("Quality: +20"), QStringLiteral("Quality: +10"));
        armourClip.replace(QStringLiteral("812"), QStringLiteral("1,043"));
        const auto corrupted = parseText(armourClip, Game::Poe2);
        QVERIFY(corrupted);
        st = defaultFilterState(*corrupted);
        for (PropFilterState& p : st.props)
            p.use = p.use || p.key == QStringLiteral("ar");
        props = searchFilters(searchQuery(*corrupted, st), "equipment_filters");
        QCOMPARE(props.value(QStringLiteral("ar")).toObject().value(QStringLiteral("min")).toDouble(), 1043.0);
    }

    void poe2GemHeadersAndSockets()
    {
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        const QString clip = QStringLiteral(
            "아이템 종류: 스킬 젬\n아이템 희귀도: 젬\n화염구\n--------\n주문, 투사체, 화염\n레벨: 19\n--------\n"
            "요구 사항: 레벨 86, 205 지능\n--------\n홈: G G G G G\n--------\n타락\n");
        const auto gem = parseText(clip, Game::Poe2);
        QVERIFY(gem && gem->checkSockets && gem->sockets == 5);
        for (bool useDb : {true, false}) {
            const auto query = searchQuery(*gem, defaultFilterState(*gem), useDb);
            const auto misc = searchFilters(query, "misc_filters");
            QCOMPARE(misc.value(QStringLiteral("gem_sockets")).toObject().value(QStringLiteral("min")).toDouble(), 5.0);
            QVERIFY(!searchFilters(query, "equipment_filters").contains(QStringLiteral("rune_sockets")));
        }
        QString twoSockets = clip;
        twoSockets.replace(QStringLiteral("G G G G G"), QStringLiteral("G G"));
        const auto two = parseText(twoSockets, Game::Poe2);
        QVERIFY(two && two->sockets == 2 && !two->checkSockets);

        const QString metaClip = fixture(QStringLiteral("poe2_en_meta_gem.txt"));
        ItemParser parser(kw, dbs, &checked);
        QVERIFY(parser.looksLikeItem(metaClip));
        QCOMPARE(parser.guessGame(metaClip, Game::Poe1), Game::Poe2);
        const auto meta = parseText(metaClip, Game::Poe2);
        QVERIFY(meta && meta->isGem);
        QCOMPARE(meta->category, QStringLiteral("gem.metagem"));
        const auto query = searchQuery(*meta, defaultFilterState(*meta));
        QCOMPARE(query.value(QStringLiteral("type")).toString(), QStringLiteral("Mirage Archer"));
        QCOMPARE(searchFilters(query, "misc_filters").value(QStringLiteral("gem_sockets")).toObject()
                     .value(QStringLiteral("min")).toDouble(), 4.0);
    }

    void poe2DesecratedAdvancedQuery()
    {
        if (!haveData)
            QSKIP("PTS_TEST_DATA 없음");
        const QString clip = QStringLiteral(
            "Item Class: Amulets\nRarity: Rare\nBrood Locket\nGold Amulet\n--------\nItem Level: 81\n--------\n"
            "{ Desecrated Suffix Modifier \"of Amanamu\" (Tier: 1) — Elemental, Fire, Chaos, Resistance }\n"
            "+17(13-17)% to Fire and Chaos Resistances\n");
        for (const QString& flag : {QString(), QStringLiteral("Corrupted"), QStringLiteral("Sanctified")}) {
            auto item = parseText(flag.isEmpty() ? clip : clip + QStringLiteral("--------\n") + flag, Game::Poe2);
            QVERIFY(item && item->mods.size() == 1);
            QCOMPARE(item->mods[0].modType, QStringLiteral("desecrated"));
            const char* expected = flag.isEmpty() ? "explicit.stat_378817135" : "desecrated.stat_378817135";
            QCOMPARE(item->mods[0].current()->id, QLatin1String(expected));
            item->mods[0].checked = true;
            const auto baseQuery = searchQuery(*item, defaultFilterState(*item));
            QVERIFY(!activeStat(baseQuery, expected).isEmpty());
            QVERIFY(activeStat(baseQuery, "pseudo.pseudo_total_resistance").isEmpty());
            ItemFilterState st;
            const auto exact = applyPreset(*item, Preset::Exact, 0, &st);
            const auto stat = activeStat(searchQuery(exact, st), expected);
            QCOMPARE(stat.value(QStringLiteral("value")).toObject().value(QStringLiteral("min")).toDouble(), 17.0);
        }
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

    void estimateSparseAndSplitMarkets()
    {
        Rates rates;
        rates.base = QStringLiteral("chaos");
        auto offer = [](double price, const QString& seller) {
            Listing l;
            l.amount = l.payAmount = price;
            l.getAmount = 1;
            l.account = seller;
            l.currency = QStringLiteral("chaos");
            return l;
        };
        QVector<Listing> sparse{offer(10, QStringLiteral("A")), offer(100, QStringLiteral("B"))};
        const Estimate few = estimatePrice(sparse, rates);
        QCOMPARE(few.used, 2);
        QCOMPARE(few.value, 55.0);
        QCOMPARE(few.lo, 10.0);
        QCOMPARE(few.hi, 100.0);
        QCOMPARE(few.confidence, QStringLiteral("낮음"));
        QCOMPARE(medianRatio(sparse), 55.0);

        QVector<Listing> split{offer(1, QStringLiteral("one-off"))};
        for (int i = 0; i < 9; ++i)
            split.push_back(offer(i < 4 ? 10 : 100, QString::number(i)));
        const Estimate twoBands = estimatePrice(split, rates);
        QCOMPARE(twoBands.used, 9);
        QCOMPARE(twoBands.lo, 10.0);
        QCOMPARE(twoBands.hi, 100.0);
        QCOMPARE(twoBands.confidence, QStringLiteral("낮음"));
        QCOMPARE(medianRatio(split), 10.0);

        QVector<Listing> spam;
        for (int i = 0; i < 5; ++i)
            spam.push_back(offer(90, QStringLiteral("spammer")));
        for (int i = 0; i < 4; ++i)
            spam.push_back(offer(200, QString::number(i)));
        QCOMPARE(medianRatio(spam), 200.0);
    }

    void estimateMissingRatesAndRounding()
    {
        Rates rates;
        rates.base = QStringLiteral("chaos");
        auto listing = [](double amount, const QString& currency) {
            Listing l;
            l.amount = amount;
            l.currency = currency;
            return l;
        };
        const QVector<Listing> mixed{listing(2, QStringLiteral("divine")), listing(10000, QStringLiteral("chaos")),
                                     listing(1, QStringLiteral("divine"))};
        QVERIFY(!estimatePrice(mixed, rates).ok);
        QCOMPARE(PriceSummary::summarize(mixed, shortCurrencyName), QStringLiteral("1~2 디바인 · 10000 카오스"));
        rates.toBase.insert(QStringLiteral("divine"), 200);
        QVERIFY(estimatePrice(mixed, rates).ok);

        Estimate e;
        e.ok = true;
        e.value = 2010;
        e.lo = 2005;
        e.hi = 2015;
        const QString shown = formatEstimate(e, rates, 100);
        QVERIFY2(shown.startsWith(QStringLiteral("≈ 10.1 디바인  (10~10.1")), qPrintable(shown));
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
