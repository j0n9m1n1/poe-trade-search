// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Application.h"

#include "core/PriceSummary.h"
#include "core/QueryBuilder.h"
#include "core/SearchDefaults.h"
#include "net/DataManager.h"
#include "net/Endpoints.h"
#include "net/IconCache.h"
#include "net/TradeClient.h"
#include "net/Updater.h"
#include "platform/HotkeyManager.h"
#include "platform/Platform.h"
#include "ui/MainPopup.h"
#include "ui/SettingsDialog.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QScreen>
#include <QUrlQuery>

namespace pts {

namespace {
constexpr int kEscId = 99;
constexpr int kShortcutBase = 100;
const QString kRun = QStringLiteral("{run}");
const QString kEnter = QStringLiteral("{enter}");
const QString kPause = QStringLiteral("{pause}");
const QString kRestart = QStringLiteral("{restart}");
const QString kLink = QStringLiteral("{link}");
} // namespace

Application::Application(const QString& dataDir, QObject* parent) : QObject(parent), m_dataDir(dataDir)
{
}

QWidget* Application::popupWidget() const
{
    return m_popup;
}

Application::~Application()
{
    platform::stopWatchingOutsideMousePress();
    if (m_hotkeys)
        m_hotkeys->clear();
    delete m_popup;
    delete m_menu;
}

bool Application::start(QString* error)
{
    QDir().mkpath(m_dataDir);

    // ---- 설정 (원본 Config.txt 그대로 읽힘)
    m_cfgPath = QDir(m_dataDir).filePath(QStringLiteral("Config.txt"));
    if (QFileInfo::exists(m_cfgPath)) {
        QString e;
        if (!m_cfg.load(m_cfgPath, &e)) {
            QMessageBox::warning(nullptr, QStringLiteral("설정 오류"),
                                 QStringLiteral("Config.txt를 읽지 못해 기본값으로 시작합니다.\n%1").arg(e));
            m_cfg = Config::defaults();
        } else if (m_cfg.migrated) {
            saveConfig();
        }
    } else {
        m_cfg = Config::defaults();
        m_freshConfig[0] = m_freshConfig[1] = true;
        saveConfig();
    }
    m_checked.load(QDir(m_dataDir).filePath(QStringLiteral("Checked.txt")));
    if (!QFileInfo::exists(m_checked.path()))
        m_checked.save();

    // ---- 키워드 (데이터 폴더에 parser.json 있으면 그걸 우선)
    const QString userParser = QDir(m_dataDir).filePath(QStringLiteral("parser.json"));
    QString e;
    if (!(QFileInfo::exists(userParser) && m_kw.load(userParser, &e)) &&
        !m_kw.load(QStringLiteral(":/data/parser.json"), &e)) {
        if (error)
            *error = QStringLiteral("parser.json을 읽지 못했습니다: ") + e;
        return false;
    }

    m_client = new TradeClient(this);
    m_data = new DataManager(m_dataDir, m_client, this);
    m_icons = new IconCache(QDir(m_dataDir).filePath(QStringLiteral("cache/icons")), this);
    m_updater = new Updater(QStringLiteral(PTS_UPDATE_REPO), QDir(m_dataDir).filePath(QStringLiteral("update")), this);
    m_parser = std::make_unique<ItemParser>(m_kw, m_dbs, &m_checked);
    m_hotkeys = new HotkeyManager(this);
    connect(m_hotkeys, &HotkeyManager::activated, this, &Application::onHotkey);
    connect(m_client, &TradeClient::throttled, this, [this](int s) {
        if (m_listingPages && m_listingPages->loading) {
            updateListingPageStatus(QStringLiteral("요청 제한 · %1초 대기").arg(s));
        } else if (m_priceToken != 0 && m_popup && m_popup->isVisible()) {
            m_popup->priceStatus(QStringLiteral("요청 제한 때문에 %1초 대기 중...").arg(s));
        }
    });

    // ---- 팝업
    m_popup = new MainPopup(m_kw);
    m_priceRefreshTimer.setSingleShot(true);
    m_priceRefreshTimer.setTimerType(Qt::PreciseTimer);
    m_priceRefreshTimer.setInterval(800);
    connect(&m_priceRefreshTimer, &QTimer::timeout, this, &Application::priceCheck);
    connect(m_popup, &MainPopup::openTradeRequested, this, &Application::openTrade);
    connect(m_popup, &MainPopup::priceCheckRequested, this, &Application::priceCheck);
    connect(m_popup, &MainPopup::moreListingsRequested, this, &Application::loadMoreListings);
    connect(m_popup, &MainPopup::priceFiltersChanged, this, [this]() {
        ++m_priceFilterRevision;
        // 진행 중 조회는 항상 취소 (예전 조건 결과가 새 조건 결과처럼 덮어쓰지 않게)
        const bool initialLoading = !m_listingPages && m_priceToken != 0;
        if (m_priceToken != 0) {
            m_client->cancel(m_priceToken);
            m_priceToken = 0;
        }
        // 자동 재검색이 켜져 있으면 0.8초 뒤 다시 조회, 꺼져 있으면 안내만
        if (m_cfg.autoPriceRefresh && m_popup->isVisible()) {
            m_priceRefreshTimer.start();
        } else if (initialLoading) {
            m_popup->priceIdle(QStringLiteral("조건이 변경되었습니다. 클릭하면 다시 조회합니다."));
            return;
        }
        if (!m_listingPages)
            return;
        m_popup->pricePageStatus(false, m_listingPages->loaded, m_listingPages->search.total);
        m_listingPages.reset();
    });
    connect(m_popup, &MainPopup::settingsRequested, this, &Application::openSettings);
    connect(m_popup, &MainPopup::historyRequested, this, &Application::navigateHistory);
    connect(m_popup, &MainPopup::leagueRequested, this, [this](Game game, const QString& league) {
        if (m_cfg.leagueFor(game) == league)
            return;
        m_cfg.setLeague(game, league);
        saveConfig();
        syncPopupLeague();
    });
    connect(m_popup, &MainPopup::rememberToggled, this, &Application::onRemember);
    connect(m_popup, &MainPopup::copyPriceNoteRequested, this, &Application::copyPriceNote);
    connect(m_popup, &MainPopup::popupHidden, this, [this]() {
        m_priceRefreshTimer.stop();
        ++m_popupSeq;
        m_popupPos = m_popup->pos();
        platform::stopWatchingOutsideMousePress();
        m_clickTimer.stop();
        m_client->cancel(m_priceToken);
        m_priceToken = 0;
        if (m_listingPages && m_listingPages->loading) {
            m_listingPages->loading = false;
            updateListingPageStatus();
        }
        m_client->cancel(m_openTradeToken);
        m_openTradeToken = 0;
        if (m_hotkeys->has(kEscId))
            m_hotkeys->remove(kEscId);
    });
    m_clickTimer.setInterval(30);
    connect(&m_clickTimer, &QTimer::timeout, this, &Application::checkOutsideClick);

    // ---- 트레이
    const QIcon appIcon(QStringLiteral(":/icons/app.png"));
    QApplication::setWindowIcon(appIcon);
    m_tray = new QSystemTrayIcon(appIcon, this);
    m_tray->setToolTip(QStringLiteral("Poe Trade Search %1").arg(QStringLiteral(PTS_VERSION)));
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger)
            reopenLast();
    });
    // 트레이 메뉴는 최소한만. 데이터 갱신 / 프로그램 업데이트는 설정 창에
    // 마지막 창 다시 열기는 트레이 아이콘 왼쪽 클릭 / {Last} 단축키로
    m_menu = new QMenu;
    m_menu->addAction(QStringLiteral("설정"), this, &Application::openSettings);
    m_elevatedRestartAction = m_menu->addAction(QStringLiteral("관리자 권한으로 다시 실행"), this, [this]() {
        m_elevatedRestartAction->setEnabled(false);
        const QString args = QStringLiteral("--wait-shutdown --restart-data-dir \"%1\"").arg(QDir(m_dataDir).absolutePath());
        QString error;
        if (platform::startElevated(QCoreApplication::applicationFilePath(), args, &error)) {
            qApp->quit();
            return;
        }
        m_elevatedRestartAction->setEnabled(true);
        if (!error.isEmpty())
            notify(QStringLiteral("관리자 권한 재실행 실패 (Windows 오류 %1)").arg(error), QSystemTrayIcon::Warning);
    });
    m_elevatedRestartAction->setVisible(false);
    // 업데이트: 새 버전을 모르면 확인(있으면 바로 설치), 알면 설치
    m_updateAction = m_menu->addAction(QStringLiteral("업데이트 확인 (현재 %1)").arg(Updater::currentVersion()));
    connect(m_updateAction, &QAction::triggered, this, [this]() {
        if (m_newTag.isEmpty())
            checkForUpdate(true);
        else
            installUpdate();
    });
    m_menu->addSeparator();
    m_menu->addAction(QStringLiteral("종료"), qApp, &QApplication::quit);
    // 새 버전 알림을 누르면 바로 설치 (설치 못 하는 환경이면 릴리즈 페이지)
    connect(m_tray, &QSystemTrayIcon::messageClicked, this, [this]() {
        if (m_updateNoticeShown && !m_newTag.isEmpty())
            installUpdate();
    });
    m_tray->setContextMenu(m_menu);
    m_tray->show();

    // ---- 데이터
    for (Game g : {Game::Poe1, Game::Poe2}) {
        if (m_data->hasCache(g))
            loadGame(g);
    }
    for (Game g : {Game::Poe1, Game::Poe2}) {
        if (!m_data->hasCache(g) || (m_cfg.checkUpdates && m_data->isStale(g, m_cfg.dataMaxAgeDays)))
            updateGame(g);
    }
    connect(m_data, &DataManager::progress, this, [this](const QString& m) { m_tray->setToolTip(m); });

    // ---- 트리거
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &Application::onClipboardChanged);
    m_fgTimer.setInterval(500);
    connect(&m_fgTimer, &QTimer::timeout, this, &Application::onForegroundTick);
    m_fgTimer.start();
    m_expectTimer.setSingleShot(true);
    connect(&m_expectTimer, &QTimer::timeout, this, [this]() { m_expect = Expect::None; });

    applyConfig();

    // ---- 프로그램 업데이트: 시작 15초 뒤 한 번, 이후 6시간마다
    m_updateTimer.setSingleShot(true);
    connect(&m_updateTimer, &QTimer::timeout, this, [this]() {
        checkForUpdate(false);
        m_updateTimer.start(6 * 60 * 60 * 1000);
    });
    m_updateTimer.start(15000);
    const QStringList args = QCoreApplication::arguments();
    if (args.contains(QStringLiteral("--updated")))
        return true; // 업데이트 뒤 재실행: 알림 없이 조용히 (시작 안내도 생략)
    if (args.contains(QStringLiteral("--update-failed"))) {
        notify(QStringLiteral("업데이트에 실패해 기존 버전으로 실행했습니다. 로그: %1").arg(QDir::toNativeSeparators(m_updater->logPath())),
               QSystemTrayIcon::Warning);
        return true;
    }

    notify(QStringLiteral("프로그램을 시작합니다.\n사용법: 게임에서 아이템에 마우스를 올리고 Ctrl + C\n종료: 트레이 아이콘 우클릭 → 종료"));
    return true;
}

