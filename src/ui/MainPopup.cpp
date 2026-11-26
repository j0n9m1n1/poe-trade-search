// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/MainPopup.h"

#include "core/Keywords.h"
#include "core/PriceEstimate.h"
#include "core/TradeDb.h"
#include "platform/Platform.h"
#include "ui/FilterRow.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace pts {

namespace {
QLineEdit* numEdit(QWidget* parent, int w = 34)
{
    auto* e = new QLineEdit(parent);
    e->setFixedWidth(w);
    e->setAlignment(Qt::AlignRight);
    e->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("^-?\\d*(\\.\\d*)?$")), e));
    return e;
}

OptDouble toOpt(const QLineEdit* e)
{
    bool ok = false;
    const double v = e->text().trimmed().toDouble(&ok);
    return ok ? OptDouble(v) : std::nullopt;
}

QString num(double v)
{
    return QString::number(v, 'f', 1).remove(QRegularExpression(QStringLiteral("\\.0$")));
}

QFrame* hline(QWidget* parent)
{
    auto* f = new QFrame(parent);
    f->setFrameShape(QFrame::HLine);
    f->setFrameShadow(QFrame::Sunken);
    return f;
}

void setBold(QWidget* w, bool on, const QString& color = QString())
{
    QFont f = w->font();
    f.setBold(on);
    w->setFont(f);
    w->setStyleSheet(on && !color.isEmpty() ? QStringLiteral("color:%1;").arg(color) : QString());
}

QColor dimColor(const QPalette& pal)
{
    const QColor fg = pal.color(QPalette::WindowText);
    const QColor bg = pal.color(QPalette::Window);
    return QColor((fg.red() * 3 + bg.red() * 2) / 5, (fg.green() * 3 + bg.green() * 2) / 5,
                  (fg.blue() * 3 + bg.blue() * 2) / 5);
}
} // namespace

MainPopup::MainPopup(const Keywords& kw, QWidget* parent) : QWidget(parent), m_kw(kw)
{
    setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint | Qt::CustomizeWindowHint | Qt::WindowTitleHint |
                   Qt::WindowCloseButtonHint);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setWindowTitle(QStringLiteral("POE 거래소 검색"));
    buildUi();
}

