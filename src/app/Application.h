// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/CheckedStore.h"
#include "core/Config.h"
#include "core/ItemParser.h"
#include "core/Keywords.h"
#include "core/TradeDb.h"

#include <QObject>
#include <QPointer>
#include <QSystemTrayIcon>
#include <QTimer>
#include <functional>
#include <memory>

class QAction;
class QMenu;

namespace pts {

class DataManager;
class HotkeyManager;
class MainPopup;
class TradeClient;

class Application : public QObject {
    Q_OBJECT
public:
    explicit Application(const QString& dataDir, QObject* parent = nullptr);
    ~Application() override;

    bool start(QString* error);

    // 개발용: 아이템 텍스트를 클립보드 거치지 않고 바로 띄움
    void devShowItem(const QString& text) { handleItemText(text, false); }
    QWidget* popupWidget() const;

private:
    // 트리거
    void onClipboardChanged();
    void onForegroundTick();
    void onHotkey(int id);

    // 동작
    void handleItemText(const QString& text, bool wiki);
    void openTrade();
    void priceCheck();
    void openWiki(const ParsedItem& it);
    void openSettings();
    void onRemember(const QString& statKey, const QString& top, bool on);

    // 데이터
    void loadGame(Game g);
    void updateGame(Game g, std::function<void()> then = nullptr);
    void fixLeagues();

    Game decideGame(const QString& text) const;
    Lang serverFor(Lang itemLang) const;
    QString currencyName(Game g, Lang l, const QString& id) const;
    ParseOptions parseOptions() const;

    void registerHotkeys();
    void unregisterHotkeys();
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
    std::unique_ptr<ItemParser> m_parser;
    MainPopup* m_popup = nullptr;
    QSystemTrayIcon* m_tray = nullptr;
    QMenu* m_menu = nullptr;
    QAction* m_pauseAction = nullptr;
    HotkeyManager* m_hotkeys = nullptr;

    QTimer m_fgTimer;
    QTimer m_expectTimer;
    enum class Expect { None, Search, Wiki };
    Expect m_expect = Expect::None;
    bool m_paused = false;
    bool m_customRun = false;
    bool m_hotkeysOn = false;
    bool m_unsupportedWarned = false;
    Game m_lastGame = Game::Poe1;
    quint64 m_priceToken = 0;
    QPoint m_popupPos;
};

} // namespace pts
