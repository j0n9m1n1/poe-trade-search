// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/SettingsDialog.h"

#include "core/CheckedStore.h"
#include "core/QueryBuilder.h"
#include "core/TradeDb.h"
#include "ui/HotkeyEdit.h"
#include "ui/UiChoices.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace pts {

SettingsDialog::SettingsDialog(const Config& cfg, const GameDb* dbs, CheckedStore* checked, StatTextFn statText,
                               const QString& versionInfo,
                               const QString& appUpdateText, QWidget* parent)
    : QDialog(parent), m_cfg(cfg), m_checked(checked), m_statText(std::move(statText))
{
    setWindowTitle(QStringLiteral("설정"));
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
    resize(560, 520);

    auto* root = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);
    root->addWidget(tabs, 1);

    // ---------------- 기본
    auto* basicScroll = new QScrollArea(tabs);
    basicScroll->setWidgetResizable(true);
    basicScroll->setFrameShape(QFrame::NoFrame);
    auto* basic = new QWidget(basicScroll);
    auto* f = new QFormLayout(basic);

    m_game = new QComboBox(basic);
    m_game->addItem(QStringLiteral("자동 (게임 창 제목으로 판단)"), QStringLiteral("auto"));
    m_game->addItem(QStringLiteral("PoE1 고정"), QStringLiteral("poe1"));
    m_game->addItem(QStringLiteral("PoE2 고정"), QStringLiteral("poe2"));
    m_game->setCurrentIndex(qMax(0, m_game->findData(cfg.game)));
    f->addRow(QStringLiteral("게임:"), m_game);

    m_league1 = new QComboBox(basic);
    m_league2 = new QComboBox(basic);
    fillLeagues(m_league1, dbs[0], cfg.league);
    fillLeagues(m_league2, dbs[1], cfg.leaguePoe2);
    f->addRow(QStringLiteral("PoE1 리그:"), m_league1);
    f->addRow(QStringLiteral("PoE2 리그:"), m_league2);

    m_server = new QComboBox(basic);
    m_server->addItem(QStringLiteral("자동 (아이템 언어)"));
    m_server->addItem(QStringLiteral("한국 (카카오)"));
    m_server->addItem(QStringLiteral("글로벌 (GGG)"));
    m_server->setCurrentIndex(qBound(0, cfg.serverType, 2));
    f->addRow(QStringLiteral("검색 서버:"), m_server);

    m_status = new QComboBox(basic);
    for (const auto& sc : ui::tradeStatusChoices())
        m_status->addItem(sc.first, sc.second);
    m_status->setCurrentIndex(qMax(0, m_status->findData(cfg.tradeStatus)));
    f->addRow(QStringLiteral("판매 방식:"), m_status);

    // 가격 화폐: 등가물이면 색채 같은 잡화폐로 올린 매물까지 다 나옴
    auto currencyBox = [&](Game g, const QString& cur) {
        auto* c = new QComboBox(basic);
        for (const TradeChoice& pc : priceCurrencies(g))
            c->addItem(pc.text, pc.id);
        if (c->findData(cur) < 0)
            c->addItem(cur, cur);
        c->setCurrentIndex(c->findData(cur));
        c->setToolTip(QStringLiteral("이 화폐로 가격을 올린 매물만 봅니다. 등가물 = 모든 화폐"));
        return c;
    };
    m_currency1 = currencyBox(Game::Poe1, cfg.priceCurrency);
    f->addRow(QStringLiteral("가격 화폐 (PoE1):"), m_currency1);
    m_currency2 = currencyBox(Game::Poe2, cfg.priceCurrencyPoe2);
    f->addRow(QStringLiteral("가격 화폐 (PoE2):"), m_currency2);

    m_beforeDay = new QComboBox(basic);
    for (auto p : {qMakePair(-1, QStringLiteral("자동 (즉시 구입만: 언제든 / 직접 거래 포함: 3일)")),
                   qMakePair(0, QStringLiteral("모두")), qMakePair(1, QStringLiteral("1일 이내")),
                   qMakePair(3, QStringLiteral("3일 이내")), qMakePair(7, QStringLiteral("1주 이내")),
                   qMakePair(14, QStringLiteral("2주 이내")),
                   qMakePair(30, QStringLiteral("1달 이내")), qMakePair(60, QStringLiteral("2달 이내"))})
        m_beforeDay->addItem(p.second, p.first);
    {
        int idx = 0;
        for (int i = 0; i < m_beforeDay->count(); ++i) {
            if (m_beforeDay->itemData(i).toInt() <= cfg.searchBeforeDay)
                idx = i;
        }
        m_beforeDay->setCurrentIndex(idx);
    }
    f->addRow(QStringLiteral("등록일 제한:"), m_beforeDay);

    m_listCount = new QComboBox(basic);
    for (int n : {10, 20, 40, 60, 80})
        m_listCount->addItem(QStringLiteral("%1개").arg(n), n);
    m_listCount->setCurrentIndex(qMax(0, m_listCount->findData(cfg.searchListCount)));
    f->addRow(QStringLiteral("시세 목록 수 (많을수록 느림):"), m_listCount);

    m_priceMin = new QSpinBox(basic);
    m_priceMin->setRange(0, 100000);
    m_priceMin->setValue(cfg.searchPriceMinimum);
    f->addRow(QStringLiteral("시세 최소 가격 (0 = 전부):"), m_priceMin);

    m_statRange = new QSpinBox(basic);
    m_statRange->setRange(0, 50);
    m_statRange->setSuffix(QStringLiteral("%"));
    m_statRange->setValue(cfg.searchStatRange);
    m_statRange->setToolTip(QStringLiteral("옵션 최소값을 값의 N%만큼 낮춰서 채웁니다 (0 = 일치). 고유는 롤 범위 폭을 기준으로 합니다"));
    f->addRow(QStringLiteral("옵션 값 완화:"), m_statRange);
    m_collapse = new QCheckBox(QStringLiteral("시세 조회 시 계정별로 묶기 (한 판매자의 매물이 목록을 채우지 않도록)"), basic);
    m_collapse->setChecked(cfg.collapseListings);
    f->addRow(m_collapse);

    m_autoPrice = new QCheckBox(QStringLiteral("창이 뜨면 바로 시세 조회"), basic);
    m_autoPrice->setChecked(cfg.autoPriceCheck);
    f->addRow(m_autoPrice);
    m_autoPriceRefresh = new QCheckBox(QStringLiteral("검색 조건을 바꾸면 시세를 자동으로 다시 조회합니다."), basic);
    m_autoPriceRefresh->setChecked(cfg.autoPriceRefresh);
    f->addRow(m_autoPriceRefresh);
    m_popupPos = new QComboBox(basic);
    m_popupPos->addItem(QStringLiteral("화면 가운데"), QStringLiteral("center"));
    m_popupPos->addItem(QStringLiteral("마우스 커서 근처"), QStringLiteral("cursor"));
    m_popupPos->addItem(QStringLiteral("마지막 위치"), QStringLiteral("last"));
    m_popupPos->setCurrentIndex(qMax(0, m_popupPos->findData(cfg.popupPosition)));
    f->addRow(QStringLiteral("창 위치:"), m_popupPos);
    m_clipWatch = new QCheckBox(QStringLiteral("게임에서 Ctrl+C 로 복사해도 창 띄우기 ({Run} 단축키와 별개)"), basic);
    m_clipWatch->setChecked(cfg.clipboardWatch);
    f->addRow(m_clipWatch);
    m_outsideClose = new QCheckBox(QStringLiteral("게임 화면을 누르면 창 닫기 ({Last} 단축키/트레이로 다시 열기)"), basic);
    m_outsideClose->setChecked(cfg.closeOnOutsideClick);
    f->addRow(m_outsideClose);
    m_unique = new QCheckBox(QStringLiteral("고유 아이템은 옵션 전부 체크"), basic);
    m_unique->setChecked(cfg.autoCheckUnique);
    f->addRow(m_unique);
    m_pseudo = new QCheckBox(QStringLiteral("가능하면 유사(pseudo) 옵션으로 검색"), basic);
    m_pseudo->setChecked(cfg.autoSelectPseudo);
    f->addRow(m_pseudo);
    m_totalres = new QCheckBox(QStringLiteral("저항은 합산으로 검색"), basic);
    m_totalres->setChecked(cfg.autoCheckTotalres);
    f->addRow(m_totalres);

    m_corrupt = new QComboBox(basic);
    m_corrupt->addItem(QStringLiteral("아이템 상태 따름 (비움)"), QString());
    m_corrupt->addItem(QStringLiteral("타락만 (yes)"), QStringLiteral("yes"));
    m_corrupt->addItem(QStringLiteral("비타락만 (no)"), QStringLiteral("no"));
    {
        const int ci = m_corrupt->findData(cfg.autoSelectCorrupt.trimmed().toLower());
        m_corrupt->setCurrentIndex(ci < 0 ? 0 : ci);
    }
    f->addRow(QStringLiteral("장비 타락 기본값:"), m_corrupt);
    m_byType = new QLineEdit(cfg.autoSelectByType, basic);
    m_byType->setPlaceholderText(QStringLiteral("예: jewel,accessory.ring"));
    f->addRow(QStringLiteral("유형으로 검색할 카테고리:"), m_byType);

    m_timeout = new QSpinBox(basic);
    m_timeout->setRange(1, 60);
    m_timeout->setValue(cfg.serverTimeout);
    m_timeout->setSuffix(QStringLiteral("초"));
    f->addRow(QStringLiteral("서버 대기 시간:"), m_timeout);
    m_redirect = new QCheckBox(QStringLiteral("브라우저에서 직접 검색 (server_redirect)"), basic);
    m_redirect->setChecked(cfg.serverRedirect);
    f->addRow(m_redirect);

    m_appUpdate = new QComboBox(basic);
    m_appUpdate->addItem(QStringLiteral("새 버전 알림만"), QStringLiteral("notify"));
    m_appUpdate->addItem(QStringLiteral("자동으로 받아서 설치"), QStringLiteral("auto"));
    m_appUpdate->addItem(QStringLiteral("확인 안 함"), QStringLiteral("off"));
    m_appUpdate->setCurrentIndex(qMax(0, m_appUpdate->findData(cfg.appUpdate)));
    m_appUpdate->setToolTip(QStringLiteral("GitHub Release를 기준으로 확인합니다. 자동 설치는 배포 빌드에서만 동작합니다"));
    {
        auto* row = new QHBoxLayout;
        row->addWidget(m_appUpdate, 1);
        m_appUpdateBtn = new QPushButton(appUpdateText.isEmpty() ? QStringLiteral("지금 확인") : appUpdateText, basic);
        m_appUpdateBtn->setToolTip(QStringLiteral("새 버전을 확인하고, 있으면 받아서 설치합니다"));
        connect(m_appUpdateBtn, &QPushButton::clicked, this, [this]() {
            m_appUpdateBtn->setEnabled(false);
            m_appUpdateBtn->setText(QStringLiteral("진행 중... (결과는 트레이 알림)"));
            emit appUpdateRequested();
        });
        row->addWidget(m_appUpdateBtn);
        f->addRow(QStringLiteral("프로그램 업데이트:"), row);
    }

    m_updates = new QCheckBox(QStringLiteral("시작할 때 거래소 데이터가 오래되었으면 자동 갱신"), basic);
    m_updates->setChecked(cfg.checkUpdates);
    f->addRow(m_updates);
    m_dataAge = new QSpinBox(basic);
    m_dataAge->setRange(1, 90);
    m_dataAge->setValue(cfg.dataMaxAgeDays);
    m_dataAge->setSuffix(QStringLiteral("일"));
    f->addRow(QStringLiteral("데이터 갱신 주기:"), m_dataAge);

    auto* upd = new QHBoxLayout;
    m_dataBtn[0] = new QPushButton(QStringLiteral("PoE1 데이터 갱신"), basic);
    m_dataBtn[1] = new QPushButton(QStringLiteral("PoE2 데이터 갱신"), basic);
    connect(m_dataBtn[0], &QPushButton::clicked, this, [this]() {
        m_dataBtn[0]->setEnabled(false);
        emit updateDataRequested(0);
    });
    connect(m_dataBtn[1], &QPushButton::clicked, this, [this]() {
        m_dataBtn[1]->setEnabled(false);
        emit updateDataRequested(1);
    });
    upd->addWidget(m_dataBtn[0]);
    upd->addWidget(m_dataBtn[1]);
    f->addRow(upd);
    auto* ver = new QLabel(versionInfo, basic);
    ver->setStyleSheet(QStringLiteral("color:palette(mid);"));
    f->addRow(ver);
    basicScroll->setWidget(basic);
    tabs->addTab(basicScroll, QStringLiteral("기본"));

    // ---------------- 단축키
    auto* keys = new QWidget(tabs);
    auto* kl = new QVBoxLayout(keys);
    auto* help = new QLabel(QStringLiteral("PoE 창이 활성 상태일 때만 동작합니다. 값 예시:\n"
                                           "  {Run} 아이템 검색 (기본 Alt+D, Ctrl+Alt+C 를 대신 눌러 아이템 정보를 가져옵니다)   {Last} 마지막 창 다시 열기\n"
                                           "  {Enter}/hideout{Enter} 은신처   {Enter}/exit{Enter} 로그아웃(캐릭터 선택)\n"
                                           "  ^{Enter}감사합니다{Enter} 마지막 귓속말에 답장 (키 한 번에 명령 하나)\n"
                                           "  {Pause} 일시정지   {Restart} 재시작   {Link}URL{Link} 링크 열기"),
                            keys);
    help->setStyleSheet(QStringLiteral("color:palette(mid);"));
    kl->addWidget(help);
    m_shortcuts = new QTableWidget(0, 2, keys);
    m_shortcuts->setHorizontalHeaderLabels({QStringLiteral("단축키"), QStringLiteral("작업값")});
    m_shortcuts->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_shortcuts->setColumnWidth(0, 150);
    m_shortcuts->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_shortcuts->verticalHeader()->setVisible(false);
    for (const Shortcut& s : cfg.shortcuts)
        addShortcutRow(s);
    kl->addWidget(m_shortcuts, 1);
    auto* kb = new QHBoxLayout;
    auto* add = new QPushButton(QStringLiteral("추가"), keys);
    auto* del = new QPushButton(QStringLiteral("삭제"), keys);
    connect(add, &QPushButton::clicked, this, [this]() { addShortcutRow({}); });
    connect(del, &QPushButton::clicked, this, [this]() {
        const int r = m_shortcuts->currentRow();
        if (r >= 0)
            m_shortcuts->removeRow(r);
    });
    kb->addWidget(add);
    kb->addWidget(del);
    kb->addStretch(1);
    kl->addLayout(kb);
    tabs->addTab(keys, QStringLiteral("단축키"));

    // ---------------- 자동 체크
    auto* chk = new QWidget(tabs);
    auto* cl = new QVBoxLayout(chk);
    auto* ch = new QLabel(QStringLiteral("팝업에서 옵션 줄을 우클릭하면 여기에 등록됩니다 (고정 옵션 제외).\n"
                                         "Ctrl+Alt+C 고급 설명으로 복사하면 1~2등급일 때만 자동으로 체크합니다."),
                          chk);
    ch->setStyleSheet(QStringLiteral("color:palette(mid);"));
    cl->addWidget(ch);
    m_checkedList = new QListWidget(chk);
    cl->addWidget(m_checkedList, 1);
    auto* rm = new QPushButton(QStringLiteral("선택 삭제"), chk);
    connect(rm, &QPushButton::clicked, this, [this]() {
        const int r = m_checkedList->currentRow();
        if (r >= 0 && m_checked) {
            m_checked->remove(r);
            m_checked->save();
            refreshChecked();
        }
    });
    cl->addWidget(rm, 0, Qt::AlignLeft);
    tabs->addTab(chk, QStringLiteral("자동 체크"));
    refreshChecked();

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(bb);
}

