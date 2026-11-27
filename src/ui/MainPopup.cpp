// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/MainPopup.h"

#include "core/Keywords.h"
#include "core/PriceEstimate.h"
#include "core/TradeDb.h"
#include "platform/Platform.h"
#include "ui/FilterRow.h"
#include "ui/ToggleLabel.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QPainter>
#include <QStyle>
#include <QIcon>
#include <QUrl>
#include <QRegularExpression>
#include <QStyledItemDelegate>
#include <QSet>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QScrollArea>
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

// 게임 안 아이템 이름 색 (레어도별)
QColor rarityColor(const ParsedItem& it)
{
    const QString& r = it.rarityId;
    if (it.isGem || r == QStringLiteral("gem"))
        return QColor(0x1b, 0xa2, 0x9b);
    if (it.isCurrency || r == QStringLiteral("currency"))
        return QColor(0xaa, 0x9e, 0x82);
    if (it.isCard || r == QStringLiteral("card"))
        return QColor(0x0e, 0xba, 0xff);
    if (r == QStringLiteral("unique"))
        return QColor(0xaf, 0x60, 0x25);
    if (r == QStringLiteral("rare"))
        return QColor(0xff, 0xff, 0x77);
    if (r == QStringLiteral("magic"))
        return QColor(0x88, 0x88, 0xff);
    return QColor(0xc8, 0xc8, 0xc8); // 일반
}
// 표 칸 아이콘을 글자 오른쪽에 ("1 [카오스]")
class IconRightDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

protected:
    void initStyleOption(QStyleOptionViewItem* o, const QModelIndex& idx) const override
    {
        QStyledItemDelegate::initStyleOption(o, idx);
        o->decorationPosition = QStyleOptionViewItem::Right;
    }
};
} // namespace

void MainPopup::applyRarityColor(const ParsedItem& it)
{
    // 밝은 테마에서도 노랑/회색이 보이게 배경은 게임처럼 어둡게 고정. 체크 끈 쪽은 흐리게
    const QColor c = rarityColor(it);
    m_header->setStyleSheet(QStringLiteral("QFrame#itemHeader { background:#151311; border:1px solid %1; border-radius:3px; }")
                                .arg(c.darker(220).name()));
    m_name->setStyleSheet(QStringLiteral("QLabel{color:%1; background:transparent; padding:1px 5px;}").arg(c.name()));
    m_useName->setColor(c);
    m_useType->setColor(c);
}