// ------------------------------------------------------------ 데이터

void Application::loadGame(Game g)
{
    QString err;
    GameDb fresh;
    if (m_data->load(g, &fresh, m_kw, &err)) {
        m_dbs[gi(g)] = fresh;
        fixLeagues();
    } else {
        qWarning() << "data load failed" << gameKey(g) << err;
    }
}

void Application::updateGame(Game g, std::function<void()> then)
{
    m_data->update(g, [this, g, then](bool ok, const QString& err) {
        if (ok) {
            loadGame(g);
            m_tray->setToolTip(QStringLiteral("Poe Trade Search %1").arg(QStringLiteral(PTS_VERSION)));
        } else {
            notify(QStringLiteral("%1 거래소 데이터를 업데이트하지 못했습니다.\n%2")
                       .arg(g == Game::Poe1 ? QStringLiteral("PoE1") : QStringLiteral("PoE2"), err),
                   QSystemTrayIcon::Warning);
        }
        if (m_settingsDlg)
            m_settingsDlg->setDataUpdateFinished(gi(g));
        if (then)
            then();
    });
}

void Application::fixLeagues()
{
    // 설정의 리그가 목록에 없으면(지난 리그) 현재 첫 리그로
    bool changed = false;
    for (Game g : {Game::Poe1, Game::Poe2}) {
        const GameDb& db = m_dbs[gi(g)];
        const TradeDb& d = db.loaded(Lang::En) ? db[Lang::En] : db[Lang::Ko];
        if (d.leagues().isEmpty())
            continue;
        const QString cur = m_cfg.leagueFor(g);
        bool found = false;
        for (const auto& l : d.leagues())
            found = found || l.id == cur;
        const bool fresh = m_freshConfig[gi(g)];
        m_freshConfig[gi(g)] = false;
        if (!found || (fresh && cur == QStringLiteral("Standard"))) {
            m_cfg.setLeague(g, d.leagues().first().id);
            changed = true;
            if (!found)
                notify(QStringLiteral("설정된 리그 '%1'을(를) 찾을 수 없어 '%2'(으)로 변경했습니다.").arg(cur, d.leagues().first().id));
        }
    }
    if (changed) {
        saveConfig();
        syncPopupLeague();
    }
}

// ------------------------------------------------------------ 트리거

Game Application::decideGame(const QString& text) const
{
    if (m_cfg.game == QStringLiteral("poe1"))
        return Game::Poe1;
    if (m_cfg.game == QStringLiteral("poe2"))
        return Game::Poe2;
    const platform::GameWindow w = platform::foregroundGame();
    if (w.found && !w.title.isEmpty() && w.title != QStringLiteral("stub"))
        return w.game;
    return m_parser->guessGame(text, w.found ? w.game : m_lastGame);
}

Lang Application::serverFor(Lang itemLang) const
{
    if (m_cfg.serverType == 1)
        return Lang::Ko;
    if (m_cfg.serverType == 2)
        return Lang::En;
    return itemLang;
}

void Application::onClipboardChanged()
{
    if (m_ignoreClipboard)
        return;
    const QString text = QGuiApplication::clipboard()->text();
    if (!m_selfClipboardText.isEmpty() && text == m_selfClipboardText) {
        // 앱이 복사한 가격 노트가 클립보드 감시를 다시 트리거하지 않게 무시
        if (QDateTime::currentMSecsSinceEpoch() - m_selfClipboardAt < 2000)
            return;
        m_selfClipboardText.clear();
    }
    if (m_expect != Expect::None) {
        if (text.trimmed().isEmpty())
            return; // 우리가 비운 것
        m_expect = Expect::None;
        m_expectTimer.stop();
        handleItemText(text);
        return;
    }
    if (m_paused || m_customRun)
        return;
    if (!platform::foregroundGame().foreground)
        return;
    handleItemText(text);
}

