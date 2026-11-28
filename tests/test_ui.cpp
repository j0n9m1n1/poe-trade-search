// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/CheckedStore.h"
#include "core/Config.h"
#include "core/JsonUtil.h"
#include "core/Keywords.h"
#include "core/TradeDb.h"
#include "net/DataManager.h"
#include "net/RateLimiter.h"
#include "ui/FilterRow.h"
#include "ui/MainPopup.h"
#include "ui/SettingsDialog.h"
#include "ui/UiChoices.h"

#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace pts;

class TestAppFixes : public QObject {
    Q_OBJECT

private:
    Keywords m_kw;

private slots:
    void initTestCase()
    {
        Q_INIT_RESOURCE(resources);
        QString err;
        QVERIFY2(m_kw.load(QStringLiteral(":/data/parser.json"), &err), qPrintable(err));
    }

    // #1: 한도 도달 시 로컬 기록이 충분하면 가장 오래된 요청 만료까지만, 부족하면 전체 주기, restricted>0 면 제재 시간
    void rateLimiterWindowWait()
    {
        const auto P = RateLimiter::Policy::Search;
        const QHash<QByteArray, QByteArray> hdrFull{
            {"X-Rate-Limit-Rules", "Ip"},
            {"X-Rate-Limit-Ip", "5:10:60"},
            {"X-Rate-Limit-Ip-State", "5:10:0"},
        };

        // 1) 로컬 기록이 5개 모두 있고 첫 요청이 8초 전 -> 약 2.25초만 대기 (10초 전체 차단 X)
        RateLimiter rl;
        const qint64 base = 1000000;
        for (int i = 0; i < 5; ++i)
            rl.record(P, base + i * 2000); // 0s, 2s, 4s, 6s, 8s
        const qint64 now = base + 8000;
        rl.update(P, [&](const QByteArray& k) { return hdrFull.value(k); }, now);
        const qint64 w = rl.waitMs(P, now);
        QVERIFY2(w >= 2000 && w <= 2500, qPrintable(QStringLiteral("expected ~2250ms, got %1").arg(w)));

        // 2) 로컬 기록이 부족(1개뿐)인데 서버가 cur=5 보고 -> 보수적으로 전체 주기(10초) 대기
        RateLimiter rlSparse;
        rlSparse.record(P, now);
        rlSparse.update(P, [&](const QByteArray& k) { return hdrFull.value(k); }, now);
        QVERIFY(rlSparse.waitMs(P, now) >= 9500);

        // 3) 서버 restricted=60 보고 -> 로컬 기록과 무관하게 60초 차단
        const QHash<QByteArray, QByteArray> hdrRestricted{
            {"X-Rate-Limit-Rules", "Ip"},
            {"X-Rate-Limit-Ip", "5:10:60"},
            {"X-Rate-Limit-Ip-State", "6:10:60"},
        };
        rl.update(P, [&](const QByteArray& k) { return hdrRestricted.value(k); }, now);
        QVERIFY(rl.waitMs(P, now) >= 59000);
    }

    // #9: matchers.ndjson 이 없어도 matchers.tried 가 있으면 매 시작마다 stale 로 보지 않음
    void dataManagerMatchersTried()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        DataManager dm(tmp.path(), nullptr);
        const QString dir = dm.cacheDir(Game::Poe1, Lang::Ko);
        QVERIFY(QDir().mkpath(dir));
        QVERIFY(json::writeRaw(QDir(dir).filePath(QStringLiteral("stats.json")), QByteArrayLiteral("{\"result\":[]}")));
        QVERIFY(json::writeRaw(QDir(dir).filePath(QStringLiteral("updated.txt")),
                               QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toUtf8()));
        QVERIFY(dm.isStale(Game::Poe1, 7));

