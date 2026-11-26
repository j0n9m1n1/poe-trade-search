// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Application.h"

#include "core/PriceSummary.h"
#include "core/QueryBuilder.h"
#include "core/SearchDefaults.h"
#include "net/DataManager.h"
#include "net/Endpoints.h"
#include "net/TradeClient.h"
#include "platform/HotkeyManager.h"
#include "platform/Platform.h"
#include "ui/MainPopup.h"
#include "ui/SettingsDialog.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
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
const QString kWiki = QStringLiteral("{wiki}");
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
                                 QStringLiteral("Config.txt 를 읽지 못해 기본값으로 시작함\n%1").arg(e));
            m_cfg = Config::defaults();
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
            *error = QStringLiteral("parser.json 로드 실패: ") + e;
        return false;
    }

    m_client = new TradeClient(this);
    m_data = new DataManager(m_dataDir, m_client, this);
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
    connect(m_popup, &MainPopup::wikiRequested, this, [this]() {
        if (m_popup->hasItem())
            openWiki(m_popup->collectItem());
    });
    connect(m_popup, &MainPopup::rememberToggled, this, &Application::onRemember);
    connect(m_popup, &MainPopup::popupHidden, this, [this]() {
        m_popupPos = m_popup->pos();
        m_client->cancel(m_priceToken);
        if (m_hotkeys->has(kEscId))
            m_hotkeys->remove(kEscId);
    });

    // ---- 트레이
    m_tray = new QSystemTrayIcon(QIcon(QStringLiteral(":/icons/app.ico")), this);
    m_tray->setToolTip(QStringLiteral("POE 거래소 검색 %1").arg(QStringLiteral(PTS_VERSION)));
    m_menu = new QMenu;
    m_menu->addAction(QStringLiteral("설정"), this, &Application::openSettings);
    m_menu->addAction(QStringLiteral("PoE1 데이터 업데이트"), this, [this]() { updateGame(Game::Poe1); });
    m_menu->addAction(QStringLiteral("PoE2 데이터 업데이트"), this, [this]() { updateGame(Game::Poe2); });
    m_pauseAction = m_menu->addAction(QStringLiteral("일시 정지"));
    m_pauseAction->setCheckable(true);
    connect(m_pauseAction, &QAction::toggled, this, [this](bool on) { m_paused = on; });
    m_menu->addSeparator();
    m_menu->addAction(QStringLiteral("재시작"), this, &Application::restart);
    m_menu->addAction(QStringLiteral("종료"), qApp, &QApplication::quit);
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

    QString hint = QStringLiteral("프로그램을 시작합니다.\n사용법: 아이템 위에서 Ctrl + C\n종료: 트레이 아이콘 우클릭");
    if (!platform::isAdministrator())
        hint += QStringLiteral("\n(PoE 를 관리자 권한으로 돌리면 이 프로그램도 관리자로 실행해야 단축키가 먹음)");
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
            m_tray->setToolTip(QStringLiteral("POE 거래소 검색 %1").arg(QStringLiteral(PTS_VERSION)));
        } else {
            notify(QStringLiteral("%1 거래소 데이터 업데이트 실패\n%2")
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
                notify(QStringLiteral("설정된 리그 '%1' 가 없어서 '%2' 로 바꿈").arg(cur, d.leagues().first().id));
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
        const bool wiki = m_expect == Expect::Wiki;
        m_expect = Expect::None;
        m_expectTimer.stop();
        handleItemText(text, wiki);
        return;
    }
    if (m_paused || m_customRun)
        return;
    if (!platform::foregroundGame().foreground)
        return;
    handleItemText(text, false);
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
                qWarning() << "hotkey register failed (다른 프로그램이 이미 쓰는 중일 수 있음)" << s.keycode
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
        m_pauseAction->setChecked(!m_paused);
        notify(m_paused ? QStringLiteral("일시 정지함. 같은 단축키 한 번 더 누르면 다시 시작")
                        : QStringLiteral("다시 시작함"));
        return;
    }
    if (lower.startsWith(kRestart)) {
        restart();
        return;
    }
    if (m_paused)
        return;
    if (lower.startsWith(kRun) || lower.startsWith(kWiki)) {
        m_expect = lower.startsWith(kWiki) ? Expect::Wiki : Expect::Search;
        QGuiApplication::clipboard()->clear();
        m_expectTimer.start(1500);
        // {Run} 은 Ctrl+Alt+C(고급 설명)로 복사해서 등급까지 가져옴
        platform::sendCopy(m_expect == Expect::Search);
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
        notify(QStringLiteral("이 단축키 명령은 1차 버전에서 지원 안 함: %1").arg(value), QSystemTrayIcon::Warning);
    }
}