void SettingsDialog::fillLeagues(QComboBox* c, const GameDb& db, const QString& current)
{
    c->setEditable(true);
    const TradeDb& d = db.loaded(Lang::En) ? db[Lang::En] : db[Lang::Ko];
    for (const auto& l : d.leagues())
        c->addItem(l.id);
    int i = c->findText(current);
    if (i < 0) {
        c->addItem(current);
        i = c->count() - 1;
    }
    c->setCurrentIndex(i);
}

void SettingsDialog::addShortcutRow(const Shortcut& s)
{
    const int r = m_shortcuts->rowCount();
    m_shortcuts->insertRow(r);
    auto* hk = new HotkeyEdit(m_shortcuts);
    hk->setHotkey(s.keycode, s.modifiers);
    m_shortcuts->setCellWidget(r, 0, hk);
    m_shortcuts->setItem(r, 1, new QTableWidgetItem(s.value));
}

void SettingsDialog::refreshChecked()
{
    m_checkedList->clear();
    if (!m_checked)
        return;
    for (const auto& e : m_checked->entries()) {
        const QString t = m_statText ? m_statText(e.id) : e.id;
        m_checkedList->addItem(QStringLiteral("%1   [%2]").arg(t, e.key));
    }
}

void SettingsDialog::setAppUpdateFinished(const QString& buttonText)
{
    if (!m_appUpdateBtn)
        return;
    m_appUpdateBtn->setText(buttonText.isEmpty() ? QStringLiteral("지금 확인") : buttonText);
    m_appUpdateBtn->setEnabled(true);
}