void Application::onForegroundTick()
{
    const platform::GameWindow w = platform::foregroundGame();
    if (w.found)
        m_lastGame = w.game;
    if (w.foreground) {
        if (!m_hotkeysOn) {
            registerHotkeys();
            const bool needsElevation = !platform::isAdministrator() && platform::gameRunsElevated();
            m_elevatedRestartAction->setVisible(needsElevation);
            if (needsElevation && !m_warnedGameInputPrivileges) {
                m_warnedGameInputPrivileges = true;
                notify(QStringLiteral("게임은 관리자 권한, PoeTradeSearch는 일반 권한으로 실행 중\n"
                                      "게임 화면 클릭 닫기와 단축키를 쓰려면 트레이 메뉴에서 ‘관리자 권한으로 다시 실행’ 선택"),
                       QSystemTrayIcon::Warning);
            }
        }
    } else if (m_hotkeysOn) {
        unregisterHotkeys();
    }
    updateEscHotkey(w.foreground);
}

void Application::updateEscHotkey(bool gameForeground)
{
    // ESC: 팝업 떠 있고 게임이 앞에 있을 때만 가로챔 (콤보 펼침/대화상자 열려 있으면 넘겨줌)
    const bool modalOrCombo = QApplication::activePopupWidget() || QApplication::activeModalWidget();
    const bool wantEsc = gameForeground && m_popup->isVisible() && !modalOrCombo;
    if (wantEsc && !m_hotkeys->has(kEscId)) {
        if (!m_hotkeys->add(kEscId, 0, 0x1B))
            qWarning() << "ESC hotkey register failed";
    } else if (!wantEsc && m_hotkeys->has(kEscId)) {
        m_hotkeys->remove(kEscId);
    }
}

void Application::registerHotkeys()
{
    m_hotkeysOn = true;
    for (int i = 0; i < m_cfg.shortcuts.size(); ++i) {
        const Shortcut& s = m_cfg.shortcuts[i];
        if (s.keycode > 0 && !s.value.trimmed().isEmpty()) {
            const bool ok = m_hotkeys->add(kShortcutBase + i, s.modifiers, s.keycode);
#ifdef Q_OS_WIN
            if (!ok)
                qWarning() << "hotkey register failed (다른 프로그램이 이미 사용 중일 수 있습니다)" << s.keycode
                           << s.modifiers << s.value;
#else
            Q_UNUSED(ok);
#endif
        }
    }
}

void Application::unregisterHotkeys()
{
    for (int i = 0; i < m_cfg.shortcuts.size(); ++i)
        m_hotkeys->remove(kShortcutBase + i);
    m_hotkeysOn = false;
}

void Application::onHotkey(int id)
{
    if (id == kEscId) {
        if (QApplication::activePopupWidget() || QApplication::activeModalWidget())
            return;
        m_popup->hide();
        return;
    }
    const int idx = id - kShortcutBase;
    if (idx < 0 || idx >= m_cfg.shortcuts.size())
        return;
    const QString value = m_cfg.shortcuts[idx].value.trimmed();
    const QString lower = value.toLower();

    if (lower.startsWith(kPause)) {
        m_paused = !m_paused;
        notify(m_paused ? QStringLiteral("일시 정지했습니다. 같은 단축키를 한 번 더 누르면 다시 시작합니다.")
                        : QStringLiteral("다시 시작했습니다."));
        return;
    }
    if (lower.startsWith(kRestart)) {
        restart();
        return;
    }
    if (lower.startsWith(QStringLiteral("{last}"))) {
        reopenLast();
        return;
    }
    if (m_paused)
        return;
    if (lower.startsWith(kRun)) {
        m_selfClipboardText.clear();
        m_expect = Expect::Search;
        QGuiApplication::clipboard()->clear();
        m_expectTimer.start(1500);
        // {Run} 은 Ctrl+Alt+C(고급 설명)로 복사해서 등급까지 가져옴
        platform::sendCopy(true);
        return;
    }
    if (lower.startsWith(kEnter) || lower.startsWith(QStringLiteral("^") + kEnter)) {
        // 채팅 명령: "{Enter}/hideout{Enter}", "^{Enter}고마워{Enter}" (^ = 마지막 귓말 답장)
        // 한 번 누르면 명령 하나만 보냄 (GGG 1키 1동작 원칙)
        const bool reply = lower.startsWith(QLatin1Char('^'));
        QString body = value.mid(reply ? 1 + kEnter.size() : kEnter.size());
        const int end = body.toLower().indexOf(kEnter);
        if (end >= 0)
            body = body.left(end);
        body = body.trimmed();
        if (!body.isEmpty() || reply)
            platform::sendChat(body, reply);
        return;
    }
    if (lower.startsWith(kLink)) {
        QString url = value.mid(kLink.size());
        const int end = url.toLower().indexOf(kLink);
        if (end >= 0)
            url = url.left(end);
        if (!url.trimmed().isEmpty())
            QDesktopServices::openUrl(QUrl(url.trimmed()));
        return;
    }
    if (!m_unsupportedWarned) {
        m_unsupportedWarned = true;
        notify(QStringLiteral("지원하지 않는 단축키 명령입니다: %1").arg(value), QSystemTrayIcon::Warning);
    }
}

// ------------------------------------------------------------ 동작

ParseOptions Application::parseOptions() const
{
    ParseOptions o;
    o.statRangePct = m_cfg.searchStatRange;
    o.autoSelectPseudo = m_cfg.autoSelectPseudo;
    o.autoCheckTotalRes = m_cfg.autoCheckTotalres;
    o.autoCheckUnique = m_cfg.autoCheckUnique;
    o.autoSelectCorrupt = m_cfg.autoSelectCorrupt.toLower();
    o.autoSelectByType = m_cfg.byTypeList();
    return o;
}

void Application::handleItemText(const QString& text)
{
    if (!m_parser->looksLikeItem(text))
        return;
    const Game g = decideGame(text);
    m_lastGame = g;
    if (auto item = parseItem(text, g)) {
        m_history.record(g, text.trimmed());
        showParsedItem(*item, false);
    }
}

std::optional<ParsedItem> Application::parseItem(const QString& text, Game g)
{
    QString err;
    auto it = m_parser->parse(text, g, parseOptions(), &err);
    if (!it) {
        if (err == QStringLiteral("data not loaded")) {
            if (!m_data->isUpdating(g)) {
                notify(QStringLiteral("거래소 데이터를 받는 중입니다. 완료되면 다시 검색해 주세요 (Alt+D)."));
                updateGame(g);
            } else {
                notify(QStringLiteral("거래소 데이터를 받는 중입니다. 잠시 기다려 주세요."));
            }
        } else {
            qWarning() << "parse failed" << err;
        }
    }
    return it;
}

void Application::navigateHistory(int offset)
{
    const auto* entry = m_history.step(offset);
    if (!entry)
        return;
    if (auto item = parseItem(entry->text, entry->game)) {
        m_lastGame = item->game;
        showParsedItem(*item, true);
    } else {
        m_history.step(-offset);
    }
}