void MainPopup::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 10, 14, 12);
    root->setSpacing(6);

    // ---- 상단: 희귀도 / 프리셋 / 범위 / 서버 / 설정
    auto* top = new QHBoxLayout;
    m_rarity = new QComboBox(this);
    const QList<QPair<QString, QString>> rar = {{QStringLiteral("모두"), QString()},
                                                {QStringLiteral("일반"), QStringLiteral("normal")},
                                                {QStringLiteral("마법"), QStringLiteral("magic")},
                                                {QStringLiteral("희귀"), QStringLiteral("rare")},
                                                {QStringLiteral("고유"), QStringLiteral("unique")},
                                                {QStringLiteral("고유 제외"), QStringLiteral("nonunique")}};
    for (const auto& r : rar)
        m_rarity->addItem(r.first, r.second);
    m_rarity->setToolTip(QStringLiteral("검색할 희귀도"));

    m_preset = new QComboBox(this);
    m_preset->addItem(QStringLiteral("기본"), static_cast<int>(Preset::Default));
    m_preset->addItem(QStringLiteral("정확"), static_cast<int>(Preset::Exact));
    m_preset->addItem(QStringLiteral("베이스"), static_cast<int>(Preset::Base));
    m_preset->setToolTip(QStringLiteral("기본: pseudo 우선 / 자동 체크\n"
                                        "정확: 원래 옵션, T1~T2 만 체크 (등급 모르면 일반 옵션 전부), 정확값\n"
                                        "베이스: 옵션 다 끄고 아이템 레벨·영향 위주"));
    m_range = new QComboBox(this);
    m_range->setToolTip(QStringLiteral("옵션 값 완화: 최소값을 N% 낮춰서 검색 (고유는 롤 범위 폭 기준)"));

    m_server = new QComboBox(this);
    m_server->addItem(QStringLiteral("한국 서버"), li(Lang::Ko));
    m_server->addItem(QStringLiteral("글로벌 서버"), li(Lang::En));
    m_settings = new QPushButton(QStringLiteral("설정"), this);
    m_settings->setFixedWidth(52);
    top->addWidget(m_rarity);
    top->addWidget(m_preset);
    top->addWidget(m_range);
    top->addStretch(1);
    top->addWidget(m_server);
    top->addWidget(m_settings);
    root->addLayout(top);

    // 아이콘 + 이름
    auto* head = new QHBoxLayout;
    m_icon = new QLabel(this);
    m_icon->setFixedSize(52, 52);
    m_icon->setAlignment(Qt::AlignCenter);
    m_icon->setVisible(false);
    auto* names = new QVBoxLayout;
    names->setSpacing(1);
    m_name = new QLabel(this);
    QFont nf = m_name->font();
    nf.setBold(true);
    nf.setPointSizeF(nf.pointSizeF() + 2);
    m_name->setFont(nf);
    m_name->setWordWrap(true);
    m_sub = new QLabel(this);
    m_sub->setWordWrap(true);
    names->addWidget(m_name);
    names->addWidget(m_sub);
    head->addWidget(m_icon, 0, Qt::AlignTop);
    head->addLayout(names, 1);
    root->addLayout(head);

    auto* hdr = new QHBoxLayout;
    m_info = new QLabel(QStringLiteral("옵션"), this);
    auto* lmin = new QLabel(QStringLiteral("최소"), this);
    auto* lmax = new QLabel(QStringLiteral("최대"), this);
    lmin->setFixedWidth(42);
    lmax->setFixedWidth(42);
    lmin->setAlignment(Qt::AlignCenter);
    lmax->setAlignment(Qt::AlignCenter);
    m_all = new QCheckBox(this);
    m_all->setToolTip(QStringLiteral("전체 체크 (색 있는 고정/제작 옵션 제외)"));
    hdr->addWidget(m_info, 1);
    hdr->addWidget(lmin);
    hdr->addWidget(lmax);
    hdr->addWidget(m_all);
    root->addLayout(hdr);

    // ---- 탭
    m_tabs = new QTabWidget(this);
    m_tabs->setTabPosition(QTabWidget::South);
    m_tabs->setDocumentMode(true);

    auto* page1 = new QWidget(m_tabs);
    auto* p1 = new QVBoxLayout(page1);
    p1->setContentsMargins(0, 4, 0, 4);

    auto* scroll = new QScrollArea(page1);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMinimumHeight(250);
    m_rowsBox = new QWidget(scroll);
    m_rowsLayout = new QVBoxLayout(m_rowsBox);
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(2);
    m_detail = new QLabel(m_rowsBox);
    m_detail->setWordWrap(true);
    m_detail->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_rowsLayout->addWidget(m_detail);
    m_rowsLayout->addStretch(1);
    scroll->setWidget(m_rowsBox);
    p1->addWidget(scroll, 1);

    // 일괄 교환
    m_exchangeBox = new QWidget(page1);
    auto* ex = new QHBoxLayout(m_exchangeBox);
    ex->setContentsMargins(0, 0, 0, 0);
    m_want1 = new QComboBox(m_exchangeBox);
    m_want2 = new QComboBox(m_exchangeBox);
    m_want1->setToolTip(QStringLiteral("이 아이템을 팔고 받을 화폐 (일괄 교환 시세). '일반 검색' 이면 아이템 검색"));
    m_want2->setToolTip(QStringLiteral("같은 종류의 다른 아이템과 교환 시세"));
    ex->addWidget(m_want1, 1);
    ex->addWidget(m_want2, 1);
    p1->addWidget(m_exchangeBox);

    p1->addWidget(hline(page1));

    // 레벨 / 퀄 / 홈 / 특수 / 타락 / 영향
    m_miscBox = new QWidget(page1);
    auto* g = new QGridLayout(m_miscBox);
    g->setContentsMargins(0, 0, 0, 0);
    g->setHorizontalSpacing(6);
    g->setVerticalSpacing(3);
    m_lv = new QCheckBox(QStringLiteral("레벨"), m_miscBox);
    m_lvMin = numEdit(m_miscBox);
    m_lvMax = numEdit(m_miscBox);
    m_q = new QCheckBox(QStringLiteral("퀄리티"), m_miscBox);
    m_qMin = numEdit(m_miscBox);
    m_qMax = numEdit(m_miscBox);
    m_sock = new QCheckBox(QStringLiteral("홈"), m_miscBox);
    m_sMin = numEdit(m_miscBox, 26);
    m_sMax = numEdit(m_miscBox, 26);
    m_linkLabel = new QLabel(QStringLiteral("링크"), m_miscBox);
    m_lMin = numEdit(m_miscBox, 26);
    m_lMax = numEdit(m_miscBox, 26);

    auto pair = [&](QCheckBox* c, QLineEdit* a, QLineEdit* b) {
        auto* w = new QWidget(m_miscBox);
        auto* v = new QVBoxLayout(w);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(1);
        v->addWidget(c);
        auto* h = new QHBoxLayout;
        h->setSpacing(2);
        h->addWidget(a);
        h->addWidget(b);
        h->addStretch(1);
        v->addLayout(h);
        return w;
    };
    g->addWidget(pair(m_lv, m_lvMin, m_lvMax), 0, 0);
    g->addWidget(pair(m_q, m_qMin, m_qMax), 0, 1);
    {
        auto* w = new QWidget(m_miscBox);
        auto* v = new QVBoxLayout(w);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(1);
        v->addWidget(m_sock);
        auto* h = new QHBoxLayout;
        h->setSpacing(2);
        h->addWidget(m_sMin);
        h->addWidget(m_sMax);
        h->addWidget(m_linkLabel);
        h->addWidget(m_lMin);
        h->addWidget(m_lMax);
        h->addStretch(1);
        v->addLayout(h);
        g->addWidget(w, 0, 2);
    }
    m_special1 = new QCheckBox(QStringLiteral("결합"), m_miscBox);
    m_special2 = new QCheckBox(QStringLiteral("역병 침식"), m_miscBox);
    m_corrupt = new QComboBox(m_miscBox);
    m_corrupt->addItem(QStringLiteral("타락: 무관"));
    m_corrupt->addItem(QStringLiteral("타락: 예"));
    m_corrupt->addItem(QStringLiteral("타락: 아니오"));
    m_infl1 = new QComboBox(m_miscBox);
    m_infl2 = new QComboBox(m_miscBox);
    for (QComboBox* c : {m_infl1, m_infl2}) {
        c->addItem(QStringLiteral("영향 없음"), -1);
        for (int i = 0; i < m_kw.influences().size(); ++i)
            c->addItem(m_kw.influences()[i].name.first(Lang::Ko), i);
    }
    m_alt = new QComboBox(m_miscBox);

    auto* row2 = new QHBoxLayout;
    row2->addWidget(m_special1);
    row2->addWidget(m_special2);
    row2->addStretch(1);
    row2->addWidget(m_infl1);
    row2->addWidget(m_infl2);
    row2->addWidget(m_alt);
    row2->addWidget(m_corrupt);
    g->addLayout(row2, 1, 0, 1, 3);
    p1->addWidget(m_miscBox);

    m_byCategory = new QCheckBox(QStringLiteral("아이템 유형으로 검색 (이름/베이스 무시)"), page1);
    p1->addWidget(m_byCategory);

    m_tabs->addTab(page1, QStringLiteral("간략히"));

    // 시세 목록 탭
    auto* page2 = new QWidget(m_tabs);
    auto* p2 = new QVBoxLayout(page2);
    p2->setContentsMargins(0, 4, 0, 4);
    m_prices = new QTableWidget(page2);
    m_prices->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_prices->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_prices->verticalHeader()->setVisible(false);
    m_prices->verticalHeader()->setDefaultSectionSize(20);
    m_prices->horizontalHeader()->setStretchLastSection(true);
    m_prices->setShowGrid(false);
    m_prices->setAlternatingRowColors(true);
    m_prices->setWordWrap(false);
    p2->addWidget(m_prices, 1);
    auto* pr = new QHBoxLayout;
    m_priceCount = new QLabel(page2);
    pr->addWidget(m_priceCount, 1);
    m_status = new QComboBox(page2);
    m_status->addItem(QStringLiteral("즉시구매+직접"), QStringLiteral("available"));
    m_status->addItem(QStringLiteral("즉시구매만"), QStringLiteral("securable"));
    m_status->addItem(QStringLiteral("직접(접속 중)"), QStringLiteral("online"));
    m_status->addItem(QStringLiteral("전부"), QStringLiteral("any"));
    m_status->setToolTip(QStringLiteral("판매 방식"));
    pr->addWidget(m_status);
    m_collapse = new QCheckBox(QStringLiteral("계정별 묶기"), page2);
    pr->addWidget(m_collapse);
    pr->addWidget(new QLabel(QStringLiteral("최소:"), page2));
    m_priceMin = numEdit(page2, 44);
    m_priceMin->setToolTip(QStringLiteral("최소 가격 (PoE1 카오스 / PoE2 엑잘티드 환산)"));
    pr->addWidget(m_priceMin);
    m_listCount = new QComboBox(page2);
    for (int n : {10, 20, 40, 60, 80})
        m_listCount->addItem(QStringLiteral("%1개").arg(n), n);
    m_listCount->setToolTip(QStringLiteral("목록 수 (많을수록 느림)"));
    pr->addWidget(m_listCount);
    p2->addLayout(pr);
    m_tabs->addTab(page2, QStringLiteral("자세히"));

    root->addWidget(m_tabs, 1);

    // 시세 줄
    m_price = new QPushButton(this);
    m_price->setFlat(true);
    m_price->setCursor(Qt::PointingHandCursor);
    m_price->setStyleSheet(QStringLiteral("text-align:right; padding:2px;"));
    m_price->setToolTip(QStringLiteral("좌클릭: 시세 조회 / 우클릭: 시세 목록 탭"));
    m_price->installEventFilter(this);
    m_idleText = QStringLiteral("시세를 검색하려면 클릭");
    m_price->setText(m_idleText);
    root->addWidget(m_price);

    auto* bottom = new QHBoxLayout;
    m_search = new QPushButton(QStringLiteral("거래소에서 찾기"), this);
    m_close = new QPushButton(QStringLiteral("닫기"), this);
    m_search->setMinimumHeight(34);
    m_close->setMinimumHeight(34);
    QFont bf = m_close->font();
    bf.setBold(true);
    m_close->setFont(bf);
    bottom->addWidget(m_search, 3);
    bottom->addWidget(m_close, 1);
    root->addLayout(bottom);

    setMinimumWidth(660);

    // 다크 테마에서도 보이게: 글자색과 배경색 중간값으로 흐린 글씨
    {
        const QString css = QStringLiteral("color:%1;").arg(dimColor(palette()).name());
        m_sub->setStyleSheet(css);
        m_byCategory->setStyleSheet(css);
    }

    // ---- 연결
    connect(m_search, &QPushButton::clicked, this, &MainPopup::openTradeRequested);
    connect(m_close, &QPushButton::clicked, this, &QWidget::hide);
    connect(m_settings, &QPushButton::clicked, this, &MainPopup::settingsRequested);
    connect(m_price, &QPushButton::clicked, this, &MainPopup::priceCheckRequested);
    connect(m_all, &QCheckBox::toggled, this, [this](bool on) {
        for (FilterRow* r : m_rows) {
            if (r->isPlain() && r->isCheckable())
                r->setChecked(on);
        }
    });
    auto stale = [this]() { markStale(); };
    for (QComboBox* c : {m_rarity, m_server, m_corrupt, m_infl1, m_infl2, m_alt, m_status, m_listCount})
        connect(c, qOverload<int>(&QComboBox::currentIndexChanged), this, stale);
    for (QCheckBox* c : {m_lv, m_q, m_sock, m_special1, m_special2, m_byCategory, m_collapse})
        connect(c, &QCheckBox::toggled, this, stale);
    connect(m_preset, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() {
        applyPresetAndRender();
        markStale();
    });
    connect(m_range, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() {
        applyPresetAndRender();
        markStale();
    });
    auto exch = [this](QComboBox* other) {
        return [this, other](int idx) {
            if (idx > 0) {
                QSignalBlocker b(other);
                other->setCurrentIndex(0);
            }
            markStale();
            emit priceCheckRequested();
        };
    };
    connect(m_want1, qOverload<int>(&QComboBox::currentIndexChanged), this, exch(m_want2));
    connect(m_want2, qOverload<int>(&QComboBox::currentIndexChanged), this, exch(m_want1));

    for (QWidget* w : findChildren<QWidget*>()) {
        if (qobject_cast<QLineEdit*>(w) || qobject_cast<QComboBox*>(w))
            installActivator(w);
    }
}

