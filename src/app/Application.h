// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/CheckedStore.h"
#include "core/Config.h"
#include "core/ItemParser.h"
#include "core/Keywords.h"
#include "core/PriceEstimate.h"
#include "core/TradeDb.h"
#include "net/TradeClient.h"
#include "net/Updater.h"

#include <QObject>
#include <QPointer>
#include <QSystemTrayIcon>
#include <QTimer>
#include <functional>
#include <memory>
#include <optional>

class QAction;
class QMenu;

namespace pts {

class DataManager;
class HotkeyManager;
class IconCache;
class MainPopup;
class TradeClient;

namespace platform {
enum class GameClickTarget;
}

class Application : public QObject {
    Q_OBJECT
public:
    explicit Application(const QString& dataDir, QObject* parent = nullptr);
    ~Application() override;

    bool start(QString* error);

    // 개발용: 아이템 텍스트를 클립보드 거치지 않고 바로 띄움
    void devShowItem(const QString& text) { handleItemText(text); }
    QWidget* popupWidget() const;

private:
    // 트리거
    void onClipboardChanged();
    void onForegroundTick();
    void onHotkey(int id);

    // 동작
    void handleItemText(const QString& text);
    void showPopup();  // 커서 근처(설정)에 띄우고 바깥 클릭 감시 시작
    void reopenLast(); // 실수로 닫았을 때
    void watchPopupClicks();
    void checkOutsideClick();
    void onOutsideClick(platform::GameClickTarget target, quint64 popupSeq);
    void openTrade();
    void priceCheck();
    void loadMoreListings();
    void updateListingPageStatus(const QString& status = {});
    void appendPriceListings(Game game, Lang server, const Rates& rates, const QVector<Listing>& listings);
    void copyPriceNote(const QString& note);
    void openSettings();

    // 프로그램 업데이트 (GitHub Release)
    void checkForUpdate(bool manual);
    void installUpdate();
    void applyDownloadedUpdate();

    // 환율 (거래소 일괄 교환 API 로 계산, 30분 캐시)
    void ensureRates(Game g, Lang server, const QString& league, std::function<void()> then);
    Rates ratesFor(Game g, const QString& league) const;

    // 아이콘
    static QString iconKey(const ParsedItem& it);
    void showIconFor(const ParsedItem& it);
    void learnIcon(const ParsedItem& it, const QVector<Listing>& listings);
    void onRemember(const QString& statKey, const QString& top, bool on);

    // 데이터
    void loadGame(Game g);
    void updateGame(Game g, std::function<void()> then = nullptr);
    void fixLeagues();

    Game decideGame(const QString& text) const;
    Lang serverFor(Lang itemLang) const;
    QString currencyName(Game g, Lang l, const QString& id) const;
    // 화폐 아이콘 받아서 팝업에 등록 (가격 칸/추정가에 이름 대신 아이콘)
    void loadCurrencyIcons(Game g, Lang l, const QStringList& ids);
    ParseOptions parseOptions() const;

    void registerHotkeys();
    void unregisterHotkeys();
    void updateEscHotkey(bool gameForeground);
    void syncPopupLeague();
    void applyConfig();
    void saveConfig();
    void restart();
    void notify(const QString& msg, QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Information);
    void showPopupMessage(const QString& title, const QString& msg);

    QString m_dataDir;
    QString m_cfgPath;
    Config m_cfg;
    bool m_freshConfig[2] = {false, false}; // 새 설정이면 리그를 현재 리그로 한 번 맞춤
    Keywords m_kw;
    GameDb m_dbs[2];
    CheckedStore m_checked;

    TradeClient* m_client = nullptr;
    DataManager* m_data = nullptr;
    IconCache* m_icons = nullptr;
    Updater* m_updater = nullptr;
    QTimer m_updateTimer;
    Updater::Release m_newRelease; // 찾은 새 버전
    bool m_updateNoticeShown = false; // 마지막 트레이 알림이 새 버전 알림
    QAction* m_updateAction = nullptr; // 트레이 "업데이트 확인/설치"
    QAction* m_elevatedRestartAction = nullptr;
    // 같은 조건 재조회 캐시 (1분). 키 = 게임/서버/리그/개수/쿼리
    struct CachedPrice {
        qint64 at = 0;
        TradeClient::PriceProgress result;
    };
    QHash<QString, CachedPrice> m_priceCache;
    struct ListingPages {
        Game game;
        Lang server;
        Rates rates;
        TradeClient::SearchResult search;
        int next = 0;
        int loaded = 0;
        bool loading = false;
    };
    std::optional<ListingPages> m_listingPages;
    quint64 m_priceFilterRevision = 0;
    QTimer m_priceRefreshTimer;
    QString m_newTag, m_newPage;
    QString m_downloadedZip;
    std::unique_ptr<ItemParser> m_parser;
    MainPopup* m_popup = nullptr;
    QSystemTrayIcon* m_tray = nullptr;
    QMenu* m_menu = nullptr;
    HotkeyManager* m_hotkeys = nullptr;

    QTimer m_fgTimer;
    QTimer m_expectTimer;
    QTimer m_clickTimer; // Windows 이벤트 등록 실패 시 폴링
    bool m_mouseWasDown = false;
    enum class Expect { None, Search };
    Expect m_expect = Expect::None;
    bool m_paused = false;
    bool m_customRun = false;
    bool m_ignoreClipboard = false;
    QString m_selfClipboardText;
    qint64 m_selfClipboardAt = 0;
    bool m_hotkeysOn = false;
    bool m_warnedGameInputPrivileges = false;
    bool m_unsupportedWarned = false;
    Game m_lastGame = Game::Poe1;
    quint64 m_priceToken = 0;
    quint64 m_openTradeToken = 0;
    quint64 m_popupSeq = 0;
    QPoint m_popupPos;

    struct RateState {
        Rates rates;
        bool loading = false;
        QVector<std::function<void()>> waiters;
    };
    QHash<QString, RateState> m_rates; // "poe1|League"
};

} // namespace pts