void Application::showParsedItem(const ParsedItem& it, bool fromHistory)
{
    const Game g = it.game;
    m_priceRefreshTimer.stop();
    m_client->cancel(m_priceToken); // 앞 아이템 시세 조회 중이면 끊음
    m_priceToken = 0;
    m_listingPages.reset();
    m_client->cancel(m_openTradeToken);
    m_openTradeToken = 0;
    // 화폐 콤보/가격 칸 아이콘 (받아둔 파일이면 바로 붙음)
    loadCurrencyIcons(g, it.lang, {QStringLiteral("chaos"), QStringLiteral("divine"), QStringLiteral("exalted")});
    m_popup->showItem(it, &m_dbs[gi(g)], m_cfg, serverFor(it.lang));
    showIconFor(it);
    // 영향/엘드리치/분열 배지: 게임 이름판 문양 먼저, 못 받으면 오브 아이콘 (영향 엑잘티드 오브, 섬뜩한 불씨/영액, 분열의 오브)
    for (const auto& src : m_popup->badgeIconSources()) {
        const QString symbol = src.first, id = src.second;
        const StaticEntry* s = m_dbs[gi(g)][it.lang].staticById(id);
        const QString orb = s ? s->image : QString();
        auto fallback = [this, id, orb]() {
            if (!orb.isEmpty())
                m_icons->get(orb, [this, id](const QString& path) { m_popup->setBadgeIcon(id, path); });
        };
        const QString url = endpoints::itemSymbol(symbol);
        if (url.isEmpty()) {
            fallback();
            continue;
        }
        m_icons->get(url, [this, symbol, fallback](const QString& path) {
            if (path.isEmpty()) {
                qWarning() << "item symbol download failed" << symbol;
                fallback();
                return;
            }
            m_popup->setBadgeSymbol(symbol, path);
        });
    }
    m_popup->setHistoryAvailability(m_history.canStep(-1), m_history.canStep(1));
    showPopup(fromHistory);

    if (fromHistory || m_cfg.autoPriceCheck)
        priceCheck();
}

void Application::showPopup(bool keepPosition)
{
    const bool centered = !keepPosition && m_cfg.popupPosition == QStringLiteral("center");
    const bool nearCursor = !keepPosition && m_cfg.popupPosition == QStringLiteral("cursor");
    const QPoint cursor = QCursor::pos();
    QPoint position = m_popup->isVisible() ? m_popup->pos() : m_popupPos;
    QScreen* screen = nullptr;
    if (centered || nearCursor)
        screen = QGuiApplication::screenAt(cursor);
    else if (keepPosition || !position.isNull())
        screen = QGuiApplication::screenAt(position);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect area = screen->availableGeometry();
    m_popup->fitToAvailableHeight(qMax(1, area.height()));
    const QSize size = m_popup->size();
    if (centered) {
        position = area.center() - QPoint(size.width() / 2, size.height() / 2);
    } else if (nearCursor) {
        // 커서 오른쪽에 공간이 없으면 왼쪽에 표시
        int x = cursor.x() + 24;
        if (x + size.width() > area.right() + 1)
            x = cursor.x() - 24 - size.width();
        position = QPoint(x, cursor.y() - size.height() / 3);
    } else if ((!keepPosition && position.isNull()) || !area.contains(position)) {
        position = area.topLeft() + QPoint(30, 30);
    }
    const int x = qBound(area.left(), position.x(), qMax(area.left(), area.right() + 1 - size.width()));
    const int y = qBound(area.top(), position.y(), qMax(area.top(), area.bottom() + 1 - size.height()));
    m_popup->move(x, y);
    const bool gameFg = platform::foregroundGame().foreground;
    if (gameFg) {
        platform::setNoActivate(m_popup, true);
        m_popup->show();
        m_popup->raise();
    } else {
        // 게임이 앞에 없는 상태(트레이 클릭으로 다시 열기 등)에서는 활성화해 ESC 닫기가 바로 동작하게 함
        m_popup->show();
        platform::activateWindow(m_popup);
        m_popup->raise();
    }
    updateEscHotkey(gameFg);
    watchPopupClicks();
}

void Application::reopenLast()
{
    if (!m_popup->hasItem()) {
        notify(QStringLiteral("다시 열 창이 없습니다."));
        return;
    }
    showPopup();
}

void Application::watchPopupClicks()
{
    const quint64 seq = ++m_popupSeq;
    platform::stopWatchingOutsideMousePress();
    m_clickTimer.stop();
    if (!m_cfg.closeOnOutsideClick)
        return;
    if (!platform::watchOutsideMousePress(m_popup, [this, seq](platform::GameClickTarget target) {
            if (QApplication::activePopupWidget() || QApplication::activeModalWidget())
                return;
            QMetaObject::invokeMethod(this, [this, target, seq]() { onOutsideClick(target, seq); }, Qt::QueuedConnection);
        })) {
        m_mouseWasDown = platform::mouseButtonDown();
        m_clickTimer.start();
    }
}

void Application::checkOutsideClick()
{
    const bool down = platform::mouseButtonDown();
    const bool pressed = down && !m_mouseWasDown;
    m_mouseWasDown = down;
    if (pressed && !m_popup->frameGeometry().contains(QCursor::pos()))
        onOutsideClick(platform::cursorGameClickTarget(), m_popupSeq);
}

void Application::onOutsideClick(platform::GameClickTarget target, quint64 popupSeq)
{
    using platform::GameClickTarget;
    if (popupSeq != m_popupSeq || !m_popup->isVisible() ||
        QApplication::activePopupWidget() || QApplication::activeModalWidget())
        return;
    if (target == GameClickTarget::Game) {
        qInfo() << "outside game click -> close";
        m_popup->hide();
    } else if (target == GameClickTarget::GameArea) {
        // 게임과 겹치는 창은 클릭 뒤 게임이 활성화됐을 때만 닫음
        QTimer::singleShot(150, this, [this, popupSeq]() {
            if (platform::foregroundGame().foreground)
                onOutsideClick(platform::GameClickTarget::Game, popupSeq);
        });
    }
}

void Application::showPopupMessage(const QString& title, const QString& msg)
{
    QMessageBox box(QMessageBox::Information, title, msg, QMessageBox::Ok, m_popup->isVisible() ? m_popup : nullptr);
    box.setWindowFlags(box.windowFlags() | Qt::WindowStaysOnTopHint);
    box.exec();
    if (m_popup->isVisible()) {
        const bool gameFg = platform::foregroundGame().foreground;
        if (!gameFg)
            platform::activateWindow(m_popup);
        updateEscHotkey(gameFg);
    }
}

void Application::openTrade()
{
    if (!m_popup->hasItem())
        return;
    m_client->cancel(m_openTradeToken);
    m_openTradeToken = m_client->newToken();
    const quint64 token = m_openTradeToken;

    const ParsedItem it = m_popup->collectItem();
    const Game g = it.game;
    const QString league = m_cfg.leagueFor(g);
    const Lang server = m_popup->server();

    const QString want = m_popup->exchangeWant();
    if (!want.isEmpty()) {
        m_popup->setSearchBusy(true);
        // 시세 조회와 같은 방향: 화폐를 내고 이 아이템을 받는 판매 매물
        m_client->exchange(
            g, server, league, want, it.exchangeId,
            [this, token, g, server, league](const TradeClient::ExchangeResult& r) {
                if (token != m_openTradeToken)
                    return;
                m_openTradeToken = 0;
                m_popup->setSearchBusy(false);
                if (!r.error.isEmpty() || r.id.isEmpty()) {
                    showPopupMessage(QStringLiteral("검색 실패"), r.error);
                    return;
                }
                QDesktopServices::openUrl(endpoints::webExchange(g, server, league, r.id));
                m_popup->hide();
            },
            token);
        return;
    }

    RequestContext ctx;
    ctx.server = server;
    ctx.league = m_cfg.leagueFor(g);
    ctx.status = m_popup->tradeStatus();
    ctx.indexed = m_popup->indexed();
    ctx.priceOption = m_popup->priceCurrency();
    ctx.forBrowser = true;
    QString warn;
    const SearchRequest req = makeRequest(it, m_popup->collectState(), m_kw, &m_dbs[gi(g)], ctx, &warn);
    if (!warn.isEmpty())
        notify(warn, QSystemTrayIcon::Warning);
    const QJsonObject q = QueryBuilder::build(req, &m_dbs[gi(g)][req.lang]);

    if (m_cfg.serverRedirect) {
        m_openTradeToken = 0;
        QDesktopServices::openUrl(endpoints::redirectSearch(g, req.lang, league, QJsonDocument(q).toJson(QJsonDocument::Compact)));
        m_popup->hide();
        return;
    }
    m_popup->setSearchBusy(true);
    m_client->search(
        g, req.lang, league, q,
        [this, token, g, lang = req.lang, league](const TradeClient::SearchResult& r) {
            if (token != m_openTradeToken)
                return;
            m_openTradeToken = 0;
            m_popup->setSearchBusy(false);
            if (!r.error.isEmpty()) {
                showPopupMessage(QStringLiteral("검색 실패"),
                                 QStringLiteral("거래소 검색에 실패했습니다: %1\n한국/글로벌 거래소에 접속되는지 확인한 뒤 다시 시도해 주세요.").arg(r.error));
                return;
            }
            QDesktopServices::openUrl(endpoints::webSearch(g, lang, league, r.id));
            m_popup->hide();
        },
        token);
}