MainPopup::MainPopup(const Keywords& kw, QWidget* parent) : QWidget(parent), m_kw(kw)
{
    setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint | Qt::CustomizeWindowHint | Qt::WindowTitleHint |
                   Qt::WindowCloseButtonHint);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setWindowTitle(QStringLiteral("Poe Trade Search"));
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
                                        "정확: 원래 옵션, T1~T2 만 체크 (등급 모르면 일반 옵션 전부), 값 일치\n"
                                        "베이스: 옵션을 모두 끄고 아이템 레벨·영향 위주로 검색"));
    m_range = new QComboBox(this);
    m_range->setToolTip(QStringLiteral("옵션 값 완화: 최소값을 N% 낮춰서 검색 (고유는 롤 범위 폭 기준)"));

    m_server = new QComboBox(this);
    m_server->addItem(QStringLiteral("한국 서버"), li(Lang::Ko));
    m_server->addItem(QStringLiteral("글로벌 서버"), li(Lang::En));
    m_settings = new QPushButton(QStringLiteral("설정"), this);
    m_settings->setFixedWidth(52);
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
    names->setSpacing(2);
    // 이름/베이스: 게임 아이템 헤더처럼 어두운 띠 + 레어도 색. 체크 = 검색에 넣음
    m_header = new QFrame(this);
    m_header->setObjectName(QStringLiteral("itemHeader"));
    // 게임 아이템 헤더처럼 [영향][이름 / 베이스 두 줄 가운데][영향]. 이름/베이스는 눌러서 검색에 넣고 뺌
    auto* hh = new QHBoxLayout(m_header);
    hh->setContentsMargins(6, 4, 6, 4);
    hh->setSpacing(4);
    m_inflL = new QLabel(m_header);
    m_inflR = new QLabel(m_header);
    for (QLabel* b : {m_inflL, m_inflR}) {
        b->setFixedWidth(64);
        b->setAlignment(Qt::AlignCenter);
        b->setWordWrap(true);
    }
    auto* hl = new QVBoxLayout;
    hl->setSpacing(1);
    m_useName = new ToggleLabel(m_header);
    m_useName->setToolTip(QStringLiteral("고유 이름으로 검색 (누르면 켜고 끕니다)"));
    m_name = new QLabel(m_header); // 희귀 이름 (검색엔 못 씀)
    m_useType = new ToggleLabel(m_header);
    m_useType->setToolTip(QStringLiteral("베이스로 검색. 끄면 아이템 유형(한손 검 등)으로 검색합니다 (누르면 켜고 끕니다)"));
    QFont nf = m_name->font();
    nf.setBold(true);
    nf.setPointSizeF(nf.pointSizeF() + 2);
    for (QWidget* w : std::initializer_list<QWidget*>{m_useName, m_name, m_useType})
        w->setFont(nf);
    for (QWidget* w : std::initializer_list<QWidget*>{m_useName, m_name, m_useType})
        hl->addWidget(w, 0, Qt::AlignHCenter);
    hh->addWidget(m_inflL, 0, Qt::AlignVCenter);
    hh->addLayout(hl, 1);
    hh->addWidget(m_inflR, 0, Qt::AlignVCenter);
    names->addWidget(m_header);
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
    m_all = new ToggleLabel(this);
    m_all->setText(QStringLiteral("전체"));
    m_all->setToolTip(QStringLiteral("일반 옵션 전체 선택/해제 (고정·제작 옵션 제외)"));
    hdr->addWidget(m_info, 1);
    hdr->addWidget(lmin);
    hdr->addWidget(lmax);
    hdr->addWidget(m_all);
    root->addLayout(hdr);

    // ---- 본문 (옵션 / 교환 / 기타 필터)
    auto* page1 = new QWidget(this);
    auto* p1 = new QVBoxLayout(page1);
    p1->setContentsMargins(0, 2, 0, 2);

    auto* scroll = new QScrollArea(page1);
    m_scroll = scroll;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_rowsBox = new QWidget(scroll);
    m_rowsLayout = new QVBoxLayout(m_rowsBox);
    m_rowsLayout->setContentsMargins(0, 0, 6, 0); // 오른쪽 여유 (스크롤바/테두리에 안 잘리게)
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
    m_want1->setToolTip(QStringLiteral("이 아이템을 팔고 받을 화폐 (일괄 교환 시세). '일반 검색'을 고르면 아이템으로 검색합니다"));
    m_want2->setToolTip(QStringLiteral("같은 종류의 다른 아이템과 교환 시세"));
    ex->addWidget(m_want1, 1);
    ex->addWidget(m_want2, 1);
    p1->addWidget(m_exchangeBox);

    p1->addWidget(hline(page1));

    // ---- 아이템 속성 줄 (PoE Overlay 처럼 옵션 목록 위에 한 줄씩, 옵션 줄과 같은 최소/최대 열)
    //  레어도 / 아이템 레벨(젬 레벨, 지도 등급) / 퀄리티 / 홈 / 링크 / 방어도·DPS·지도 속성
    m_propRows = new QWidget(m_rowsBox);
    {
        auto* prl = new QVBoxLayout(m_propRows);
        prl->setContentsMargins(0, 0, 0, 0);
        prl->setSpacing(2);
        auto addRow = [&]() {
            auto* r = new PropRow(m_propRows);
            prl->addWidget(r);
            return r;
        };
        m_rarityRow = addRow(); // 켜면 콤보의 레어도로, 끄면 레어도 조건 없이
        m_rarityRow->setExtra(m_rarity);
        m_lvRow = addRow();
        m_qRow = addRow();
        m_sockRow = addRow();
        m_linkRow = addRow();
        m_lvMin = m_lvRow->minEdit();
        m_lvMax = m_lvRow->maxEdit();
        m_qMin = m_qRow->minEdit();
        m_qMax = m_qRow->maxEdit();
        m_sMin = m_sockRow->minEdit();
        m_sMax = m_sockRow->maxEdit();
        m_lMin = m_linkRow->minEdit();
        m_lMax = m_linkRow->maxEdit();
        const QList<QPair<QString, QString>> keys = {
            {QStringLiteral("ilvl"), QStringLiteral("아이템 레벨")},    {QStringLiteral("map_bonus"), QStringLiteral("아틀라스 획득물")},
            {QStringLiteral("map_iiq"), QStringLiteral("아이템 수량")}, {QStringLiteral("map_iir"), QStringLiteral("아이템 희귀도")},
            {QStringLiteral("map_packsize"), QStringLiteral("몬스터 무리 규모")},
            {QStringLiteral("ar"), QStringLiteral("방어도")},       {QStringLiteral("ev"), QStringLiteral("회피")},
            {QStringLiteral("es"), QStringLiteral("에너지 보호막")}, {QStringLiteral("ward"), QStringLiteral("수호")},
            {QStringLiteral("block"), QStringLiteral("막기 확률")},   {QStringLiteral("spirit"), QStringLiteral("정신력")},
            {QStringLiteral("dps"), QStringLiteral("DPS")},          {QStringLiteral("pdps"), QStringLiteral("물리 DPS")},
            {QStringLiteral("edps"), QStringLiteral("원소 DPS")}};
        for (const auto& k : keys) {
            PropWidgets pw;
            pw.key = k.first;
            pw.label = k.second;
            pw.row = addRow();
            m_props.push_back(pw);
        }
        m_mirrorRow = addRow(); // 복제된 아이템일 때만: 켜면 복제된 매물만, 끄면 무관
        m_mirrorRow->setEditsVisible(false);
        m_notFracRow = addRow(); // 분열 안 된 장비: 켜면 분열된 매물 제외
        m_notFracRow->setEditsVisible(false);
        m_notFracRow->setToolTip(QStringLiteral("켜면 분열된 매물(분열 옵션이 있는 제작 베이스)을 뺍니다"));
    }
    m_rowsLayout->insertWidget(0, m_propRows);
    m_propSep = hline(m_rowsBox);
    m_rowsLayout->insertWidget(1, m_propSep);

    // 결합 / 역병 / 영향 / 강탈·결전 / 삿된 / 타락
    m_miscBox = new QWidget(page1);
    auto* g = new QGridLayout(m_miscBox);
    g->setContentsMargins(0, 0, 0, 0);
    g->setHorizontalSpacing(6);
    g->setVerticalSpacing(3);
    m_special1 = new QCheckBox(QStringLiteral("결합"), m_miscBox);
    m_special2 = new QCheckBox(QStringLiteral("역병 침식"), m_miscBox);
    m_corrupt = new QComboBox(m_miscBox);
    // data = 타락 + 두 번 타락*10 (각각 0 무관 / 1 예 / 2 아니오)
    m_corrupt->addItem(QStringLiteral("타락: 무관"), 0);
    m_corrupt->addItem(QStringLiteral("타락: 예"), 1);
    m_corrupt->addItem(QStringLiteral("타락: 아니오"), 2);
    m_corrupt->addItem(QStringLiteral("타락: 한 번만"), 1 + 2 * 10);
    m_corrupt->addItem(QStringLiteral("타락: 두 번"), 1 + 1 * 10);
    m_corrupt->setToolTip(QStringLiteral("한 번만 = 두 번 타락 제외 / 두 번 = 두 번 타락만"));
    m_mutated = new QComboBox(m_miscBox); // 이름은 게임별로 renderMisc 에서
    m_mutated->addItem(QString(), 0);
    m_mutated->addItem(QString(), 1);
    m_mutated->addItem(QString(), 2);
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
    row2->addWidget(m_mutated);
    row2->addWidget(m_corrupt);
    g->addLayout(row2, 0, 0, 1, 3);
    p1->addWidget(m_miscBox);


    root->addWidget(page1);

    // 매물 목록 (항상 보임)
    m_prices = new QTableWidget(this);
    m_prices->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_prices->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_prices->verticalHeader()->setVisible(false);
    m_prices->verticalHeader()->setDefaultSectionSize(20);
    m_prices->horizontalHeader()->setStretchLastSection(false);
    m_prices->setShowGrid(false);
    m_prices->setAlternatingRowColors(true);
    m_prices->setWordWrap(false);
    m_prices->setMinimumHeight(170);
    m_prices->setIconSize(QSize(18, 18));
    m_prices->verticalHeader()->setDefaultSectionSize(22);
    {
        auto* d = new IconRightDelegate(m_prices);
        m_prices->setItemDelegateForColumn(0, d);
        m_prices->setItemDelegateForColumn(1, d);
    }
    m_priceCount = new QLabel(this);

    // 거래 조건 줄. 거래소 사이트 순서: 화폐 / 판매 방식 / 등록 기간
    auto* pr = new QHBoxLayout;
    m_currency = new QComboBox(this); // 게임별 목록은 setItem 에서 채움
    m_currency->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_currency->setToolTip(QStringLiteral("가격 화폐 (등가물 = 모든 화폐. 색채의 오브 같은 화폐로 올린 매물을 제외하려면 기준 화폐 또는 신성한 오브를 고르세요)"));
    pr->addWidget(m_currency);
    connect(m_currency, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() { refreshCurrencyCombo(); });
    m_status = new QComboBox(this);
    m_status->addItem(QStringLiteral("즉시 구입만"), QStringLiteral("securable"));
    m_status->addItem(QStringLiteral("즉시 구입+직접"), QStringLiteral("available"));
    m_status->addItem(QStringLiteral("직접(접속 중)"), QStringLiteral("online"));
    m_status->addItem(QStringLiteral("전부"), QStringLiteral("any"));
    m_status->setToolTip(QStringLiteral("판매 방식"));
    pr->addWidget(m_status);
    m_indexed = new QComboBox(this);
    for (const TradeChoice& c : indexedChoices())
        m_indexed->addItem(c.text, c.id);
    m_indexed->setToolTip(QStringLiteral("등록 기간 (기본값은 설정 > 등록일 제한)"));
    pr->addWidget(m_indexed);
    pr->addStretch(1);
    m_collapse = new QCheckBox(QStringLiteral("계정별 묶기"), this);
    pr->addWidget(m_collapse);
    pr->addWidget(new QLabel(QStringLiteral("최소:"), this));
    m_priceMin = numEdit(this, 44);
    m_priceMin->setToolTip(QStringLiteral("최소 가격 (PoE1 카오스 / PoE2 엑잘티드 환산)"));
    pr->addWidget(m_priceMin);
    m_listCount = new QComboBox(this);
    for (int n : {10, 20, 40, 60, 80})
        m_listCount->addItem(QStringLiteral("%1개").arg(n), n);
    m_listCount->setToolTip(QStringLiteral("목록 수 (많을수록 느림)"));
    pr->addWidget(m_listCount);
    root->addLayout(pr);
    root->addWidget(m_prices, 1);

    // 시세 줄
    m_price = new QLabel(this); // 화폐 아이콘 넣으려고 버튼 대신 라벨 (누르면 시세 조회)
    m_price->setCursor(Qt::PointingHandCursor);
    m_price->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_price->setMinimumHeight(26);
    m_price->setToolTip(QStringLiteral("누르면 시세를 다시 조회합니다"));
    m_price->installEventFilter(this);
    m_idleText = QStringLiteral("시세를 검색하려면 클릭하세요");
    setPriceText(m_idleText);
    m_refresh = new QPushButton(style()->standardIcon(QStyle::SP_BrowserReload), QStringLiteral("다시 검색"), this);
    m_refresh->setToolTip(QStringLiteral("지금 조건으로 시세를 다시 조회합니다"));
    m_refresh->setCursor(Qt::PointingHandCursor);
    connect(m_refresh, &QPushButton::clicked, this, &MainPopup::priceCheckRequested);
    {
        auto* pl = new QHBoxLayout;
        pl->addWidget(m_priceCount);
        pl->addWidget(m_price, 1);
        pl->addWidget(m_refresh);
        root->addLayout(pl);
    }

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

    // ---- 연결
    connect(m_search, &QPushButton::clicked, this, &MainPopup::openTradeRequested);
    connect(m_close, &QPushButton::clicked, this, &QWidget::hide);
    connect(m_settings, &QPushButton::clicked, this, &MainPopup::settingsRequested);
    connect(m_all, &ToggleLabel::toggled, this, [this](bool on) {
        for (FilterRow* r : m_rows) {
            if (r->isPlain() && r->isCheckable())
                r->setChecked(on);
        }
    });
    auto stale = [this]() { markStale(); };
    for (QComboBox* c : {m_rarity, m_server, m_corrupt, m_mutated, m_infl1, m_infl2, m_alt, m_status, m_currency, m_indexed, m_listCount})
        connect(c, qOverload<int>(&QComboBox::currentIndexChanged), this, stale);
    for (QCheckBox* c : {m_special1, m_special2, m_collapse})
        connect(c, &QCheckBox::toggled, this, stale);
    for (PropRow* r : {m_rarityRow, m_lvRow, m_qRow, m_sockRow, m_linkRow, m_mirrorRow, m_notFracRow})
        connect(r, &PropRow::changed, this, stale);
    // 레어도 콤보를 고르면 레어도 줄도 켬 ("모두"면 끔)
    connect(m_rarity, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this]() { m_rarityRow->setChecked(!m_rarity->currentData().toString().isEmpty()); });
    for (ToggleLabel* t : {m_useName, m_useType})
        connect(t, &ToggleLabel::toggled, this, stale);
    // 등록일 자동: 판매 방식을 바꾸면 따라감 (등록일을 직접 고르면 그때부터 고정)
    connect(m_status, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() {
        if (!m_indexedAuto)
            return;
        QSignalBlocker b(m_indexed);
        const int i = m_indexed->findData(QueryBuilder::indexedDefault(-1, tradeStatus()));
        m_indexed->setCurrentIndex(i < 0 ? 0 : i);
    });
    connect(m_indexed, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() { m_indexedAuto = false; });
    for (const PropWidgets& pw : m_props)
        connect(pw.row, &PropRow::changed, this, stale);
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
        if (o == m_price && static_cast<QMouseEvent*>(e)->button() == Qt::LeftButton) {
            emit priceCheckRequested();
            return true;
        }
        if (qobject_cast<QLineEdit*>(o) && !isActiveWindow()) {
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
    // 조건을 바꿨으면 지금 시세는 옛 조건 결과라 색으로 표시
    if (m_priceText != m_idleText && !m_priceText.endsWith(QStringLiteral("...")))
        setPriceText(m_priceText, QStringLiteral("#e75480"));
}