void MainPopup::installActivator(QWidget* w)
{
    w->installEventFilter(this);
}

bool MainPopup::eventFilter(QObject* o, QEvent* e)
{
    if (e->type() == QEvent::MouseButtonPress) {
        if (o == m_price) {
            auto* me = static_cast<QMouseEvent*>(e);
            if (me->button() == Qt::RightButton) {
                m_tabs->setCurrentIndex(m_tabs->currentIndex() == 0 ? 1 : 0);
                return true;
            }
        } else if (qobject_cast<QLineEdit*>(o) && !isActiveWindow()) {
            // 포커스 안 뺏고 떠 있다가 입력칸 누르면 그때 활성화 (원본 WS_EX_NOACTIVATE 토글)
            platform::activateWindow(this);
        }
    }
    return QWidget::eventFilter(o, e);
}

void MainPopup::clearRows()
{
    for (FilterRow* r : m_rows) {
        m_rowsLayout->removeWidget(r);
        r->deleteLater();
    }
    m_rows.clear();
}

void MainPopup::markStale()
{
    if (m_price->text() != m_idleText && !m_price->text().endsWith(QStringLiteral("...")))
        m_price->setStyleSheet(QStringLiteral("text-align:right; padding:2px; color:#e75480;"));
}

void MainPopup::setLeagueTitle(const QString& league)
{
    setWindowTitle(QStringLiteral("POE 거래소 검색 - %1").arg(league));
}