QString Application::currencyName(Game g, Lang l, const QString& id) const
{
    const TradeDb& db = m_dbs[gi(g)][l];
    if (const StaticEntry* s = db.staticById(id))
        return s->text;
    return id;
}

void Application::loadCurrencyIcons(Game g, Lang l, const QStringList& ids)
{
    const TradeDb& db = m_dbs[gi(g)][l];
    for (const QString& id : ids) {
        const StaticEntry* s = db.staticById(id);
        if (!s || s->image.isEmpty())
            continue;
        const QString shortName = shortCurrencyName(id), fullName = s->text;
        m_icons->get(s->image, [this, shortName, fullName](const QString& path) {
            if (path.isEmpty())
                return;
            m_popup->setUnitIcon(shortName, path);
            m_popup->setUnitIcon(fullName, path);
        });
    }
}

// ------------------------------------------------------------ 프로그램 업데이트

QString Application::appUpdateButtonText() const
{
    if (m_newTag.isEmpty())
        return QStringLiteral("지금 확인");
    return Updater::canSelfInstall() ? QStringLiteral("%1 설치").arg(m_newTag)
                                     : QStringLiteral("%1 릴리즈 페이지 열기").arg(m_newTag);
}

void Application::checkForUpdate(bool manual)
{
    if (!manual && m_cfg.appUpdate == QStringLiteral("off"))
        return;
    if (!m_updater->enabled() || m_updater->busy()) {
        if (manual && m_settingsDlg)
            m_settingsDlg->setAppUpdateFinished(appUpdateButtonText());
        return;
    }
    m_updater->check([this, manual](const Updater::Release* rel, const QString& error) {
        if (!error.isEmpty()) {
            qWarning() << "update check failed" << error;
            if (manual)
                notify(QStringLiteral("업데이트를 확인하지 못했습니다: %1").arg(error), QSystemTrayIcon::Warning);
            if (m_settingsDlg)
                m_settingsDlg->setAppUpdateFinished(appUpdateButtonText());
            return;
        }
        if (!rel) {
            if (manual)
                notify(QStringLiteral("최신 버전입니다 (%1).").arg(Updater::currentVersion()));
            if (m_settingsDlg)
                m_settingsDlg->setAppUpdateFinished(appUpdateButtonText());
            return;
        }
        const bool firstSeen = m_newTag != rel->tag;
        m_newRelease = *rel;
        m_newTag = rel->tag;
        m_newPage = rel->page;
        QString why;
        const bool canInstall = Updater::canSelfInstall(&why);
        if (m_updateAction)
            m_updateAction->setText(canInstall ? QStringLiteral("업데이트 설치 (%1 → %2)").arg(Updater::currentVersion(), rel->tag)
                                               : QStringLiteral("새 버전 %1 릴리즈 페이지 열기").arg(rel->tag));
        if (canInstall && (manual || m_cfg.appUpdate == QStringLiteral("auto"))) {
            if (manual)
                notify(QStringLiteral("새 버전(%1)을 받아서 설치합니다.").arg(rel->tag));
            installUpdate();
            return;
        }
        if (manual) {
            notify(QStringLiteral("새 버전(%1)이 있습니다 (%2). 릴리즈 페이지를 엽니다.").arg(rel->tag, why));
            installUpdate();
            return;
        }
        if (firstSeen) {
            notify(canInstall ? QStringLiteral("새 버전(%1)이 있습니다. 이 알림을 누르거나 트레이 메뉴 / 설정 > 프로그램 업데이트에서 설치하세요.").arg(rel->tag)
                              : QStringLiteral("새 버전(%1)이 있습니다 (%2). 이 알림을 누르면 릴리즈 페이지를 엽니다.").arg(rel->tag, why));
            m_updateNoticeShown = true;
        }
        if (m_settingsDlg)
            m_settingsDlg->setAppUpdateFinished(appUpdateButtonText());
    });
}

void Application::installUpdate()
{
    if (m_newTag.isEmpty()) {
        if (m_settingsDlg)
            m_settingsDlg->setAppUpdateFinished(appUpdateButtonText());
        return;
    }
    QString why;
    if (!Updater::canSelfInstall(&why)) {
        QDesktopServices::openUrl(QUrl(m_newPage));
        if (m_settingsDlg)
            m_settingsDlg->setAppUpdateFinished(appUpdateButtonText());
        return;
    }
    if (!m_downloadedZip.isEmpty()) {
        applyDownloadedUpdate();
        return;
    }
    const Updater::Release rel = m_newRelease;
    m_updater->download(rel, [this](const QString& zip, const QString& error) {
        if (!error.isEmpty()) {
            notify(QStringLiteral("업데이트를 받지 못했습니다: %1").arg(error), QSystemTrayIcon::Warning);
            if (m_settingsDlg)
                m_settingsDlg->setAppUpdateFinished(appUpdateButtonText());
            return;
        }
        m_downloadedZip = zip;
        if (m_settingsDlg)
            m_settingsDlg->setAppUpdateFinished(appUpdateButtonText());
        applyDownloadedUpdate();
    });
}

void Application::applyDownloadedUpdate()
{
    // 팝업이나 설정 같은 모달 대화상자가 열려 있으면 닫힐 때까지 기다렸다가 교체
    if (m_popup->isVisible() || QApplication::activeModalWidget()) {
        QTimer::singleShot(2000, this, &Application::applyDownloadedUpdate);
        return;
    }
    // 받고 나면 알림 없이 바로 설치 후 재실행 (실패할 때만 알림)
    QString err;
    if (!m_updater->install(m_downloadedZip, &err, QDir(m_dataDir).absolutePath())) {
        notify(QStringLiteral("업데이트를 설치하지 못했습니다: %1").arg(err), QSystemTrayIcon::Warning);
        return;
    }
    m_hotkeys->clear();
    qApp->quit();
}

// ------------------------------------------------------------ 환율

QString Application::ratesKey(Game g, Lang server, const QString& league)
{
    return QStringLiteral("%1|%2|%3").arg(gameKey(g), langKey(server), league);
}

Rates Application::ratesFor(Game g, Lang server, const QString& league) const
{
    return m_rates.value(ratesKey(g, server, league)).rates;
}