void MainPopup::setLeagueTitle(const QString& league)
{
    setWindowTitle(QStringLiteral("Poe Trade Search - %1").arg(league));
}

int MainPopup::rangePct() const
{
    return m_range->currentData().toInt();
}

void MainPopup::showItem(const ParsedItem& parsed, const GameDb* gdb, const Config& cfg, Lang server)
{
    m_cfg = cfg;
    showItemImpl(parsed, gdb, server);
}

void MainPopup::showItemImpl(const ParsedItem& parsed, const GameDb* gdb, Lang server)
{
    const Config& cfg = m_cfg;
    m_parsed = parsed;
    m_gdb = gdb;
    m_hasItem = true;
    m_cfgRange = cfg.searchStatRange;
    const ParsedItem& it = m_parsed;
    const TradeDb* db = gdb ? &(*gdb)[it.lang] : nullptr;

    // 이름 (고유: 이름 체크 + 베이스 체크 / 희귀: 이름 표시 + 베이스 체크 / 그 외: 베이스 체크)
    {
        const bool unique = it.rarityId == QStringLiteral("unique") && !it.name.isEmpty();
        const bool rareName = !unique && it.rarityId == QStringLiteral("rare") && !it.rawName.isEmpty() && it.rawName != it.rawType;
        QString base = it.transfigured ? it.rawType : it.type;
        if (base.isEmpty())
            base = it.rawType;
        m_useName->setText(it.name);
        m_useName->setVisible(unique);
        m_name->setText(it.rawName);
        m_name->setVisible(rareName);
        m_useType->setText(base);
        m_useType->setVisible(!base.isEmpty());
        applyRarityColor(it);
    }
    // 영향 배지: 첫째 왼쪽, 둘째 오른쪽 (게임 헤더 양쪽 아이콘 자리)
    {
        QVector<QPair<QString, QString>> badges; // 이름, 색
        static const QHash<QString, QString> inflColor = {
            {QStringLiteral("shaper_item"), QStringLiteral("#7fa8f0")},   {QStringLiteral("elder_item"), QStringLiteral("#b98be6")},
            {QStringLiteral("crusader_item"), QStringLiteral("#e8d27a")}, {QStringLiteral("redeemer_item"), QStringLiteral("#7fd3e6")},
            {QStringLiteral("hunter_item"), QStringLiteral("#8fd36b")},   {QStringLiteral("warlord_item"), QStringLiteral("#ec7a62")},
        };
        for (int i : it.influences) {
            if (i >= 0 && i < m_kw.influences().size()) {
                const auto& inf = m_kw.influences()[i];
                badges.push_back({inf.name.first(Lang::Ko), inflColor.value(inf.flag, QStringLiteral("#cccccc"))});
            }
        }
        if (it.searing)
            badges.push_back({QStringLiteral("총주교"), QStringLiteral("#ff8a3d")});
        if (it.tangled)
            badges.push_back({QStringLiteral("포식자"), QStringLiteral("#5fd0b8")});
        auto setBadge = [](QLabel* l, const QVector<QPair<QString, QString>>& b) {
            QStringList names;
            for (const auto& x : b)
                names << x.first;
            l->setText(names.join(QLatin1Char('\n')));
            const QString c = b.isEmpty() ? QStringLiteral("transparent") : b.first().second;
            l->setStyleSheet(b.isEmpty() ? QStringLiteral("QLabel{background:transparent;}")
                                         : QStringLiteral("QLabel{color:%1; border:1px solid %1; border-radius:8px; "
                                                          "padding:1px 3px; font-weight:bold; background:transparent;}")
                                               .arg(c));
        };
        const auto left = badges.mid(0, 1), right = badges.mid(1);
        setBadge(m_inflL, left);
        setBadge(m_inflR, right);
        m_inflL->setVisible(!badges.isEmpty());
        m_inflR->setVisible(!badges.isEmpty());
    }
    m_icon->clear();
    m_icon->setVisible(false);

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
        m_range->addItem(QStringLiteral("일치"), 0);
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

    // 젬/화폐/카드 설명 문단은 안 보여 줌 (게임에서 보면 됨)
    m_detail->clear();
    m_detail->setVisible(false);

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
    m_lastRowKey.clear();
    m_prices->setColumnCount(0);
    m_priceCount->clear();
    {
        QSignalBlocker b1(m_listCount), b2(m_status), b3(m_collapse), b4(m_currency), b5(m_indexed);
        m_indexedAuto = cfg.searchBeforeDay < 0;
        const int ii = m_indexed->findData(QueryBuilder::indexedDefault(cfg.searchBeforeDay, cfg.tradeStatus));
        m_indexed->setCurrentIndex(ii < 0 ? 0 : ii);
        const int lc = m_listCount->findData(cfg.searchListCount);
        m_listCount->setCurrentIndex(lc < 0 ? 1 : lc);
        const int si = m_status->findData(cfg.tradeStatus);
        m_status->setCurrentIndex(si < 0 ? 0 : si);
        m_collapse->setChecked(cfg.collapseListings);
        m_currency->clear();
        for (const TradeChoice& pc : priceCurrencies(it.game)) {
            m_currency->addItem(pc.text, pc.id);
            m_currency->setItemData(m_currency->count() - 1, pc.text, Qt::ToolTipRole); // 원래 이름 (아이콘으로 줄여도)
        }
        const QString cur = cfg.priceCurrencyFor(it.game);
        int ci = m_currency->findData(cur);
        if (ci < 0) { // 설정 파일에 목록 밖 값을 직접 넣은 경우
            m_currency->addItem(cur, cur);
            ci = m_currency->count() - 1;
        }
        m_currency->setCurrentIndex(ci);
        refreshCurrencyCombo();
    }
    m_priceMin->setText(cfg.searchPriceMinimum > 0 ? QString::number(cfg.searchPriceMinimum) : QString());
    priceIdle(m_idleText);
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
    fitRowsArea();
}