int MainPopup::rangePct() const
{
    return m_range->currentData().toInt();
}

void MainPopup::showItem(const ParsedItem& parsed, const GameDb* gdb, const Config& cfg, Lang server)
{
    m_parsed = parsed;
    m_gdb = gdb;
    m_hasItem = true;
    m_cfgRange = cfg.searchStatRange;
    const ParsedItem& it = m_parsed;
    const TradeDb* db = gdb ? &(*gdb)[it.lang] : nullptr;
    const bool poe1 = it.game == Game::Poe1;

    // 이름
    QString title = it.rarityId == QStringLiteral("unique") && !it.name.isEmpty() ? it.name + QLatin1Char(' ') + it.type : it.type;
    if (it.transfigured)
        title = it.rawType;
    m_name->setText(title.isEmpty() ? it.rawType : title);
    m_icon->clear();
    m_icon->setVisible(false);
    QStringList sub;
    sub << (poe1 ? QStringLiteral("PoE1") : QStringLiteral("PoE2")) << it.itemClass;
    if (!it.rarityText.isEmpty())
        sub << it.rarityText;
    if (it.itemLevel > 0)
        sub << QStringLiteral("ilvl %1").arg(it.itemLevel);
    if (it.corrupted)
        sub << QStringLiteral("타락");
    m_sub->setText(sub.join(QStringLiteral(" · ")));

    {
        QSignalBlocker b(m_rarity);
        const int ri = m_rarity->findData(it.rarityFilter);
        m_rarity->setCurrentIndex(ri < 0 ? 0 : ri);
    }
    {
        QSignalBlocker b(m_server);
        m_server->setCurrentIndex(m_server->findData(li(server)));
    }
    {
        QSignalBlocker b1(m_preset), b2(m_range);
        m_preset->setCurrentIndex(0);
        m_range->clear();
        const int base = m_cfgRange > 0 ? m_cfgRange : 10;
        m_range->addItem(QStringLiteral("정확값"), 0);
        m_range->addItem(QStringLiteral("-%1%").arg(base), base);
        m_range->addItem(QStringLiteral("-%1% (넓게)").arg(base * 2), base * 2);
        m_range->setCurrentIndex(m_cfgRange > 0 ? 1 : 0);
        const bool hasMods = !it.mods.isEmpty();
        m_preset->setEnabled(hasMods);
        m_range->setEnabled(hasMods && !exactValuesOnly(it));
    }

    if (it.hasDps)
        m_info->setText(QStringLiteral("DPS: P.%1 + E.%2 = T.%3")
                            .arg(num(it.pdps), num(it.edps), num(it.pdps + it.edps + it.cdps)));
    else
        m_info->setText(QStringLiteral("옵션"));

    m_detail->setVisible(it.detail || it.mods.isEmpty());
    m_detail->setText(it.detail ? it.detailText
                                : (it.mods.isEmpty() ? QStringLiteral("검색 가능한 옵션이 없음") : QString()));

    // 일괄 교환: 교환 가능한 템이면 기본으로 기준 화폐 교환 시세 (R6)
    const bool exchange = !it.exchangeId.isEmpty() && db;
    m_exchangeBox->setVisible(exchange);
    {
        QSignalBlocker b1(m_want1), b2(m_want2);
        m_want1->clear();
        m_want2->clear();
        if (exchange) {
            const StaticEntry* self = db->staticById(it.exchangeId);
            m_want1->addItem(QStringLiteral("일반 검색 (교환 안 함)"), QString());
            m_want2->addItem(QStringLiteral("같은 종류와 교환"), QString());
            for (const StaticEntry& s : db->statics()) {
                if (s.id == it.exchangeId)
                    continue;
                if (s.group == QStringLiteral("Currency"))
                    m_want1->addItem(s.text, s.id);
                else if (self && s.group == self->group)
                    m_want2->addItem(s.text, s.id);
            }
            m_want2->setVisible(m_want2->count() > 1);
            QString def = Rates::baseFor(it.game);
            if (it.exchangeId == def)
                def = QStringLiteral("divine");
            const int di = m_want1->findData(def);
            if (di > 0)
                m_want1->setCurrentIndex(di);
        }
    }

    // 시세 영역
    m_prices->clear();
    m_prices->setRowCount(0);
    m_prices->setColumnCount(0);
    m_priceCount->clear();
    {
        QSignalBlocker b1(m_listCount), b2(m_status), b3(m_collapse);
        const int lc = m_listCount->findData(cfg.searchListCount);
        m_listCount->setCurrentIndex(lc < 0 ? 1 : lc);
        const int si = m_status->findData(cfg.tradeStatus);
        m_status->setCurrentIndex(si < 0 ? 0 : si);
        m_collapse->setChecked(cfg.collapseListings);
    }
    m_priceMin->setText(cfg.searchPriceMinimum > 0 ? QString::number(cfg.searchPriceMinimum) : QString());
    priceIdle(m_idleText);
    m_tabs->setCurrentIndex(0);
    setLeagueTitle(cfg.leagueFor(it.game));

    applyPresetAndRender();
    adjustSize();
}