void Application::ensureRates(Game g, Lang server, const QString& league, std::function<void()> then)
{
    const QString key = ratesKey(g, server, league);
    RateState& rs = m_rates[key];
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (rs.rates.fresh() || (rs.retryAfterMs > 0 && now < rs.retryAfterMs)) {
        then();
        return;
    }
    rs.waiters.push_back(std::move(then));
    if (rs.loading)
        return;
    rs.loading = true;

    // 기준 화폐로 디바인(과 보조 화폐)을 사는 일괄 교환 시세의 중앙값
    const QString base = Rates::baseFor(g);
    const QStringList targets = {QStringLiteral("divine"),
                                 g == Game::Poe1 ? QStringLiteral("exalted") : QStringLiteral("chaos")};
    auto fetched = std::make_shared<QHash<QString, double>>();
    auto remaining = std::make_shared<int>(targets.size());
    const int targetCount = static_cast<int>(targets.size());
    for (const QString& t : targets) {
        m_client->exchange(g, server, league, base, t, [this, key, base, t, fetched, remaining, targetCount](const TradeClient::ExchangeResult& r) {
            const double ratio = medianRatio(r.listings); // 기준 화폐 몇 개 = t 1개
            if (ratio > 0)
                fetched->insert(t, ratio);
            if (--(*remaining) > 0)
                return;
            RateState& st = m_rates[key];
            st.rates.base = base;
            // 화폐별 마지막 성공 환율은 보존하고 새로 성공한 화폐만 갱신
            for (auto it = fetched->constBegin(); it != fetched->constEnd(); ++it)
                st.rates.toBase.insert(it.key(), it.value());
            if (fetched->size() == targetCount) {
                st.rates.at = QDateTime::currentDateTimeUtc();
                st.retryAfterMs = 0;
            } else {
                // 전체/부분 실패는 30분 캐시하지 않고 60초 뒤 재시도
                st.rates.at = QDateTime();
                st.retryAfterMs = QDateTime::currentMSecsSinceEpoch() + 60000;
            }
            st.loading = false;
            const auto waiters = st.waiters;
            st.waiters.clear();
            for (const auto& w : waiters)
                w();
        });
    }
}

// ------------------------------------------------------------ 시세

const Application::CachedPrice* Application::cachedPrice(const QString& key)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto c = m_priceCache.begin(); c != m_priceCache.end();) {
        if (now - c->at > 60000)
            c = m_priceCache.erase(c);
        else
            ++c;
    }
    const auto c = m_priceCache.constFind(key);
    return c != m_priceCache.constEnd() ? &c.value() : nullptr;
}

void Application::checkExchangePrice(Game g, Lang server, const QString& league, const QString& pay, const QString& item,
                                     std::function<void(const TradeClient::ExchangeResult&)> done, quint64 token)
{
    const QString key = QStringLiteral("exchange|%1|%2|%3|%4|%5").arg(gi(g)).arg(li(server)).arg(league, pay, item);
    if (const auto* c = cachedPrice(key)) {
        done(std::get<TradeClient::ExchangeResult>(c->result));
        return;
    }
    m_client->exchange(g, server, league, pay, item, [this, key, done](const TradeClient::ExchangeResult& result) {
        if (result.error.isEmpty())
            m_priceCache.insert(key, {QDateTime::currentMSecsSinceEpoch(), result});
        done(result);
    }, token);
}