// ------------------------------------------------------------ 동작

ParseOptions Application::parseOptions() const
{
    ParseOptions o;
    o.autoSelectPseudo = m_cfg.autoSelectPseudo;
    o.autoCheckTotalRes = m_cfg.autoCheckTotalres;
    o.autoCheckUnique = m_cfg.autoCheckUnique;
    o.autoSelectCorrupt = m_cfg.autoSelectCorrupt.toLower();
    o.autoSelectByType = m_cfg.byTypeList();
    return o;
}

void Application::handleItemText(const QString& text, bool wiki)
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
                notify(QStringLiteral("거래소 데이터를 받는 중... 받고 나면 다시 Ctrl+C 하셈"));
                updateGame(g);
            } else {
                notify(QStringLiteral("거래소 데이터 받는 중. 잠깐 기다려"));
            }
        } else {
            qWarning() << "parse failed" << err;
        }
        return;
    }
    if (wiki) {
        openWiki(*it);
        return;
    }

    m_popup->showItem(*it, &m_dbs[gi(g)], m_cfg, serverFor(it->lang));
    if (m_popupPos.isNull()) {
        const QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
        m_popupPos = avail.topLeft() + QPoint(30, 30);
    }
    if (!m_popup->isVisible())
        m_popup->move(m_popupPos);
    platform::setNoActivate(m_popup, true);
    m_popup->show();
    m_popup->raise();

    if (m_cfg.autoPriceCheck)
        priceCheck();
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
    ctx.status = m_cfg.tradeStatus;
    ctx.indexed = QueryBuilder::indexedFromDays(m_cfg.searchBeforeDay);
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
                             QStringLiteral("거래소 검색 실패: %1\n한/영 서버 거래소 접속을 확인하고 다시 해봐").arg(r.error));
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

    const QString want = m_popup->exchangeWant();
    if (!want.isEmpty()) {
        m_popup->priceBusy(QStringLiteral("교환 시세 확인중..."));
        const QString haveName = currencyName(g, server, it.exchangeId);
        const QString wantName = currencyName(g, server, want);
        m_client->exchange(
            g, server, league, it.exchangeId, want,
            [this, token, haveName, wantName](const TradeClient::ExchangeResult& r) {
                if (token != m_priceToken)
                    return;
                if (!r.error.isEmpty()) {
                    m_popup->priceIdle(QStringLiteral("교환 시세 실패: %1 (클릭해서 다시)").arg(r.error));
                    return;
                }
                QStringList lines;
                double best = 0, worst = 0;
                for (const Listing& l : r.listings) {
                    lines << QStringLiteral("1 %1 → %2 %3   [%4]  재고 %5")
                                 .arg(haveName, PriceSummary::formatAmount(l.amount), wantName, l.account)
                                 .arg(l.stock);
                    best = best == 0 ? l.amount : qMax(best, l.amount);
                    worst = worst == 0 ? l.amount : qMin(worst, l.amount);
                }
                m_popup->priceAppend(lines);
                const QString summary =
                    r.listings.isEmpty()
                        ? QStringLiteral("해당 교환 매물 없음")
                        : QStringLiteral("1개당 %1 ~ %2 %3")
                              .arg(PriceSummary::formatAmount(best), PriceSummary::formatAmount(worst), wantName);
                m_popup->priceDone(summary, QStringLiteral("%1/%2 건").arg(r.listings.size()).arg(r.total));
            },
            token);
        return;
    }

    RequestContext ctx;
    ctx.server = server;
    ctx.status = m_cfg.tradeStatus;
    ctx.indexed = QueryBuilder::indexedFromDays(m_cfg.searchBeforeDay);
    ctx.priceMin = m_popup->priceMin();
    ctx.forBrowser = false;
    const SearchRequest req = makeRequest(it, m_popup->collectState(), m_kw, &m_dbs[gi(g)], ctx);
    const QJsonObject q = QueryBuilder::build(req, &m_dbs[gi(g)][req.lang]);

    m_popup->priceBusy(QStringLiteral("시세 확인중..."));
    const Lang lang = req.lang;
    m_client->priceCheck(
        g, lang, league, q, m_popup->listCount(),
        [this, token, g, lang](const TradeClient::PriceProgress& p) {
            if (token != m_priceToken)
                return;
            QStringList lines;
            for (const Listing& l : p.listings) {
                lines << QStringLiteral("%1 %2   [%3]  %4")
                             .arg(PriceSummary::formatAmount(l.amount), currencyName(g, lang, l.currency), l.account,
                                  PriceSummary::ago(l.indexed));
            }
            m_popup->priceAppend(lines);
            m_popup->priceBusy(QStringLiteral("시세 확인중... %1/%2").arg(p.fetched).arg(p.requested));
        },
        [this, token, g, lang](const TradeClient::PriceProgress& all, const QString& error) {
            if (token != m_priceToken)
                return;
            if (!error.isEmpty()) {
                m_popup->priceIdle(QStringLiteral("시세 조회 실패: %1 (클릭해서 다시)").arg(error));
                return;
            }
            const QString summary =
                all.listings.isEmpty()
                    ? (all.total == 0 ? QStringLiteral("해당 물품의 거래가 없음") : QStringLiteral("가격 붙은 매물 없음"))
                    : PriceSummary::summarize(all.listings,
                                              [this, g, lang](const QString& id) { return currencyName(g, lang, id); });
            m_popup->priceDone(summary, QStringLiteral("%1/%2 검색").arg(all.listings.size()).arg(all.total));
        },
        token);
}