void MainPopup::fitRowsArea()
{
    // 속성 줄 묶음 / 구분선
    bool anyProp = false;
    for (PropRow* r : m_propRows->findChildren<PropRow*>())
        anyProp = anyProp || !r->isHidden();
    m_propRows->setVisible(anyProp);
    m_propSep->setVisible(anyProp && !m_rows.isEmpty());

    // 옵션 영역은 내용만큼만 (빈 공간은 매물 목록에). 너무 많으면 스크롤
    const int w = qMax(m_scroll->viewport()->width(), 600);
    int h = 0;
    for (int i = 0; i < m_rowsLayout->count(); ++i) {
        QWidget* x = m_rowsLayout->itemAt(i)->widget();
        if (!x || x->isHidden())
            continue;
        h += (x->hasHeightForWidth() ? x->heightForWidth(w) : x->sizeHint().height()) + m_rowsLayout->spacing();
    }
    // 줄바꿈 라벨은 최소 높이를 넉넉히 잡아서, 그보다 작으면 스크롤바가 생김
    m_rowsLayout->activate();
    h = qMax(h, m_rowsBox->minimumSizeHint().height());
    m_scroll->setMinimumHeight(qBound(60, h + 6, 420));
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
    QSignalBlocker b4(m_special1), b5(m_special2), b6(m_corrupt), b6b(m_mutated), b7(m_infl1), b8(m_infl2), b9(m_alt),
        b10(m_useName), b10b(m_useType), b11(m_rarity);
    const QColor propGray(0xc8, 0xc8, 0xc8), warn(0xe0, 0x66, 0x66);

    // 레어도 줄: 켜면 콤보 값으로 검색, 끄면 레어도 조건 없음
    m_rarityRow->setText(QStringLiteral("아이템 희귀도: %1").arg(it.rarityText));
    m_rarityRow->setVisible(!it.rarityText.isEmpty() && !it.isCurrency && !it.isCard && !it.isGem);
    m_rarityRow->setChecked(!st.rarity.isEmpty());
    // 복제된 아이템 줄
    m_mirrorRow->setText(QStringLiteral("복제된 아이템"));
    m_mirrorRow->setVisible(it.mirrored);
    m_mirrorRow->setChecked(st.mirrored == 1);
    // 분열 속성 없음 줄 (분열 안 된 고유 제외 장비만)
    m_notFracRow->setText(QStringLiteral("분열 속성 없음"));
    m_notFracRow->setVisible((it.isGear || it.isJewel) && it.rarityId != QStringLiteral("unique") && !it.fractured);
    m_notFracRow->setChecked(st.notFractured);
    // 레벨 줄: 아이템 레벨 / 젬 레벨 / 지도 등급
    {
        const auto kind = levelKindFor(it);
        const int v = kind == SearchRequest::LevelKind::GemLevel  ? it.level
                      : kind == SearchRequest::LevelKind::MapTier ? it.mapTier
                                                                   : it.itemLevel;
        const QString name = kind == SearchRequest::LevelKind::GemLevel  ? QStringLiteral("젬 레벨")
                             : kind == SearchRequest::LevelKind::MapTier ? QStringLiteral("지도 등급")
                                                                          : QStringLiteral("아이템 레벨");
        m_lvRow->setText(QStringLiteral("%1: %2").arg(name).arg(v));
        m_lvRow->setVisible(v > 0);
        m_lvRow->setChecked(st.useLevel);
        m_lvRow->setValues(st.levelMin, st.levelMax);
        m_lvRow->setColor(it.highlightLevel ? warn : propGray);
    }
    // 퀄리티 / 홈 / 링크 줄
    m_qRow->setText(QStringLiteral("퀄리티: +%1%").arg(it.quality));
    m_qRow->setVisible(it.quality > 0);
    m_qRow->setChecked(st.useQuality);
    m_qRow->setValues(st.qualityMin, st.qualityMax);
    m_qRow->setColor(it.highlightQuality ? warn : propGray);
    m_sockRow->setText((poe1 ? QStringLiteral("홈: %1") : QStringLiteral("룬 홈: %1")).arg(it.sockets));
    m_sockRow->setVisible(it.sockets > 0);
    m_sockRow->setChecked(st.useSockets);
    m_sockRow->setValues(st.socketsMin, st.socketsMax);
    m_linkRow->setText(QStringLiteral("링크: %1").arg(it.links));
    m_linkRow->setVisible(poe1 && it.links > 1);
    m_linkRow->setChecked(st.useSockets);
    m_linkRow->setValues(st.linksMin, st.linksMax);

    // 결합 / 역병
    const bool mapP1 = poe1 && it.isMap;
    m_special1->setVisible(poe1 && (it.isGear || mapP1));
    m_special1->setText(mapP1 ? QStringLiteral("역병") : QStringLiteral("결합"));
    m_special1->setChecked(mapP1 ? st.blighted : st.synthesised);
    m_special2->setVisible(mapP1);
    m_special2->setChecked(st.blightRavaged);

    {
        const int twice = st.corrupted == 1 ? st.twiceCorrupted : 0;
        const int ci = m_corrupt->findData(st.corrupted + twice * 10);
        m_corrupt->setCurrentIndex(ci < 0 ? 0 : ci);
    }
    setBold(m_corrupt, it.corrupted, QStringLiteral("#e06666"));

    // 삿된(PoE1) / 함양된 바알 고유(PoE2): 같은 거래소 필터(mutated), 고유만
    const QString mu = poe1 ? QStringLiteral("삿된") : QStringLiteral("함양된 바알");
    m_mutated->setItemText(0, mu + QStringLiteral(": 무관"));
    m_mutated->setItemText(1, mu + QStringLiteral(": 예"));
    m_mutated->setItemText(2, mu + QStringLiteral(": 아니오"));
    m_mutated->setCurrentIndex(qBound(0, st.mutated, 2));
    m_mutated->setVisible(it.rarityId == QStringLiteral("unique"));
    setBold(m_mutated, it.foulborn, QStringLiteral("#e06666"));

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

    // 방어도 / DPS / 지도 속성 줄 (값은 거래소 기준: 방어·물리 DPS 는 퀄리티 20%)
    {
        static const QSet<QString> q20 = {QStringLiteral("ar"), QStringLiteral("ev"), QStringLiteral("es"),
                                          QStringLiteral("ward"), QStringLiteral("dps"), QStringLiteral("pdps")};
        for (const PropWidgets& pw : m_props) {
            const PropFilterState* ps = nullptr;
            for (const auto& x : st.props) {
                if (x.key == pw.key)
                    ps = &x;
            }
            double v = 0;
            for (const ItemProp& ip : it.props) {
                if (ip.key == pw.key)
                    v = ip.value;
            }
            QString val;
            if (pw.key.startsWith(QStringLiteral("map_")))
                val = QStringLiteral("+%1%").arg(qRound(v));
            else if (pw.key == QStringLiteral("block"))
                val = QStringLiteral("%1%").arg(qRound(v));
            else if (pw.key.endsWith(QStringLiteral("dps")))
                val = QString::number(v, 'f', 1);
            else
                val = QString::number(qRound(v));
            const bool atQ20 = q20.contains(pw.key) && it.quality < 20;
            pw.row->setText(QStringLiteral("%1: %2%3").arg(pw.label, val, atQ20 ? QStringLiteral(" (퀄 20)") : QString()));
            pw.row->setToolTip(q20.contains(pw.key) ? QStringLiteral("거래소는 퀄리티 20% 기준 값으로 검색합니다")
                                                    : QStringLiteral("누르면 검색에 넣거나 뺍니다"));
            pw.row->setVisible(ps != nullptr);
            pw.row->setChecked(ps && ps->use);
            pw.row->setValues(ps ? ps->min : OptDouble(), ps ? ps->max : OptDouble());
        }
    }
    m_useName->setChecked(st.useName && !st.byCategory);
    m_useType->setChecked(st.useType && !st.byCategory);
    // 콤보는 꺼져 있어도 아이템 기본 레어도(고유 / 고유 제외)를 보여 줌
    const int ri = m_rarity->findData(st.rarity.isEmpty() ? it.rarityFilter : st.rarity);
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
    s.useLevel = !m_lvRow->isHidden() && m_lvRow->isChecked();
    s.levelMin = toOpt(m_lvMin);
    s.levelMax = toOpt(m_lvMax);
    s.useQuality = !m_qRow->isHidden() && m_qRow->isChecked();
    s.qualityMin = toOpt(m_qMin);
    s.qualityMax = toOpt(m_qMax);
    // 홈 / 링크는 줄마다 켜고 끔 (꺼진 쪽 값은 안 보냄)
    const bool sockOn = !m_sockRow->isHidden() && m_sockRow->isChecked();
    const bool linkOn = !m_linkRow->isHidden() && m_linkRow->isChecked() && m_item.game == Game::Poe1;
    s.useSockets = sockOn || linkOn;
    s.socketsMin = sockOn ? toOpt(m_sMin) : std::nullopt;
    s.socketsMax = sockOn ? toOpt(m_sMax) : std::nullopt;
    s.linksMin = linkOn ? toOpt(m_lMin) : std::nullopt;
    s.linksMax = linkOn ? toOpt(m_lMax) : std::nullopt;
    {
        const int v = m_corrupt->currentData().toInt();
        s.corrupted = v % 10;
        s.twiceCorrupted = v / 10;
    }
    s.mutated = m_mutated->isHidden() ? 0 : m_mutated->currentIndex();
    s.rarity = !m_rarityRow->isHidden() && m_rarityRow->isChecked() ? m_rarity->currentData().toString() : QString();
    if (!m_mirrorRow->isHidden())
        s.mirrored = m_mirrorRow->isChecked() ? 1 : 0;
    s.notFractured = !m_notFracRow->isHidden() && m_notFracRow->isChecked();
    s.byCategory = false;
    s.exact = static_cast<Preset>(m_preset->currentData().toInt()) == Preset::Exact;
    for (PropFilterState& p : s.props) {
        for (const PropWidgets& pw : m_props) {
            if (pw.key == p.key && !pw.row->isHidden()) {
                p.use = pw.row->isChecked();
                p.min = pw.row->minValue();
                p.max = pw.row->maxValue();
            }
        }
    }
    s.useName = m_useName->isHidden() || m_useName->isChecked();
    s.useType = m_useType->isHidden() || m_useType->isChecked();
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

QString MainPopup::priceCurrency() const
{
    return m_currency->currentData().toString();
}

QString MainPopup::indexed() const
{
    return m_indexed->currentData().toString();
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
    m_lastRowKey.clear();
    m_priceCount->clear();
    m_price->setToolTip(QStringLiteral("누르면 시세를 다시 조회합니다"));
    priceStatus(msg);
}

void MainPopup::priceStatus(const QString& msg)
{
    setPriceText(msg);
}

void MainPopup::priceColumns(const QStringList& headers, const QVector<int>& hidden)
{
    m_prices->setRowCount(0);
    m_lastRowKey.clear();
    m_prices->setColumnCount(headers.size());
    m_prices->setHorizontalHeaderLabels(headers);
    int lastVisible = -1;
    for (int c = 0; c < headers.size(); ++c) {
        m_prices->setColumnHidden(c, hidden.contains(c));
        m_prices->horizontalHeader()->setSectionResizeMode(c, QHeaderView::Interactive);
        if (!hidden.contains(c))
            lastVisible = c;
    }
    // 숨긴 열이 끝에 있어도 보이는 마지막 열(판매자)이 남는 폭을 채우게
    m_lastPriceCol = lastVisible;
    if (lastVisible >= 0)
        m_prices->horizontalHeader()->setSectionResizeMode(lastVisible, QHeaderView::Stretch);
}

void MainPopup::priceAddRow(const QStringList& cells, bool dim, int online, const QString& tooltip)
{
    // 같은 판매자가 같은 가격으로 연달아 올린 건 한 줄로 묶고 "×N" (APT)
    const QString key = cells.isEmpty() ? QString() : cells.first() + QLatin1Char('\x1f') + cells.last();
    if (!cells.isEmpty() && !cells.last().isEmpty() && key == m_lastRowKey && m_prices->rowCount() > 0) {
        ++m_lastRowCount;
        if (QTableWidgetItem* s = m_prices->item(m_prices->rowCount() - 1, m_prices->columnCount() - 1)) {
            QString t = s->text();
            const int x = t.lastIndexOf(QStringLiteral("  ×"));
            if (x >= 0)
                t = t.left(x);
            s->setText(t + QStringLiteral("  ×%1").arg(m_lastRowCount));
            s->setToolTip(QStringLiteral("같은 판매자가 같은 가격으로 %1개 올림").arg(m_lastRowCount));
        }
        return;
    }
    m_lastRowKey = key;
    m_lastRowCount = 1;
    const int r = m_prices->rowCount();
    m_prices->insertRow(r);
    const QColor dimC = dimColor(palette());
    for (int c = 0; c < cells.size() && c < m_prices->columnCount(); ++c) {
        auto* item = new QTableWidgetItem(cells[c]);
        if (c < 2)
            applyUnitIcon(item); // "1 카오스" -> "1" + 카오스 아이콘
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
            s->setToolTip(online == 1 ? QStringLiteral("접속 중") : (online == 2 ? QStringLiteral("자리 비움") : QStringLiteral("오프라인 (즉시 구입은 가능할 수 있습니다)")));
        }
    }
    if (r == 0)
        fitPriceColumns();
}