void Application::priceCheck()
{
    m_priceRefreshTimer.stop();
    if (!m_popup->hasItem())
        return;
    m_client->cancel(m_priceToken);
    m_listingPages.reset();
    m_priceToken = m_client->newToken();
    const quint64 token = m_priceToken;

    const Game g = m_popup->parsedItem().game;
    const QString league = m_cfg.leagueFor(g);
    const Lang server = m_popup->server();
    m_popup->priceBusy(QStringLiteral("시세 확인중..."));
    loadCurrencyIcons(g, server, {QStringLiteral("chaos"), QStringLiteral("divine"), QStringLiteral("exalted"),
                                  QStringLiteral("mirror")});

    // 환율 먼저 (30분에 한 번, 일괄 교환 요청 2개)
    ensureRates(g, server, league, [this, token, g, league, server]() {
        if (token != m_priceToken || !m_popup->hasItem())
            return;
        // 환율 대기 뒤 옵션(collectItem)과 속성(collectState)을 같은 시점에 읽음
        const ParsedItem it = m_popup->collectItem();
        const ItemFilterState state = m_popup->collectState();
        const quint64 filterRevision = m_priceFilterRevision;
        const Rates rates = ratesFor(g, server, league);
        const QString want = m_popup->exchangeWant();

        // ---- 일괄 교환: 화폐(want 콤보)를 내고 이 아이템을 받는 판매 매물 = 시장 가격 (APT 와 같은 방향)
        // 예전엔 아이템을 내고 화폐를 받는 방향이라 아이템을 "사겠다는" 매물만 나왔음 (초대 1건 vs 판매 13건)
        if (!want.isEmpty()) {
            const QString payName = currencyName(g, server, want);
            const QString itemName = currencyName(g, server, it.exchangeId);
            loadCurrencyIcons(g, server, {want});
            m_popup->priceStatus(QStringLiteral("교환 시세 확인중..."));
            checkExchangePrice(
                g, server, league, want, it.exchangeId,
                [this, token, filterRevision, payName, itemName, want, rates](const TradeClient::ExchangeResult& r) {
                    if (token != m_priceToken)
                        return;
                    m_priceToken = 0;
                    if (filterRevision != m_priceFilterRevision)
                        return;
                    if (!r.error.isEmpty()) {
                        m_popup->priceIdle(QStringLiteral("교환 시세를 가져오지 못했습니다: %1 (클릭하면 다시 시도합니다)").arg(r.error));
                        return;
                    }
                    m_popup->priceColumns({QStringLiteral("1개당"), QStringLiteral("환산"), QStringLiteral("재고"),
                                           QStringLiteral("등록"), QStringLiteral("판매자")});
                    // 싼 순서 (1개당 내는 화폐). 중앙값 절반도 안 되는 미끼 매물은 흐리게
                    QVector<Listing> rows = r.listings;
                    for (Listing& l : rows)
                        l.amount = l.payAmount / l.getAmount; // 아이템 1개당 내는 화폐 (TradeClient 가 0 인 건 걸러 줌)
                    std::stable_sort(rows.begin(), rows.end(),
                                     [](const Listing& a, const Listing& b) { return a.amount < b.amount; });
                    QVector<double> perOne;
                    for (const Listing& l : rows)
                        perOne << l.amount;
                    const double mid = perOne.isEmpty() ? 0 : perOne[perOne.size() / 2];
                    for (const Listing& l : rows) {
                        m_popup->priceAddRow({PriceSummary::formatAmount(l.amount) + QLatin1Char(' ') + payName,
                                              formatConversion(l.amount, want, rates), QString::number(l.stock),
                                              PriceSummary::ago(l.indexed), l.account},
                                             l.amount < mid * 0.5, l.online,
                                             QStringLiteral("%1 %2 → %3 %4")
                                                 .arg(PriceSummary::formatAmount(l.payAmount), payName,
                                                      PriceSummary::formatAmount(l.getAmount), itemName),
                                             l.amount, want);
                    }
                    QString summary = QStringLiteral("해당 교환 매물이 없습니다");
                    double estAmount = 0;
                    QString estCurrency;
                    if (!perOne.isEmpty()) {
                        // 미끼 뺀 가장 싼 5개의 중앙값 (환율 계산과 같은 방식)
                        const double med = medianRatio(rows);
                        estAmount = med;
                        estCurrency = want;
                        double conv = 0;
                        summary = QStringLiteral("1개 ≈ %1 %2").arg(PriceSummary::formatAmount(med), payName);
                        if (rates.convert(med, want, &conv) && want != rates.base)
                            summary += QStringLiteral(" (%1)").arg(formatValue(conv, rates));
                        summary += QStringLiteral(" · 매물 %1").arg(r.total);
                    }
                    m_popup->priceDone(summary, QStringLiteral("%1/%2 건").arg(r.listings.size()).arg(r.total),
                                       QString(), estAmount, estCurrency);
                },
                token);
            return;
        }

        // ---- 일반 시세
        RequestContext ctx;
        ctx.server = server;
        ctx.league = league;
        ctx.status = m_popup->tradeStatus();
        ctx.indexed = m_popup->indexed();
        ctx.priceMin = m_popup->priceMin();
        ctx.priceOption = m_popup->priceCurrency();
        ctx.collapse = m_popup->collapse();
        ctx.forBrowser = false;
        const SearchRequest req = makeRequest(it, state, m_kw, &m_dbs[gi(g)], ctx);
        const QJsonObject q = QueryBuilder::build(req, &m_dbs[gi(g)][req.lang]);
        const Lang lang = req.lang;

        // 가격 / 환산 / ilvl / 퀄 / DPS / 등록 / 판매자
        QVector<int> hidden;
        hidden << 3; // 퀄 열은 안 씀 (사용자 결정)
        if (!rates.valid() || rates.toBase.isEmpty())
            hidden << 1;
        if (!it.isWeapon)
            hidden << 4;
        if (!(it.isGear || it.isJewel || it.isFlask))
            hidden << 2;
        m_popup->priceColumns({QStringLiteral("가격"), QStringLiteral("환산"), QStringLiteral("ilvl"), QStringLiteral("퀄"),
                               QStringLiteral("DPS"), QStringLiteral("등록"), QStringLiteral("판매자")},
                              hidden);
        m_popup->priceStatus(QStringLiteral("시세 확인중..."));
        auto onProgress = [this, token, filterRevision, g, lang, rates](const TradeClient::PriceProgress& p) {
            if (token != m_priceToken || filterRevision != m_priceFilterRevision)
                return;
            appendPriceListings(g, lang, rates, p.listings);
            m_popup->priceStatus(QStringLiteral("시세 확인중... %1/%2").arg(p.fetched).arg(p.requested));
        };
        auto onDone = [this, token, filterRevision, g, lang, it, rates](const TradeClient::PriceProgress& all,
                                                                       const QString& error) {
            if (token != m_priceToken)
                return;
            m_priceToken = 0;
            if (filterRevision != m_priceFilterRevision)
                return;
            learnIcon(it, all.listings);
            if (!error.isEmpty() && all.listings.isEmpty()) {
                m_popup->priceIdle(QStringLiteral("시세를 조회하지 못했습니다: %1 (클릭하면 다시 시도합니다)").arg(error));
                return;
            }
            const QString dist =
                PriceSummary::summarize(all.listings, [this, g, lang](const QString& id) { return currencyName(g, lang, id); });
            QString summary;
            PriceTag estPrice;
            if (all.listings.isEmpty()) {
                summary = all.total == 0 ? QStringLiteral("해당 물품의 거래가 없습니다") : QStringLiteral("가격이 붙은 매물이 없습니다");
            } else {
                const Estimate e = estimatePrice(all.listings, rates);
                if (e.ok) {
                    summary = formatEstimate(e, rates, all.total);
                    estPrice = displayPrice(e.value, rates);
                } else {
                    summary = dist;
                }
            }
            const int loaded = static_cast<int>(all.listings.size());
            m_popup->priceDone(summary, QStringLiteral("%1/%2 검색").arg(loaded).arg(all.total),
                               dist.isEmpty() ? QString() : QStringLiteral("분포: ") + dist,
                               estPrice.amount, estPrice.currency);
            if (filterRevision == m_priceFilterRevision && !all.search.ids.isEmpty() && all.fetched < all.total) {
                m_listingPages = ListingPages{g, lang, rates, all.search, all.fetched, loaded};
                updateListingPageStatus(error.isEmpty() ? QString() : QStringLiteral("추가 조회 실패"));
            }
        };

        // 같은 조건을 1분 안에 다시 조회하면 요청 안 보내고 저장한 결과 (요청 제한 절약, APT 캐시와 같은 취지)
        const QString key = QStringLiteral("%1|%2|%3|%4|").arg(gi(g)).arg(static_cast<int>(lang)).arg(league).arg(m_popup->listCount()) +
                            QString::fromUtf8(QJsonDocument(q).toJson(QJsonDocument::Compact));
        if (const auto* c = cachedPrice(key)) {
            const auto& result = std::get<TradeClient::PriceProgress>(c->result);
            onProgress(result);
            onDone(result, QString());
            return;
        }
        m_client->priceCheck(
            g, lang, league, q, m_popup->listCount(), onProgress,
            [this, key, onDone](const TradeClient::PriceProgress& all, const QString& error) {
                if (error.isEmpty())
                    m_priceCache.insert(key, {QDateTime::currentMSecsSinceEpoch(), all});
                onDone(all, error);
            },
            token);
    });
}

void Application::loadMoreListings()
{
    if (!m_popup->isVisible() || !m_listingPages || m_listingPages->loading)
        return;
    auto& page = *m_listingPages;
    const QStringList ids = page.search.ids.mid(page.next, 10);
    if (ids.isEmpty())
        return;
    page.loading = true;
    updateListingPageStatus(QStringLiteral("추가 조회 중..."));
    m_priceToken = m_client->newToken();
    const quint64 token = m_priceToken;
    m_client->fetchListings(page.game, page.server, page.search.id, ids,
        [this, token, count = static_cast<int>(ids.size())](const QVector<Listing>& listings, const QString& error) {
            if (token != m_priceToken || !m_listingPages)
                return;
            m_priceToken = 0;
            auto& page = *m_listingPages;
            page.loading = false;
            if (!error.isEmpty()) {
                qWarning().noquote() << "listing page fetch failed:" << error;
                updateListingPageStatus(QStringLiteral("추가 조회 실패"));
                return;
            }
            appendPriceListings(page.game, page.server, page.rates, listings);
            page.next += count;
            page.loaded += static_cast<int>(listings.size());
            updateListingPageStatus();
        }, token);
}

void Application::updateListingPageStatus(const QString& status)
{
    const auto& page = *m_listingPages;
    const bool hasMore = !page.loading && page.next < page.search.ids.size();
    const bool reachedServerLimit = page.next == page.search.ids.size() && page.next < page.search.total;
    const QString message = status.isEmpty() && reachedServerLimit ? QStringLiteral("거래소 제공 매물 끝") : status;
    m_popup->pricePageStatus(hasMore, page.loaded, page.search.total, message);
}

void Application::appendPriceListings(Game game, Lang server, const Rates& rates, const QVector<Listing>& listings)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QStringList currencies;
    for (const Listing& l : listings) {
        if (!currencies.contains(l.currency))
            currencies << l.currency;
    }
    loadCurrencyIcons(game, server, currencies);
    for (const Listing& l : listings) {
        const bool stale = l.indexed.isValid() && l.indexed.daysTo(now) >= 3;
        QString tip = PriceSummary::listingTooltip(l);
        if (tip.isEmpty())
            tip = currencyName(game, server, l.currency);
        m_popup->priceAddRow({PriceSummary::formatAmount(l.amount) + QLatin1Char(' ') +
                                  shortCurrencyName(l.currency).replace(QLatin1Char('-'), QLatin1Char(' ')),
                              formatConversion(l.amount, l.currency, rates),
                              l.ilvl >= 0 ? QString::number(l.ilvl) : QString(),
                              l.quality >= 0 ? QString::number(l.quality) : QString(),
                              l.dps > 0 ? QString::number(l.dps, 'f', 1) : QString(),
                              PriceSummary::ago(l.indexed), l.account},
                             stale, l.online, tip, l.amount, l.currency);
    }
}

