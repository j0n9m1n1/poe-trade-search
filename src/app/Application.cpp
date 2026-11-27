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
        if (m_popup && m_popup->isVisible())
            m_popup->priceIdle(QStringLiteral("요청 제한 때문에 %1초 대기 중...").arg(s));
    });

    // ---- 팝업
    m_popup = new MainPopup(m_kw);
    connect(m_popup, &MainPopup::openTradeRequested, this, &Application::openTrade);
    connect(m_popup, &MainPopup::priceCheckRequested, this, &Application::priceCheck);
    connect(m_popup, &MainPopup::settingsRequested, this, &Application::openSettings);
    connect(m_popup, &MainPopup::rememberToggled, this, &Application::onRemember);
    connect(m_popup, &MainPopup::popupHidden, this, [this]() {
        m_popupPos = m_popup->pos();
        m_clickTimer.stop();
        m_client->cancel(m_priceToken);
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

    QString hint = QStringLiteral("프로그램을 시작합니다.\n사용법: 게임에서 아이템에 마우스를 올리고 Ctrl + C\n종료: 트레이 아이콘 우클릭 → 종료");
    if (!platform::isAdministrator())
        hint += QStringLiteral("\n(PoE를 관리자 권한으로 실행했다면 이 프로그램도 관리자 권한으로 실행해야 단축키가 동작합니다)");
    notify(hint);
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
    if (changed)
        saveConfig();
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
    const QString text = QGuiApplication::clipboard()->text();
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
        if (!m_hotkeysOn)
            registerHotkeys();
    } else if (m_hotkeysOn) {
        unregisterHotkeys();
    }
    // ESC: 팝업 떠 있고 게임이 앞에 있을 때만 가로챔
    const bool wantEsc = w.foreground && m_popup->isVisible();
    if (wantEsc && !m_hotkeys->has(kEscId))
        m_hotkeys->add(kEscId, 0, 0x1B);
    else if (!wantEsc && m_hotkeys->has(kEscId))
        m_hotkeys->remove(kEscId);
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
        return;
    }

    m_client->cancel(m_priceToken); // 앞 아이템 시세 조회 중이면 끊음
    // 화폐 콤보/가격 칸 아이콘 (받아둔 파일이면 바로 붙음)
    loadCurrencyIcons(g, it->lang, {QStringLiteral("chaos"), QStringLiteral("divine"), QStringLiteral("exalted")});
    m_popup->showItem(*it, &m_dbs[gi(g)], m_cfg, serverFor(it->lang));
    showIconFor(*it);
    // 영향/엘드리치 배지 아이콘 (영향 엑잘티드 오브, 섬뜩한 불씨/영액)
    for (const QString& id : m_popup->badgeIconIds()) {
        const StaticEntry* s = m_dbs[gi(g)][it->lang].staticById(id);
        if (!s || s->image.isEmpty())
            continue;
        m_icons->get(s->image, [this, id](const QString& path) { m_popup->setBadgeIcon(id, path); });
    }
    showPopup();

    if (m_cfg.autoPriceCheck)
        priceCheck();
}

void Application::showPopup()
{
    m_popup->adjustSize();
    if (m_cfg.popupPosition == QStringLiteral("center")) {
        // 커서가 있는 화면(=보통 게임 화면)의 가운데
        QScreen* scr = QGuiApplication::screenAt(QCursor::pos());
        if (!scr)
            scr = QGuiApplication::primaryScreen();
        const QRect a = scr->availableGeometry();
        const QSize sz = m_popup->size() + QSize(16, 40); // 창 테두리/제목줄 여유
        const int x = qBound(a.left(), a.center().x() - sz.width() / 2, qMax(a.left(), a.right() - sz.width()));
        const int y = qBound(a.top(), a.center().y() - sz.height() / 2, qMax(a.top(), a.bottom() - sz.height()));
        m_popup->move(x, y);
    } else if (m_cfg.popupPosition == QStringLiteral("cursor")) {
        // 커서 오른쪽 (모자라면 왼쪽), 세로는 커서가 위쪽 1/3 쯤 오게. 화면 밖으로 안 나가게
        const QPoint c = QCursor::pos();
        QScreen* scr = QGuiApplication::screenAt(c);
        if (!scr)
            scr = QGuiApplication::primaryScreen();
        const QRect a = scr->availableGeometry();
        const QSize sz = m_popup->size() + QSize(16, 40); // 창 테두리/제목줄 여유
        int x = c.x() + 24;
        if (x + sz.width() > a.right())
            x = c.x() - 24 - sz.width();
        int y = c.y() - sz.height() / 3;
        x = qBound(a.left(), x, qMax(a.left(), a.right() - sz.width()));
        y = qBound(a.top(), y, qMax(a.top(), a.bottom() - sz.height()));
        m_popup->move(x, y);
    } else if (!m_popup->isVisible()) {
        if (m_popupPos.isNull())
            m_popupPos = QGuiApplication::primaryScreen()->availableGeometry().topLeft() + QPoint(30, 30);
        m_popup->move(m_popupPos);
    }
    platform::setNoActivate(m_popup, true);
    m_popup->show();
    m_popup->raise();
    m_shownAt.restart();
    m_mouseWasDown = platform::mouseButtonDown();
    m_clickTimer.start();
}