void MainPopup::applyPresetAndRender()
{
    if (!m_hasItem)
        return;
    const Preset p = static_cast<Preset>(m_preset->currentData().toInt());
    ItemFilterState st;
    m_item = applyPreset(m_parsed, p, p == Preset::Exact ? 0 : rangePct(), &st);
    // 정확 프리셋은 범위를 정확값으로 보여줌
    if (p == Preset::Exact) {
        QSignalBlocker b(m_range);
        m_range->setCurrentIndex(0);
    }
    renderRows();
    renderMisc(st);
}

void MainPopup::renderRows()
{
    clearRows();
    for (int i = 0; i < m_item.mods.size(); ++i) {
        auto* r = new FilterRow(m_rowsBox);
        r->setMod(m_item.mods[i]);
        installActivator(r->minEdit());
        installActivator(r->maxEdit());
        connect(r, &FilterRow::changed, this, [this]() { markStale(); });
        connect(r, &FilterRow::rememberToggled, this, [this, i](bool on) {
            const ParsedMod& m = m_item.mods[i];
            emit rememberToggled(m.statKey, m_item.top, on);
            markStale();
        });
        m_rowsLayout->insertWidget(m_rowsLayout->count() - 2, r);
        m_rows.push_back(r);
    }
    QSignalBlocker b(m_all);
    m_all->setChecked(m_item.checkAllUnique);
}