void Application::openWiki(const ParsedItem& it)
{
    QString name = it.rarityId == QStringLiteral("unique") ? it.name : (it.transfigured ? it.rawType : it.type);
    if (it.lang == Lang::Ko) {
        QString t, n;
        if (m_dbs[gi(it.game)].translateItem(Lang::Ko, Lang::En, it.type, it.name, &t, &n)) {
            name = it.rarityId == QStringLiteral("unique") ? n : t;
        } else {
            // 영어 이름을 못 찾으면 한글 지원하는 poedb 검색으로
            QUrl u(it.game == Game::Poe1 ? QStringLiteral("https://poedb.tw/kr/search")
                                         : QStringLiteral("https://poe2db.tw/kr/search"));
            QUrlQuery q;
            q.addQueryItem(QStringLiteral("q"), name);
            u.setQuery(q);
            QDesktopServices::openUrl(u);
            return;
        }
    }
    if (!name.isEmpty())
        QDesktopServices::openUrl(endpoints::wiki(it.game, name));
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

    SettingsDialog dlg(m_cfg, m_dbs, &m_checked, statText, ver);
    connect(&dlg, &SettingsDialog::updateDataRequested, this,
            [this](int g) { updateGame(g == 0 ? Game::Poe1 : Game::Poe2); });
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
    m_customRun = false;
    for (const Shortcut& s : m_cfg.shortcuts) {
        if (s.keycode > 0 && s.value.trimmed().toLower().startsWith(kRun))
            m_customRun = true;
    }
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
    if (m_tray && QSystemTrayIcon::supportsMessages())
        m_tray->showMessage(QStringLiteral("POE 거래소 검색"), msg, icon, 5000);
    qInfo().noquote() << msg;
}

} // namespace pts