void MainPopup::fitPriceColumns()
{
    // 마지막 열(판매자)은 늘려서 남는 폭을 채우고, 나머지만 내용에 맞춤
    for (int c = 0; c < m_prices->columnCount(); ++c) {
        if (c != m_lastPriceCol && !m_prices->isColumnHidden(c))
            m_prices->resizeColumnToContents(c);
    }
}

QString MainPopup::withUnitIcons(const QString& plain) const
{
    // "≈ 29 디바인" 의 화폐 이름 -> 아이콘. 맨 앞 금액 부분은 굵게
    QString html = plain.toHtmlEscaped();
    bool replaced = false;
    for (auto u = m_unitIcons.constBegin(); u != m_unitIcons.constEnd(); ++u) {
        const QRegularExpression re(QStringLiteral("(\\d) %1(?=\\s|$|\\))").arg(QRegularExpression::escape(u.key().toHtmlEscaped())));
        if (!re.match(html).hasMatch())
            continue;
        const QString img = QStringLiteral("\\1 <img src=\"%1\" width=\"20\" height=\"20\" align=\"middle\">")
                                .arg(QUrl::fromLocalFile(u.value()).toString());
        html.replace(re, img);
        replaced = true;
    }
    if (!replaced)
        return {};
    // "≈ 29 [아이콘]  (..." 앞부분 굵게
    const int cut = html.indexOf(QStringLiteral("  ("));
    if (html.startsWith(QStringLiteral("≈")) && cut > 0)
        html = QStringLiteral("<b>") + html.left(cut) + QStringLiteral("</b>") + html.mid(cut);
    return html;
}

