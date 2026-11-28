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
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalSpy>
#include <QToolButton>
#include <QTemporaryDir>
#include <QTableWidget>
#include <QWheelEvent>
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

        // GitHub 뱃지 버튼만 있고, 라이선스 문구 라벨은 없음 (README 에만)
        auto* gh = dlg.findChild<QToolButton*>(QStringLiteral("githubLink"));
        QVERIFY(gh);
        QVERIFY(!gh->icon().isNull());
        QCOMPARE(gh->text(), QStringLiteral("GitHub"));
        QVERIFY(!dlg.findChild<QLabel*>(QStringLiteral("aboutText")));
        if (qEnvironmentVariableIsSet("PTS_SHOT_DIR")) {
            dlg.resize(660, 900);
            dlg.show();
            QTest::qWait(100);
            for (QScrollArea* a : dlg.findChildren<QScrollArea*>())
                a->ensureWidgetVisible(gh);
            QTest::qWait(50);
            dlg.grab().save(qEnvironmentVariable("PTS_SHOT_DIR") + QStringLiteral("/settings_about.png"));
        }
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

    // 영향 콤보 항목 앞에 배지와 같은 문양 아이콘이 붙어야 함
    void influenceComboIcons()
    {
        MainPopup popup(m_kw);
        const auto sources = popup.influenceIconSources();
        QCOMPARE(sources.size(), 6);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString png = dir.filePath(QStringLiteral("s.png"));
        QPixmap pm(27, 27);
        pm.fill(Qt::red);
        QVERIFY(pm.save(png));
        popup.setBadgeIcon(QStringLiteral("symbol:") + sources[0].first, png);
        int combos = 0;
        for (QComboBox* c : popup.findChildren<QComboBox*>()) {
            if (c->count() < 2 || c->itemData(0).toInt() != -1 || c->itemText(0) != QStringLiteral("영향 없음"))
                continue;
            ++combos;
            QVERIFY(c->itemIcon(0).isNull());
            QVERIFY(!c->itemIcon(1).isNull());
            QVERIFY(c->itemIcon(2).isNull());
        }
        QCOMPARE(combos, 2);
    }

    // 코드 점검(v0.0.48): 값 완화를 바꿔도 손으로 바꾼 체크는 유지, 일반/교환 줄은 따로, 새 아이템은 "더 불러오기" 상태를 물려받지 않음
    void auditPopupFixes()
    {
        MainPopup popup(m_kw);
        GameDb gdb;
        Config cfg = Config::defaults();
        ParsedItem it;
        it.game = Game::Poe1;
        it.lang = Lang::Ko;
        it.rarityId = QStringLiteral("rare");
        it.rarityText = QStringLiteral("희귀");
        it.name = QStringLiteral("점검 반지");
        it.type = QStringLiteral("반지");
        it.rawType = it.type;
        for (int i = 0; i < 2; ++i) {
            ParsedMod m;
            m.raw = m.text = m.shown = QStringLiteral("옵션 %1 +50").arg(i);
            m.statKey = QStringLiteral("stat_%1").arg(900000 + i);
            ModCandidate c;
            c.group = QStringLiteral("explicit");
            c.label = QStringLiteral("일반");
            c.id = QStringLiteral("explicit.") + m.statKey;
            m.cands.push_back(c);
            m.value = 50;
            m.checked = true;
            it.mods.push_back(m);
        }
        popup.showItem(it, &gdb, cfg, Lang::Ko);
        auto rows = popup.findChildren<FilterRow*>();
        QVERIFY(rows.size() >= 2);
        FilterRow* row = nullptr;
        for (FilterRow* r : rows) {
            ParsedMod m;
            r->apply(m);
            if (r->isCheckable() && m.checked) {
                row = r;
                break;
            }
        }
        QVERIFY(row);
        row->setChecked(false);
        QComboBox* range = nullptr;
        for (QComboBox* c : popup.findChildren<QComboBox*>()) {
            if (c->findText(QStringLiteral("일치")) >= 0 && c->count() >= 2)
                range = c;
        }
        QVERIFY(range);
        range->setCurrentIndex(range->currentIndex() == 0 ? 1 : 0);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); // 지운 이전 줄 정리
        int checkedRows = 0, uncheckedRows = 0;
        for (FilterRow* r : popup.findChildren<FilterRow*>()) {
            ParsedMod m;
            r->apply(m);
            if (!r->isCheckable())
                continue;
            (m.checked ? checkedRows : uncheckedRows) += 1;
        }
        QCOMPARE(uncheckedRows, 1); // 값 완화 변경 전 끈 줄이 다시 켜지면 안 됨
        QCOMPARE(checkedRows, 1);

        // 가격·판매자가 같아도 일반/교환 줄은 묶지 않음, 완전히 같은 줄은 ×2 로 묶음
        popup.priceColumns({QStringLiteral("가격"), QStringLiteral("구분"), QStringLiteral("등록"), QStringLiteral("판매자")});
        popup.priceAddRow({QStringLiteral("10 카오스"), QStringLiteral("일반"), QStringLiteral("1분"), QStringLiteral("seller")}, false, 1, {}, 10, QStringLiteral("chaos"));
        popup.priceAddRow({QStringLiteral("10 카오스"), QStringLiteral("교환"), QStringLiteral("1분"), QStringLiteral("seller")}, false, 1, {}, 10, QStringLiteral("chaos"));
        popup.priceAddRow({QStringLiteral("10 카오스"), QStringLiteral("교환"), QStringLiteral("2분"), QStringLiteral("seller")}, false, 1, {}, 10, QStringLiteral("chaos"));
        QTableWidget* table = popup.findChild<QTableWidget*>();
        QVERIFY(table);
        QCOMPARE(table->rowCount(), 2);

        // 더 불러올 매물이 있던 상태에서 새 아이템을 띄우면 휠로 "더 불러오기"가 나가면 안 됨
        popup.pricePageStatus(true, 20, 100);
        popup.showItem(it, &gdb, cfg, Lang::Ko);
        QSignalSpy more(&popup, &MainPopup::moreListingsRequested);
        QWheelEvent wheel(QPointF(5, 5), QPointF(5, 5), QPoint(0, -120), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(table->viewport(), &wheel);
        QCOMPARE(more.count(), 0);
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