void SettingsDialog::setDataUpdateFinished(int game)
{
    if (game >= 0 && game < 2 && m_dataBtn[game])
        m_dataBtn[game]->setEnabled(true);
}

Config SettingsDialog::result() const
{
    Config c = m_cfg;
    c.game = m_game->currentData().toString();
    c.league = m_league1->currentText().trimmed();
    c.leaguePoe2 = m_league2->currentText().trimmed();
    c.serverType = m_server->currentIndex();
    c.tradeStatus = m_status->currentData().toString();
    c.priceCurrency = m_currency1->currentData().toString();
    c.priceCurrencyPoe2 = m_currency2->currentData().toString();
    c.searchBeforeDay = m_beforeDay->currentData().toInt();
    c.searchListCount = m_listCount->currentData().toInt();
    c.searchPriceMinimum = m_priceMin->value();
    c.searchStatRange = m_statRange->value();
    c.collapseListings = m_collapse->isChecked();
    c.autoPriceCheck = m_autoPrice->isChecked();
    c.autoPriceRefresh = m_autoPriceRefresh->isChecked();
    c.popupPosition = m_popupPos->currentData().toString();
    c.closeOnOutsideClick = m_outsideClose->isChecked();
    c.clipboardWatch = m_clipWatch->isChecked();
    c.searchAutoDelay = c.autoPriceCheck ? qMax(c.searchAutoDelay, 1) : 0;
    c.autoCheckUnique = m_unique->isChecked();
    c.autoSelectPseudo = m_pseudo->isChecked();
    c.autoCheckTotalres = m_totalres->isChecked();
    c.autoSelectCorrupt = m_corrupt->currentData().toString();
    c.autoSelectByType = m_byType->text().trimmed();
    c.serverTimeout = m_timeout->value();
    c.serverRedirect = m_redirect->isChecked();
    c.checkUpdates = m_updates->isChecked();
    c.appUpdate = m_appUpdate->currentData().toString();
    c.dataMaxAgeDays = m_dataAge->value();
    c.shortcuts.clear();
    for (int r = 0; r < m_shortcuts->rowCount(); ++r) {
        auto* hk = qobject_cast<HotkeyEdit*>(m_shortcuts->cellWidget(r, 0));
        Shortcut s;
        s.keycode = hk ? hk->vk() : 0;
        s.modifiers = hk ? hk->modifiers() : 0;
        s.value = m_shortcuts->item(r, 1) ? m_shortcuts->item(r, 1)->text() : QString();
        if (s.keycode > 0 || !s.value.isEmpty())
            c.shortcuts.push_back(s);
    }
    return c;
}

} // namespace pts