void MainPopup::setPriceText(const QString& plain, const QString& color)
{
    // 매물이 없거나 조건이 바뀌어 다시 찾아야 할 때 다시 검색 버튼을 눈에 띄게
    const bool attention = plain.contains(QStringLiteral("없습니다")) || plain.contains(QStringLiteral("못했습니다")) ||
                           !color.isEmpty();
    if (m_refresh)
        m_refresh->setStyleSheet(attention ? QStringLiteral("QPushButton{border:1px solid #d2a03c; color:#f0c060; padding:2px 8px;}")
                                           : QString());
    m_priceText = plain;
    m_priceColor = color;
    m_price->setStyleSheet(QStringLiteral("QLabel{padding:2px 4px; %1}")
                               .arg(color.isEmpty() ? QString() : QStringLiteral("color:%1;").arg(color)));
    const QString html = withUnitIcons(plain);
    m_price->setTextFormat(html.isEmpty() ? Qt::PlainText : Qt::RichText);
    m_price->setText(html.isEmpty() ? plain : html);
}

void MainPopup::applyUnitIcon(QTableWidgetItem* item) const
{
    const QString t = item->text();
    const int sp = t.indexOf(QLatin1Char(' '));
    if (sp <= 0)
        return;
    const QString path = m_unitIcons.value(t.mid(sp + 1));
    if (path.isEmpty())
        return;
    item->setText(t.left(sp));
    item->setIcon(QIcon(path));
    if (item->toolTip().isEmpty())
        item->setToolTip(t);
}