void Application::copyPriceNote(const QString& note)
{
    if (note.isEmpty())
        return;
    m_selfClipboardText = note;
    m_selfClipboardAt = QDateTime::currentMSecsSinceEpoch();
    m_ignoreClipboard = true;
    QGuiApplication::clipboard()->setText(note);
    m_ignoreClipboard = false;
    if (m_popup)
        m_popup->showCopyFeedback(note);
}

// ------------------------------------------------------------ 아이콘

QString Application::iconKey(const ParsedItem& it)
{
    // 고유는 이름별, 나머지는 베이스별 그림 (언어마다 이름이 달라서 언어도 키에 포함)
    const bool unique = it.rarityId == QStringLiteral("unique") && !it.name.isEmpty();
    return QStringLiteral("%1|%2|%3").arg(gameKey(it.game), langKey(it.lang),
                                          unique ? QStringLiteral("u:") + it.name + QLatin1Char('|') + it.type
                                                 : QStringLiteral("b:") + it.type + QLatin1Char('|') + it.disc);
}

void Application::showIconFor(const ParsedItem& it)
{
    QString url;
    if (!it.exchangeId.isEmpty()) {
        if (const StaticEntry* s = m_dbs[gi(it.game)][it.lang].staticById(it.exchangeId))
            url = s->image;
    }
    if (url.isEmpty())
        url = m_icons->urlFor(iconKey(it));
    if (url.isEmpty())
        return; // 처음 보는 아이템: 시세 조회하면 그때 배움
    const QString key = iconKey(it);
    m_icons->get(url, [this, key](const QString& path) {
        if (!path.isEmpty() && m_popup->hasItem() && iconKey(m_popup->collectItem()) == key)
            m_popup->setIcon(path);
    });
}

void Application::learnIcon(const ParsedItem& it, const QVector<Listing>& listings)
{
    const bool unique = it.rarityId == QStringLiteral("unique") && !it.name.isEmpty();
    for (const Listing& l : listings) {
        if (l.icon.isEmpty())
            continue;
        // 유형으로 검색 등으로 다른 베이스가 섞일 수 있으니 같은 아이템일 때만
        if (it.transfigured || l.baseType == it.type) {
            if (unique && l.itemName != it.name)
                continue;
            m_icons->remember(iconKey(it), l.icon);
            showIconFor(it);
            return;
        }
    }
}

void Application::onRemember(const QString& statKey, const QString& top, bool on)
{
    m_checked.toggle(statKey, top, on);
    m_checked.save();
}

void Application::openSettings()
{
    if (m_settingsDlg) {
        m_settingsDlg->raise();
        m_settingsDlg->activateWindow();
        return;
    }
    auto statText = [this](const QString& key) {
        for (Game g : {Game::Poe1, Game::Poe2}) {
            for (Lang l : {Lang::Ko, Lang::En}) {
                const TradeDb& db = m_dbs[gi(g)][l];
                if (!db.isLoaded())
                    continue;
                for (const QString& grp : {QStringLiteral("explicit"), QStringLiteral("pseudo")}) {
                    const int i = db.findStat(grp + QLatin1Char('.') + key);
                    if (i >= 0)
                        return db.stat(i).text;
                }
            }
        }
        return key;
    };
    QString ver = QStringLiteral("버전 %1").arg(QStringLiteral(PTS_VERSION));
    for (Game g : {Game::Poe1, Game::Poe2}) {
        const QDateTime t = m_data->updatedAt(g);
        ver += QStringLiteral("\n%1 데이터: %2").arg(g == Game::Poe1 ? QStringLiteral("PoE1") : QStringLiteral("PoE2"),
                                                   t.isValid() ? t.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                                                               : QStringLiteral("없음"));
    }
    ver += QStringLiteral("\n데이터 폴더: %1").arg(QDir::toNativeSeparators(m_dataDir));

    // 프로그램 업데이트 버튼: 새 버전을 알면 설치, 모르면 확인 (찾으면 바로 설치)
    SettingsDialog dlg(m_cfg, m_dbs, &m_checked, statText, ver, appUpdateButtonText(), m_popup->isVisible() ? m_popup : nullptr);
    m_settingsDlg = &dlg;
    connect(&dlg, &SettingsDialog::updateDataRequested, this,
            [this](int g) { updateGame(g == 0 ? Game::Poe1 : Game::Poe2); });
    connect(&dlg, &SettingsDialog::appUpdateRequested, this, [this]() {
        if (m_newTag.isEmpty())
            checkForUpdate(true);
        else
            installUpdate();
    });
    const int rc = dlg.exec();
    m_settingsDlg = nullptr;
    if (rc == QDialog::Accepted) {
        unregisterHotkeys();
        m_cfg = dlg.result();
        saveConfig();
        applyConfig();
    }
    if (m_popup->isVisible()) {
        const bool gameFg = platform::foregroundGame().foreground;
        if (!gameFg)
            platform::activateWindow(m_popup);
        updateEscHotkey(gameFg);
    }
}

void Application::resetPopupPrices()
{
    m_priceRefreshTimer.stop();
    m_client->cancel(m_priceToken);
    m_priceToken = 0;
    m_client->cancel(m_openTradeToken);
    m_openTradeToken = 0;
    m_popup->setSearchBusy(false);
    ++m_priceFilterRevision;
    m_listingPages.reset();
    m_popup->clearPrices();
    if (m_popup->isVisible() && m_cfg.autoPriceCheck)
        priceCheck();
}

bool Application::syncPopupLeague()
{
    if (!m_popup || !m_popup->hasItem())
        return false;
    const QString next = m_cfg.leagueFor(m_popup->parsedItem().game).trimmed();
    if (m_popup->leagueTitle() == next)
        return false;
    m_popup->setLeagueTitle(next);
    resetPopupPrices();
    return true;
}

void Application::applyConfig()
{
    if (!m_cfg.autoPriceRefresh)
        m_priceRefreshTimer.stop();
    m_client->setTimeoutSec(m_cfg.serverTimeout);
    m_customRun = !m_cfg.clipboardWatch; // Ctrl+C 감시 끔 ({Run} 단축키는 그대로 동작)
    if (m_hotkeysOn) {
        unregisterHotkeys();
        registerHotkeys();
    }
    const bool syncedCfg = m_popup && m_popup->hasItem()
                               ? m_popup->syncConfig(m_cfg, serverFor(m_popup->parsedItem().lang))
                               : false;
    const bool syncedLeague = syncPopupLeague();
    if (syncedCfg && !syncedLeague)
        resetPopupPrices();
    if (m_popup && m_popup->isVisible())
        watchPopupClicks();
}

void Application::saveConfig()
{
    QString e;
    if (!m_cfg.save(m_cfgPath, &e))
        qWarning() << "config save failed" << e;
}

void Application::restart()
{
    QProcess::startDetached(QCoreApplication::applicationFilePath(),
                            {QStringLiteral("--wait-shutdown"), QStringLiteral("--restart-data-dir"),
                             QDir(m_dataDir).absolutePath()});
    qApp->quit();
}

void Application::notify(const QString& msg, QSystemTrayIcon::MessageIcon icon)
{
    m_updateNoticeShown = false; // 마지막 알림이 새 버전 알림일 때만 눌러서 설치
    if (m_tray && QSystemTrayIcon::supportsMessages())
        m_tray->showMessage(QStringLiteral("Poe Trade Search"), msg, icon, 5000);
    qInfo().noquote() << msg;
}

} // namespace pts