void MainPopup::renderMisc(const ItemFilterState& st)
{
    const ParsedItem& it = m_item;
    const TradeDb* db = m_gdb ? &(*m_gdb)[it.lang] : nullptr;
    const bool poe1 = it.game == Game::Poe1;
    const auto kind = levelKindFor(it);
    m_lv->setText(kind == SearchRequest::LevelKind::GemLevel ? QStringLiteral("젬 레벨")
                  : kind == SearchRequest::LevelKind::MapTier ? QStringLiteral("지도 등급")
                                                               : QStringLiteral("아이템 레벨"));
    auto setOpt = [](QLineEdit* e, const OptDouble& v) { e->setText(v ? QString::number(*v) : QString()); };
    QSignalBlocker b1(m_lv), b2(m_q), b3(m_sock), b4(m_special1), b5(m_special2), b6(m_corrupt), b7(m_infl1),
        b8(m_infl2), b9(m_alt), b10(m_byCategory), b11(m_rarity);
    m_lv->setChecked(st.useLevel);
    setOpt(m_lvMin, st.levelMin);
    setOpt(m_lvMax, st.levelMax);
    m_q->setChecked(st.useQuality);
    setOpt(m_qMin, st.qualityMin);
    setOpt(m_qMax, st.qualityMax);
    m_sock->setChecked(st.useSockets);
    setOpt(m_sMin, st.socketsMin);
    setOpt(m_sMax, st.socketsMax);
    setOpt(m_lMin, st.linksMin);
    setOpt(m_lMax, st.linksMax);
    m_linkLabel->setVisible(poe1);
    m_lMin->setVisible(poe1);
    m_lMax->setVisible(poe1);
    m_sock->setText(poe1 ? QStringLiteral("홈·링크") : QStringLiteral("룬 홈"));

    setBold(m_lv, it.highlightLevel, QStringLiteral("#8B0000"));
    setBold(m_q, it.highlightQuality, QStringLiteral("#8B0000"));

    // 결합 / 역병
    const bool mapP1 = poe1 && it.isMap;
    m_special1->setVisible(poe1 && (it.isGear || mapP1));
    m_special1->setText(mapP1 ? QStringLiteral("역병") : QStringLiteral("결합"));
    m_special1->setChecked(mapP1 ? st.blighted : st.synthesised);
    m_special2->setVisible(mapP1);
    m_special2->setChecked(st.blightRavaged);

    m_corrupt->setCurrentIndex(st.corrupted);
    setBold(m_corrupt, it.corrupted, QStringLiteral("#8B0000"));

    const bool showInfl = poe1 && it.isGear;
    m_infl1->setVisible(showInfl);
    m_infl2->setVisible(showInfl);
    m_infl1->setCurrentIndex(st.influences.size() > 0 ? m_infl1->findData(st.influences[0]) : 0);
    m_infl2->setCurrentIndex(st.influences.size() > 1 ? m_infl2->findData(st.influences[1]) : 0);

    // 강탈 가치 / 결전 보상
    m_alt->clear();
    if (it.isHeist && db) {
        for (const auto& o : db->filterOptions(QStringLiteral("heist_objective_value")))
            m_alt->addItem(o.text, o.id);
        const int ai = m_alt->findData(st.heistObjective);
        m_alt->setCurrentIndex(ai < 0 ? 0 : ai);
    } else if (it.isUltimatum) {
        m_alt->addItem(QStringLiteral("모든 보상"), QString());
        for (const auto& u : m_kw.ultimatumRewards())
            m_alt->addItem(u.text.first(it.lang), u.id);
        const int ai = m_alt->findData(st.ultimatumReward);
        m_alt->setCurrentIndex(ai < 0 ? 0 : ai);
    }
    m_alt->setVisible(m_alt->count() > 0);

    m_byCategory->setChecked(st.byCategory);
    const int ri = m_rarity->findData(st.rarity);
    if (ri >= 0)
        m_rarity->setCurrentIndex(ri);
    m_miscBox->setVisible(!it.isCurrency && !it.isCard);
}