void MainPopup::refreshCurrencyCombo()
{
    // 화폐 이름 대신 아이콘으로 폭 줄이기: 등가물 = [기준] 등가물, A 또는 신성 = [A][신성], 단일 = [A]
    // 아이콘을 아직 못 받았으면 원래 이름 그대로
    const QString base = m_parsed.game == Game::Poe2 ? QStringLiteral("exalted") : QStringLiteral("chaos");
    const QSize one(18, 18);
    for (int i = 0; i < m_currency->count(); ++i) {
        const QString id = m_currency->itemData(i).toString();
        const QString full = m_currency->itemData(i, Qt::ToolTipRole).toString();
        QStringList ids;
        QString shortText;
        const bool equiv = id.isEmpty(); // 등가물: [기준 화폐][≈] 를 그림 (글자 없이 폭 줄이기)
        if (equiv) {
            ids << base;
        } else if (id.endsWith(QStringLiteral("_divine"))) {
            ids << id.section(QLatin1Char('_'), 0, 0) << QStringLiteral("divine");
        } else {
            ids << id;
        }
        QStringList paths;
        for (const QString& c : ids) {
            const QString p = m_unitIcons.value(shortCurrencyName(c));
            if (!p.isEmpty())
                paths << p;
        }
        if (paths.size() != ids.size()) {
            m_currency->setItemIcon(i, QIcon());
            m_currency->setItemText(i, full.isEmpty() ? m_currency->itemText(i) : full);
            continue;
        }
        QPixmap pm(one.width() * 2 + 2, one.height());
        pm.fill(Qt::transparent);
        {
            QPainter p(&pm);
            for (int k = 0; k < paths.size(); ++k)
                p.drawPixmap(k * (one.width() + 2), 0, QPixmap(paths[k]).scaled(one, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            if (equiv) {
                QFont f = p.font();
                f.setBold(true);
                f.setPixelSize(one.height());
                p.setFont(f);
                p.setPen(QColor(0xe0, 0xe0, 0xe0));
                p.drawText(QRect(one.width() + 2, 0, one.width(), one.height()), Qt::AlignCenter, QStringLiteral("≈"));
            }
        }
        m_currency->setItemIcon(i, QIcon(pm));
        m_currency->setItemText(i, shortText);
    }
    m_currency->setIconSize(QSize(one.width() * 2 + 2, one.height()));
    m_currency->setToolTip(QStringLiteral("가격 화폐: %1\n(등가물 = 모든 화폐. 색채의 오브 같은 화폐로 올린 매물을 제외하려면 기준 화폐 또는 신성한 오브를 고르세요)")
                               .arg(m_currency->currentData(Qt::ToolTipRole).toString()));
}

void MainPopup::setUnitIcon(const QString& unitName, const QString& path)
{
    if (unitName.isEmpty() || path.isEmpty() || m_unitIcons.value(unitName) == path)
        return;
    m_unitIcons.insert(unitName, path);
    refreshCurrencyCombo();
    // 이미 그린 줄 / 추정가에도 반영
    for (int r = 0; r < m_prices->rowCount(); ++r) {
        for (int c = 0; c < 2 && c < m_prices->columnCount(); ++c) {
            if (QTableWidgetItem* it = m_prices->item(r, c))
                applyUnitIcon(it);
        }
    }
    setPriceText(m_priceText, m_priceColor);
}

void MainPopup::priceDone(const QString& summary, const QString& count, const QString& tooltip)
{
    setPriceText(summary);
    if (!tooltip.isEmpty())
        m_price->setToolTip(tooltip + QStringLiteral("\n\n누르면 다시 조회합니다"));
    m_priceCount->setText(count);
    fitPriceColumns();
    m_prices->scrollToTop();
}

void MainPopup::priceIdle(const QString& msg)
{
    setPriceText(msg);
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