void Application::reopenLast()
{
    if (!m_popup->hasItem()) {
        notify(QStringLiteral("다시 열 창이 없습니다."));
        return;
    }
    showPopup();
}

void Application::checkOutsideClick()
{
    if (!m_popup->isVisible()) {
        m_clickTimer.stop();
        return;
    }
    const bool down = platform::mouseButtonDown();
    const bool pressed = down && !m_mouseWasDown; // 누르는 순간만
    m_mouseWasDown = down;
    if (!pressed || !m_cfg.closeOnOutsideClick)
        return;
    // 게임 화면을 눌렀을 때만 닫음. 창 모드에서 다른 프로그램/바탕화면을 누른 건 무시
    if (m_popup->frameGeometry().contains(QCursor::pos()))
        return;
    if (m_shownAt.elapsed() < 250) {
        qInfo() << "outside click ignored: just shown" << m_shownAt.elapsed() << "ms";
        return;
    }
    // 콤보 펼침/메시지 상자/설정 창이 떠 있으면 건드리지 않음
    if (QApplication::activePopupWidget() || QApplication::activeModalWidget()) {
        qInfo() << "outside click ignored: popup/modal open";
        return;
    }
    QString why;
    if (platform::cursorOverGame(&why)) {
        qInfo().noquote() << "outside click -> close (" << why << ")";
        m_popup->hide();
        return;
    }
    // 그래도 아니면: 잠깐 뒤 활성 창이 PoE 면 게임을 누른 것 (다른 프로그램을 눌렀으면 그 프로그램이 활성)
    // (underMouse 는 활성화 안 되는 창에서 안 풀릴 때가 있어서 커서 좌표로 확인)
    QTimer::singleShot(150, this, [this, why]() {
        const bool fg = platform::foregroundGame().foreground;
        const bool outside = !m_popup->frameGeometry().contains(QCursor::pos());
        qInfo().noquote() << "outside click (" << why << ") foregroundPoe=" << fg << " outside=" << outside;
        if (m_popup->isVisible() && fg && outside)
            m_popup->hide();
    });
}

void Application::showPopupMessage(const QString& title, const QString& msg)
{
    QMessageBox box(QMessageBox::Information, title, msg, QMessageBox::Ok, m_popup->isVisible() ? m_popup : nullptr);
    box.setWindowFlags(box.windowFlags() | Qt::WindowStaysOnTopHint);
    box.exec();
}