ParsedItem MainPopup::collectItem() const
{
    ParsedItem it = m_item;
    for (int i = 0; i < m_rows.size() && i < it.mods.size(); ++i)
        m_rows[i]->apply(it.mods[i]);
    return it;
}

ItemFilterState MainPopup::collectState() const
{
    ItemFilterState s = defaultFilterState(m_item);
    s.useLevel = m_lv->isChecked();
    s.levelMin = toOpt(m_lvMin);
    s.levelMax = toOpt(m_lvMax);
    s.useQuality = m_q->isChecked();
    s.qualityMin = toOpt(m_qMin);
    s.qualityMax = toOpt(m_qMax);
    s.useSockets = m_sock->isChecked();
    s.socketsMin = toOpt(m_sMin);
    s.socketsMax = toOpt(m_sMax);
    s.linksMin = m_item.game == Game::Poe1 ? toOpt(m_lMin) : std::nullopt;
    s.linksMax = m_item.game == Game::Poe1 ? toOpt(m_lMax) : std::nullopt;
    s.corrupted = m_corrupt->currentIndex();
    s.rarity = m_rarity->currentData().toString();
    s.byCategory = m_byCategory->isChecked();
    s.influences.clear();
    if (!m_infl1->isHidden() || !m_infl2->isHidden()) {
        for (QComboBox* c : {m_infl1, m_infl2}) {
            const int v = c->currentData().toInt();
            if (v >= 0 && !s.influences.contains(v))
                s.influences << v;
        }
    }
    const bool mapP1 = m_item.game == Game::Poe1 && m_item.isMap;
    s.synthesised = !mapP1 && !m_special1->isHidden() && m_special1->isChecked();
    s.blighted = mapP1 && m_special1->isChecked();
    s.blightRavaged = mapP1 && m_special2->isChecked();
    if (m_item.isHeist)
        s.heistObjective = m_alt->currentData().toString();
    if (m_item.isUltimatum) {
        s.ultimatumReward = m_alt->currentData().toString();
        s.ultimatumOutput = s.ultimatumReward == QStringLiteral("ExchangeUnique") ? m_item.ultimatumOutput : QString();
    }
    return s;
}