        QVERIFY(json::writeRaw(QDir(dir).filePath(QStringLiteral("matchers.tried")), QByteArrayLiteral("1")));
        QVERIFY(!dm.isStale(Game::Poe1, 7));
    }

    // #3, #12, #16: SettingsDialog 스크롤 영역, onlineleague 포함, 타락 콤보, 버튼 재활성화
    void settingsDialogFixes()
    {
        Config cfg = Config::defaults();
        cfg.tradeStatus = QStringLiteral("onlineleague");
        cfg.autoSelectCorrupt = QStringLiteral("no");
        GameDb dbs[2];
        CheckedStore chk;
        SettingsDialog dlg(cfg, dbs, &chk, {}, QStringLiteral("vtest"), QStringLiteral("지금 확인"));

        // 기본 탭이 QScrollArea 로 감싸여 있어야 함
        QVERIFY(!dlg.findChildren<QScrollArea*>().isEmpty());

        const Config out = dlg.result();
        QCOMPARE(out.tradeStatus, QStringLiteral("onlineleague"));
        QCOMPARE(out.autoSelectCorrupt, QStringLiteral("no"));

        // 업데이트 버튼 클릭 -> 비활성화 -> 완료 알림 -> 재활성화
        QPushButton* appBtn = nullptr;
        QPushButton* poe1Btn = nullptr;
        for (QPushButton* b : dlg.findChildren<QPushButton*>()) {
            if (b->text() == QStringLiteral("지금 확인"))
                appBtn = b;
            else if (b->text() == QStringLiteral("PoE1 데이터 갱신"))
                poe1Btn = b;
        }
        QVERIFY(appBtn != nullptr);
        QVERIFY(poe1Btn != nullptr);
        appBtn->click();
        QVERIFY(!appBtn->isEnabled());
        dlg.setAppUpdateFinished(QStringLiteral("지금 확인"));
        QVERIFY(appBtn->isEnabled());
        QCOMPARE(appBtn->text(), QStringLiteral("지금 확인"));

        poe1Btn->click();
        QVERIFY(!poe1Btn->isEnabled());
        dlg.setDataUpdateFinished(0);
        QVERIFY(poe1Btn->isEnabled());
    }

    // 일괄 교환 기본값: 쌓이는 화폐는 교환 시세, 안 쌓이는 템(초대장 등)은 일반 검색
    void exchangeDefaultOnlyForStackable()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile f(dir.filePath(QStringLiteral("static.json")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray("{\"result\":[{\"id\":\"Currency\",\"label\":\"화폐\",\"entries\":[{\"id\":\"chaos\",\"text\":\"카오스 오브\"},{\"id\":\"divine\",\"text\":\"신성한 오브\"},{\"id\":\"alch\",\"text\":\"연금술의 오브\"}]},{\"id\":\"Maps\",\"label\":\"지도\",\"entries\":[{\"id\":\"screaming-invitation\",\"text\":\"절규하는 초대\"}]}]}"));
        f.close();
        QFile sf(dir.filePath(QStringLiteral("stats.json")));
        QVERIFY(sf.open(QIODevice::WriteOnly));
        sf.write(QByteArray("{\"result\":[{\"id\":\"explicit\",\"label\":\"Explicit\",\"entries\":[{\"id\":\"explicit.stat_1\",\"text\":\"+# to maximum Life\",\"type\":\"explicit\"}]}]}"));
        sf.close();
        GameDb gdb;
        QString err;
        QVERIFY2(gdb[Lang::Ko].load(dir.path(), &err), qPrintable(err));
        QVERIFY(gdb[Lang::Ko].staticById(QStringLiteral("chaos")));
        MainPopup popup(m_kw);
        Config cfg = Config::defaults();

        ParsedItem inv;
        inv.game = Game::Poe1;
        inv.lang = Lang::Ko;
        inv.rarityId = QStringLiteral("normal");
        inv.type = inv.rawType = QStringLiteral("절규하는 초대");
        inv.exchangeId = QStringLiteral("screaming-invitation");
        popup.showItem(inv, &gdb, cfg, Lang::Ko);
        QVERIFY(popup.exchangeWant().isEmpty());

        ParsedItem cur = inv;
        cur.rarityId = QStringLiteral("currency");
        cur.type = cur.rawType = QStringLiteral("연금술의 오브");
        cur.exchangeId = QStringLiteral("alch");
        cur.stackSize = 3;
        popup.showItem(cur, &gdb, cfg, Lang::Ko);
        QCOMPARE(popup.exchangeWant(), QStringLiteral("chaos"));
    }

    // 떠 있는 팝업에 옵션 많은 아이템을 띄우면 창이 커져서 옵션 영역이 최소 높이로 눌리지 않아야 함
    void popupGrowsForManyRows()
    {
        MainPopup popup(m_kw);
        GameDb gdb;
        Config cfg = Config::defaults();
        ParsedItem small;
        small.game = Game::Poe1;
        small.lang = Lang::Ko;
        small.rarityId = QStringLiteral("rare");
        small.rarityText = QStringLiteral("희귀");
        small.name = QStringLiteral("작은 아이템");
        small.type = QStringLiteral("반지");
        small.rawType = small.type;
        popup.showItem(small, &gdb, cfg, Lang::Ko);
        popup.fitToAvailableHeight(1000);
        popup.show();
        QVERIFY(QTest::qWaitForWindowExposed(&popup));
        QCoreApplication::processEvents();
        const int before = popup.height();

        ParsedItem big = small;
        big.name = QStringLiteral("옵션 많은 아이템");
        for (int i = 0; i < 10; ++i) {
            ParsedMod m;
            m.raw = m.text = m.shown = QStringLiteral("검색할 수 없는 옵션 %1").arg(i);
            m.unmatched = true;
            m.lockedOff = true;
            big.mods.push_back(m);
        }
        // Application 처럼 다시 띄우되, 창 크기는 예약된 다시 맞춤에 맡김
        popup.showItem(big, &gdb, cfg, Lang::Ko);
        QTest::qWait(50);
        QScrollArea* scroll = nullptr;
        for (QScrollArea* a : popup.findChildren<QScrollArea*>()) {
            if (a->maximumHeight() < QWIDGETSIZE_MAX)
                scroll = a;
        }
        QVERIFY(scroll);
        QVERIFY2(popup.height() > before, qPrintable(QStringLiteral("height %1 -> %2").arg(before).arg(popup.height())));
        QVERIFY2(scroll->height() >= scroll->maximumHeight() - 2,
                 qPrintable(QStringLiteral("scroll %1 / max %2").arg(scroll->height()).arg(scroll->maximumHeight())));
    }

    // #3, #9, #13, #15: MainPopup onlineleague 유지, syncConfig, 반짝이 콤보 폭, stale 클릭 재조회
    void mainPopupFixes()
    {
        MainPopup popup(m_kw);
        ParsedItem it;
        it.game = Game::Poe1;
        it.lang = Lang::Ko;
        it.rarityId = QStringLiteral("unique");
        it.rarityFilter = QStringLiteral("uniquefoil");
        it.rarityText = QStringLiteral("고유");
        it.name = QStringLiteral("니미스");
        it.type = QStringLiteral("토파즈 반지");
        it.rawType = it.type;

        Config cfg = Config::defaults();
        cfg.tradeStatus = QStringLiteral("onlineleague");
        GameDb gdb;
        popup.showItem(it, &gdb, cfg, Lang::Ko);

        // #3: onlineleague 가 securable 로 초기화되지 않고 유지돼야 함
        QCOMPARE(popup.tradeStatus(), QStringLiteral("onlineleague"));

        // #13: 희귀도 콤보 폭이 내용(고유 (반짝이 버전))을 수용해야 함
        QComboBox* rarityCombo = nullptr;
        for (QComboBox* c : popup.findChildren<QComboBox*>()) {
            if (c->findData(QStringLiteral("uniquefoil")) >= 0) {
                rarityCombo = c;
                break;
            }
        }
        QVERIFY(rarityCombo != nullptr);
        QVERIFY2(rarityCombo->width() >= rarityCombo->sizeHint().width(),
                 qPrintable(QStringLiteral("width=%1 < sizeHint=%2").arg(rarityCombo->width()).arg(rarityCombo->sizeHint().width())));

        // #9: syncConfig 가 서버·판매방식·가격화폐 변경을 반영해야 함
        cfg.tradeStatus = QStringLiteral("available");
        cfg.priceCurrency = QStringLiteral("divine");
        QVERIFY(popup.syncConfig(cfg, Lang::En));
        QCOMPARE(popup.server(), Lang::En);
        QCOMPARE(popup.tradeStatus(), QStringLiteral("available"));
        QCOMPARE(popup.priceCurrency(), QStringLiteral("divine"));
        QVERIFY(!popup.syncConfig(cfg, Lang::En)); // 변경 없으면 false

        // #15: 추정가 표시 후 클릭 -> 가격 노트 복사, 필터 변경(stale) 후 클릭 -> 시세 재조회
        popup.priceDone(QStringLiteral("≈ 13 디바인"), QStringLiteral("10/10 검색"), QString(), 13, QStringLiteral("divine"));
        QLabel* priceLbl = nullptr;
        for (QLabel* l : popup.findChildren<QLabel*>()) {
            if (l->text().contains(QStringLiteral("13 디바인"))) {
                priceLbl = l;
                break;
            }
        }
        QVERIFY(priceLbl != nullptr);

        QSignalSpy copySpy(&popup, &MainPopup::copyPriceNoteRequested);
        QSignalSpy refreshSpy(&popup, &MainPopup::priceCheckRequested);
        QMouseEvent click(QEvent::MouseButtonPress, QPointF(4, 4), QPointF(4, 4), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(priceLbl, &click);
        QCOMPARE(copySpy.count(), 1);
        QCOMPARE(refreshSpy.count(), 0);

        // 판매 방식 콤보를 바꿔 markStale() 유발
        QComboBox* statusCombo = nullptr;
        for (QComboBox* c : popup.findChildren<QComboBox*>()) {
            if (c->findData(QStringLiteral("onlineleague")) >= 0) {
                statusCombo = c;
                break;
            }
        }
        QVERIFY(statusCombo != nullptr);
        statusCombo->setCurrentIndex(0);
        QVERIFY(priceLbl->toolTip().contains(QStringLiteral("다시 조회")));

        QCoreApplication::sendEvent(priceLbl, &click);
        QCOMPARE(copySpy.count(), 1);
        QCOMPARE(refreshSpy.count(), 1);
    }
};

QTEST_MAIN(TestAppFixes)
#include "test_ui.moc"