void Application::openTrade()
{
    if (!m_popup->hasItem())
        return;
    const ParsedItem it = m_popup->collectItem();
    const Game g = it.game;
    const QString league = m_cfg.leagueFor(g);
    const Lang server = m_popup->server();

    const QString want = m_popup->exchangeWant();
    if (!want.isEmpty()) {
        m_client->exchange(g, server, league, it.exchangeId, want,
                           [this, g, server, league](const TradeClient::ExchangeResult& r) {
                               if (!r.error.isEmpty() || r.id.isEmpty()) {
                                   showPopupMessage(QStringLiteral("검색 실패"), r.error);
                                   return;
                               }
                               QDesktopServices::openUrl(endpoints::webExchange(g, server, league, r.id));
                               m_popup->hide();
                           });
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
        QDesktopServices::openUrl(endpoints::redirectSearch(g, req.lang, league, QJsonDocument(q).toJson(QJsonDocument::Compact)));
        m_popup->hide();
        return;
    }
    m_client->search(g, req.lang, league, q, [this, g, lang = req.lang, league](const TradeClient::SearchResult& r) {
        if (!r.error.isEmpty()) {
            showPopupMessage(QStringLiteral("검색 실패"),
                             QStringLiteral("거래소 검색에 실패했습니다: %1\n한국/글로벌 거래소에 접속되는지 확인한 뒤 다시 시도해 주세요.").arg(r.error));
            return;
        }
        QDesktopServices::openUrl(endpoints::webSearch(g, lang, league, r.id));
        m_popup->hide();
    });
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

void Application::checkForUpdate(bool manual)
{
    if (!manual && m_cfg.appUpdate == QStringLiteral("off"))
        return;
    if (!m_updater->enabled() || m_updater->busy())
        return;
    m_updater->check([this, manual](const Updater::Release* rel, const QString& error) {
        if (!error.isEmpty()) {
            qWarning() << "update check failed" << error;
            if (manual)
                notify(QStringLiteral("업데이트를 확인하지 못했습니다: %1").arg(error), QSystemTrayIcon::Warning);
            return;
        }
        if (!rel) {
            if (manual)
                notify(QStringLiteral("최신 버전입니다 (%1).").arg(Updater::currentVersion()));
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
        if (canInstall && m_cfg.appUpdate == QStringLiteral("auto") && !manual) {
            installUpdate();
            return;
        }
        if (firstSeen || manual) {
            notify(canInstall ? QStringLiteral("새 버전(%1)이 있습니다. 이 알림을 누르거나 트레이 메뉴 / 설정 > 프로그램 업데이트에서 설치하세요.").arg(rel->tag)
                              : QStringLiteral("새 버전(%1)이 있습니다 (%2). 이 알림을 누르면 릴리즈 페이지를 엽니다.").arg(rel->tag, why));
            m_updateNoticeShown = true;
        }
        if (manual)
            installUpdate();
    });
}

void Application::installUpdate()
{
    if (m_newTag.isEmpty())
        return;
    QString why;
    if (!Updater::canSelfInstall(&why)) {
        QDesktopServices::openUrl(QUrl(m_newPage));
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
            return;
        }
        m_downloadedZip = zip;
        applyDownloadedUpdate();
    });
}

void Application::applyDownloadedUpdate()
{
    // 팝업 보는 중이면 닫힐 때까지 기다렸다가 교체
    if (m_popup->isVisible()) {
        QTimer::singleShot(2000, this, &Application::applyDownloadedUpdate);
        return;
    }
    // 받고 나면 알림 없이 바로 설치 후 재실행 (실패할 때만 알림)
    QString err;
    if (!m_updater->install(m_downloadedZip, &err)) {
        notify(QStringLiteral("업데이트를 설치하지 못했습니다: %1").arg(err), QSystemTrayIcon::Warning);
        return;
    }
    m_hotkeys->clear();
    qApp->quit();
}

// ------------------------------------------------------------ 환율

Rates Application::ratesFor(Game g, const QString& league) const
{
    return m_rates.value(gameKey(g) + QLatin1Char('|') + league).rates;
}