Lang MainPopup::server() const
{
    return static_cast<Lang>(m_server->currentData().toInt());
}

OptDouble MainPopup::priceMin() const
{
    return toOpt(m_priceMin);
}

int MainPopup::listCount() const
{
    return m_listCount->currentData().toInt();
}

bool MainPopup::collapse() const
{
    return m_collapse->isChecked();
}

QString MainPopup::tradeStatus() const
{
    return m_status->currentData().toString();
}

QString MainPopup::exchangeWant() const
{
    if (m_exchangeBox->isHidden())
        return {};
    if (m_want1->currentIndex() > 0)
        return m_want1->currentData().toString();
    if (m_want2->currentIndex() > 0)
        return m_want2->currentData().toString();
    return {};
}

void MainPopup::setIcon(const QString& imagePath)
{
    QPixmap px(imagePath);
    if (px.isNull())
        return;
    m_icon->setPixmap(px.scaled(m_icon->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_icon->setVisible(true);
}

void MainPopup::priceBusy(const QString& msg)
{
    m_prices->setRowCount(0);
    m_priceCount->clear();
    m_price->setToolTip(QStringLiteral("좌클릭: 시세 조회 / 우클릭: 시세 목록 탭"));
    priceStatus(msg);
}

void MainPopup::priceStatus(const QString& msg)
{
    m_price->setStyleSheet(QStringLiteral("text-align:right; padding:2px;"));
    m_price->setText(msg);
}

void MainPopup::priceColumns(const QStringList& headers, const QVector<int>& hidden)
{
    m_prices->setRowCount(0);
    m_prices->setColumnCount(headers.size());
    m_prices->setHorizontalHeaderLabels(headers);
    for (int c = 0; c < headers.size(); ++c)
        m_prices->setColumnHidden(c, hidden.contains(c));
}

void MainPopup::priceAddRow(const QStringList& cells, bool dim, int online, const QString& tooltip)
{
    const int r = m_prices->rowCount();
    m_prices->insertRow(r);
    const QColor dimC = dimColor(palette());
    for (int c = 0; c < cells.size() && c < m_prices->columnCount(); ++c) {
        auto* item = new QTableWidgetItem(cells[c]);
        if (c < 3)
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        if (dim)
            item->setForeground(dimC);
        if (!tooltip.isEmpty())
            item->setToolTip(tooltip);
        m_prices->setItem(r, c, item);
    }
    // 마지막 칸(판매자) 앞에 접속 상태 점
    const int last = m_prices->columnCount() - 1;
    if (QTableWidgetItem* s = m_prices->item(r, last)) {
        if (online >= 0) {
            s->setText(QStringLiteral("● ") + s->text());
            s->setForeground(online == 1 ? QColor(QStringLiteral("#2e9e5b"))
                                         : (online == 2 ? QColor(QStringLiteral("#d2891e")) : dimC));
            s->setToolTip(online == 1 ? QStringLiteral("접속 중") : (online == 2 ? QStringLiteral("자리 비움") : QStringLiteral("오프라인 (즉시구매는 가능할 수 있음)")));
        }
    }
    if (r == 0)
        m_prices->resizeColumnsToContents();
}

void MainPopup::priceDone(const QString& summary, const QString& count, const QString& tooltip)
{
    m_price->setStyleSheet(QStringLiteral("text-align:right; padding:2px;"));
    m_price->setText(summary);
    if (!tooltip.isEmpty())
        m_price->setToolTip(tooltip + QStringLiteral("\n\n좌클릭: 다시 조회 / 우클릭: 시세 목록 탭"));
    m_priceCount->setText(count);
    m_prices->resizeColumnsToContents();
    m_prices->scrollToTop();
}

void MainPopup::priceIdle(const QString& msg)
{
    m_price->setStyleSheet(QStringLiteral("text-align:right; padding:2px;"));
    m_price->setText(msg);
}

void MainPopup::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        hide();
        return;
    }
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && !qobject_cast<QLineEdit*>(focusWidget())) {
        hide();
        return;
    }
    QWidget::keyPressEvent(e);
}

void MainPopup::closeEvent(QCloseEvent* e)
{
    e->ignore();
    hide();
}

void MainPopup::hideEvent(QHideEvent* e)
{
    QWidget::hideEvent(e);
    platform::setNoActivate(this, true);
    emit popupHidden();
}

} // namespace pts