void Application::ensureRates(Game g, Lang server, const QString& league, std::function<void()> then)
{
    const QString key = gameKey(g) + QLatin1Char('|') + league;
    RateState& rs = m_rates[key];
    if (rs.rates.fresh()) {
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
    auto fresh = std::make_shared<Rates>();
    fresh->base = base;
    auto remaining = std::make_shared<int>(targets.size());
    for (const QString& t : targets) {
        m_client->exchange(g, server, league, base, t, [this, key, t, fresh, remaining](const TradeClient::ExchangeResult& r) {
            const double ratio = medianRatio(r.listings); // 기준 화폐 몇 개 = t 1개
            if (ratio > 0)
                fresh->toBase.insert(t, ratio);
            if (--(*remaining) > 0)
                return;
            RateState& st = m_rates[key];
            fresh->at = QDateTime::currentDateTimeUtc();
            st.rates = *fresh;
            st.loading = false;
            const auto waiters = st.waiters;
            st.waiters.clear();
            for (const auto& w : waiters)
                w();
        });
    }
}

// ------------------------------------------------------------ 시세

void Application::priceCheck()
{
    if (!m_popup->hasItem())
        return;
    m_client->cancel(m_priceToken);
    m_priceToken = m_client->newToken();
    const quint64 token = m_priceToken;

    const ParsedItem it = m_popup->collectItem();
    const Game g = it.game;
    const QString league = m_cfg.leagueFor(g);
    const Lang server = m_popup->server();
    m_popup->priceBusy(QStringLiteral("시세 확인중..."));
    loadCurrencyIcons(g, server, {QStringLiteral("chaos"), QStringLiteral("divine"), QStringLiteral("exalted"),
                                  QStringLiteral("mirror")});

    // 환율 먼저 (30분에 한 번, 일괄 교환 요청 2개)
    ensureRates(g, server, league, [this, token, it, g, league, server]() {
        if (token != m_priceToken)
            return;
        const Rates rates = ratesFor(g, league);
        const QString want = m_popup->exchangeWant();

        // ---- 일괄 교환
        if (!want.isEmpty()) {
            const QString haveName = currencyName(g, server, it.exchangeId);
            const QString wantName = currencyName(g, server, want);
            loadCurrencyIcons(g, server, {want});
            m_popup->priceStatus(QStringLiteral("교환 시세 확인중..."));
            m_client->exchange(
                g, server, league, it.exchangeId, want,
                [this, token, haveName, wantName, want, rates](const TradeClient::ExchangeResult& r) {
                    if (token != m_priceToken)
                        return;
                    if (!r.error.isEmpty()) {
                        m_popup->priceIdle(QStringLiteral("교환 시세를 가져오지 못했습니다: %1 (클릭하면 다시 시도합니다)").arg(r.error));
                        return;
                    }
                    m_popup->priceColumns({QStringLiteral("1개당"), QStringLiteral("환산"), QStringLiteral("재고"),
                                           QStringLiteral("등록"), QStringLiteral("판매자")});
                    // 나한테 좋은(1개당 많이 주는) 순서. 중앙값 절반도 안 되는 미끼 매물은 흐리게
                    QVector<Listing> rows = r.listings;
                    std::stable_sort(rows.begin(), rows.end(),
                                     [](const Listing& a, const Listing& b) { return a.amount > b.amount; });
                    QVector<double> perOne;
                    for (const Listing& l : rows)
                        perOne << l.amount;
                    const double mid = perOne.isEmpty() ? 0 : perOne[perOne.size() / 2];
                    for (const Listing& l : rows) {
                        m_popup->priceAddRow({PriceSummary::formatAmount(l.amount) + QLatin1Char(' ') + wantName,
                                              formatConversion(l.amount, want, rates), QString::number(l.stock),
                                              PriceSummary::ago(l.indexed), l.account},
                                             l.amount < mid * 0.5, l.online,
                                             QStringLiteral("%1 %2 → %3 %4")
                                                 .arg(PriceSummary::formatAmount(l.payAmount), haveName,
                                                      PriceSummary::formatAmount(l.getAmount), wantName));
                    }
                    QString summary = QStringLiteral("해당 교환 매물이 없습니다");
                    if (!perOne.isEmpty()) {
                        // 상위 5개 중앙값
                        const QVector<double> top = perOne.mid(0, 5);
                        const double med = top[top.size() / 2];
                        double conv = 0;
                        summary = QStringLiteral("1개 ≈ %1 %2").arg(PriceSummary::formatAmount(med), wantName);
                        if (rates.convert(med, want, &conv) && want != rates.base)
                            summary += QStringLiteral(" (%1)").arg(formatValue(conv, rates));
                        summary += QStringLiteral(" · 매물 %1").arg(r.total);
                    }
                    m_popup->priceDone(summary, QStringLiteral("%1/%2 건").arg(r.listings.size()).arg(r.total));
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
        const SearchRequest req = makeRequest(it, m_popup->collectState(), m_kw, &m_dbs[gi(g)], ctx);
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
        auto onProgress = [this, token, g, lang, rates](const TradeClient::PriceProgress& p) {
                if (token != m_priceToken)
                    return;
                const QDateTime now = QDateTime::currentDateTimeUtc();
                QStringList curs;
                for (const Listing& l : p.listings) {
                    if (!curs.contains(l.currency))
                        curs << l.currency;
                }
                loadCurrencyIcons(g, lang, curs); // 처음 보는 화폐면 받아서 나중에 칸에 붙음
                for (const Listing& l : p.listings) {
                    const bool stale = l.indexed.isValid() && l.indexed.daysTo(now) >= 3;
                    m_popup->priceAddRow({PriceSummary::formatAmount(l.amount) + QLatin1Char(' ') +
                                              shortCurrencyName(l.currency).replace(QStringLiteral("-"), QStringLiteral(" ")),
                                          formatConversion(l.amount, l.currency, rates),
                                          l.ilvl >= 0 ? QString::number(l.ilvl) : QString(),
                                          l.quality >= 0 ? QString::number(l.quality) : QString(),
                                          l.dps > 0 ? QString::number(l.dps, 'f', 1) : QString(),
                                          PriceSummary::ago(l.indexed), l.account},
                                         stale, l.online, currencyName(g, lang, l.currency));
                }
                m_popup->priceStatus(QStringLiteral("시세 확인중... %1/%2").arg(p.fetched).arg(p.requested));
            };
        auto onDone = [this, token, g, lang, it, rates](const TradeClient::PriceProgress& all, const QString& error) {
                if (token != m_priceToken)
                    return;
                learnIcon(it, all.listings);
                if (!error.isEmpty()) {
                    m_popup->priceIdle(QStringLiteral("시세를 조회하지 못했습니다: %1 (클릭하면 다시 시도합니다)").arg(error));
                    return;
                }
                const QString dist =
                    PriceSummary::summarize(all.listings, [this, g, lang](const QString& id) { return currencyName(g, lang, id); });
                QString summary;
                if (all.listings.isEmpty()) {
                    summary = all.total == 0 ? QStringLiteral("해당 물품의 거래가 없습니다") : QStringLiteral("가격이 붙은 매물이 없습니다");
                } else {
                    const Estimate e = estimatePrice(all.listings, rates);
                    summary = e.ok ? formatEstimate(e, rates, all.total) : dist;
                }
                m_popup->priceDone(summary, QStringLiteral("%1/%2 검색").arg(all.listings.size()).arg(all.total),
                                   dist.isEmpty() ? QString() : QStringLiteral("분포: ") + dist);
            };

        // 같은 조건을 1분 안에 다시 조회하면 요청 안 보내고 저장한 결과 (요청 제한 절약, APT 캐시와 같은 취지)
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const QString key = QStringLiteral("%1|%2|%3|%4|").arg(gi(g)).arg(static_cast<int>(lang)).arg(league).arg(m_popup->listCount()) +
                            QString::fromUtf8(QJsonDocument(q).toJson(QJsonDocument::Compact));
        for (auto c = m_priceCache.begin(); c != m_priceCache.end();) {
            if (now - c->at > 60000)
                c = m_priceCache.erase(c);
            else
                ++c;
        }
        if (auto c = m_priceCache.constFind(key); c != m_priceCache.constEnd()) {
            onProgress(c->result);
            onDone(c->result, QString());
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
    QString updText = QStringLiteral("지금 확인");
    if (!m_newTag.isEmpty())
        updText = Updater::canSelfInstall() ? QStringLiteral("%1 설치").arg(m_newTag)
                                             : QStringLiteral("%1 릴리즈 페이지 열기").arg(m_newTag);
    SettingsDialog dlg(m_cfg, m_dbs, &m_checked, statText, ver, updText);
    connect(&dlg, &SettingsDialog::updateDataRequested, this,
            [this](int g) { updateGame(g == 0 ? Game::Poe1 : Game::Poe2); });
    connect(&dlg, &SettingsDialog::appUpdateRequested, this, [this]() {
        if (m_newTag.isEmpty())
            checkForUpdate(true);
        else
            installUpdate();
    });
    if (dlg.exec() == QDialog::Accepted) {
        unregisterHotkeys();
        m_cfg = dlg.result();
        saveConfig();
        applyConfig();
    }
}

void Application::applyConfig()
{
    m_client->setTimeoutSec(m_cfg.serverTimeout);
    m_customRun = !m_cfg.clipboardWatch; // Ctrl+C 감시 끔 ({Run} 단축키는 그대로 동작)
    if (m_hotkeysOn) {
        unregisterHotkeys();
        registerHotkeys();
    }
}

void Application::saveConfig()
{
    QString e;
    if (!m_cfg.save(m_cfgPath, &e))
        qWarning() << "config save failed" << e;
}

void Application::restart()
{
    QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--wait-shutdown")});
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
