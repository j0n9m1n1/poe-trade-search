// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/SettingsDialog.h"

#include "core/CheckedStore.h"
#include "core/TradeDb.h"
#include "ui/HotkeyEdit.h"

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
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace pts {

SettingsDialog::SettingsDialog(const Config& cfg, const GameDb* dbs, CheckedStore* checked, StatTextFn statText,
                               const QString& versionInfo, QWidget* parent)
    : QDialog(parent), m_cfg(cfg), m_checked(checked), m_statText(std::move(statText))
{
    setWindowTitle(QStringLiteral("설정"));
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
    resize(560, 520);

    auto* root = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);
    root->addWidget(tabs, 1);

    // ---------------- 기본
    auto* basic = new QWidget(tabs);
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
    m_status->addItem(QStringLiteral("즉시 구매 + 직접 거래"), QStringLiteral("available"));
    m_status->addItem(QStringLiteral("즉시 구매만"), QStringLiteral("securable"));
    m_status->addItem(QStringLiteral("직접 거래 (리그 접속 중)"), QStringLiteral("onlineleague"));
    m_status->addItem(QStringLiteral("직접 거래 (접속 중)"), QStringLiteral("online"));
    m_status->addItem(QStringLiteral("모두"), QStringLiteral("any"));
    m_status->setCurrentIndex(qMax(0, m_status->findData(cfg.tradeStatus)));
    f->addRow(QStringLiteral("판매자 상태:"), m_status);

    m_beforeDay = new QComboBox(basic);
    for (auto p : {qMakePair(0, QStringLiteral("모두")), qMakePair(1, QStringLiteral("1일 이내")),
                   qMakePair(3, QStringLiteral("3일 이내")), qMakePair(7, QStringLiteral("1주 이내")),
                   qMakePair(14, QStringLiteral("2주 이내"))})
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

    m_autoPrice = new QCheckBox(QStringLiteral("창이 뜨면 바로 시세 조회 (레이트 리밋 주의)"), basic);
    m_autoPrice->setChecked(cfg.autoPriceCheck);
    f->addRow(m_autoPrice);
    m_unique = new QCheckBox(QStringLiteral("고유 아이템은 옵션 전부 체크"), basic);
    m_unique->setChecked(cfg.autoCheckUnique);
    f->addRow(m_unique);
    m_pseudo = new QCheckBox(QStringLiteral("가능하면 유사(pseudo) 옵션으로 검색"), basic);
    m_pseudo->setChecked(cfg.autoSelectPseudo);
    f->addRow(m_pseudo);
    m_totalres = new QCheckBox(QStringLiteral("저항은 합산으로 검색"), basic);
    m_totalres->setChecked(cfg.autoCheckTotalres);
    f->addRow(m_totalres);

    m_corrupt = new QLineEdit(cfg.autoSelectCorrupt, basic);
    m_corrupt->setPlaceholderText(QStringLiteral("비움 / yes / no"));
    f->addRow(QStringLiteral("장비 타락 기본값:"), m_corrupt);
    m_byType = new QLineEdit(cfg.autoSelectByType, basic);
    m_byType->setPlaceholderText(QStringLiteral("예: jewel,accessory.ring"));
    f->addRow(QStringLiteral("유형으로 검색할 카테고리:"), m_byType);

    m_timeout = new QSpinBox(basic);
    m_timeout->setRange(1, 60);
    m_timeout->setValue(cfg.serverTimeout);
    m_timeout->setSuffix(QStringLiteral("초"));
    f->addRow(QStringLiteral("서버 대기 시간:"), m_timeout);
    m_redirect = new QCheckBox(QStringLiteral("검색을 브라우저가 직접 하게 함 (server_redirect)"), basic);
    m_redirect->setChecked(cfg.serverRedirect);
    f->addRow(m_redirect);

    m_updates = new QCheckBox(QStringLiteral("시작 시 거래소 데이터 오래됐으면 자동 갱신"), basic);
    m_updates->setChecked(cfg.checkUpdates);
    f->addRow(m_updates);
    m_dataAge = new QSpinBox(basic);
    m_dataAge->setRange(1, 90);
    m_dataAge->setValue(cfg.dataMaxAgeDays);
    m_dataAge->setSuffix(QStringLiteral("일"));
    f->addRow(QStringLiteral("데이터 갱신 주기:"), m_dataAge);

    auto* upd = new QHBoxLayout;
    auto* u1 = new QPushButton(QStringLiteral("PoE1 데이터 갱신"), basic);
    auto* u2 = new QPushButton(QStringLiteral("PoE2 데이터 갱신"), basic);
    connect(u1, &QPushButton::clicked, this, [this, u1]() {
        u1->setEnabled(false);
        emit updateDataRequested(0);
    });
    connect(u2, &QPushButton::clicked, this, [this, u2]() {
        u2->setEnabled(false);
        emit updateDataRequested(1);
    });
    upd->addWidget(u1);
    upd->addWidget(u2);
    f->addRow(upd);
    auto* ver = new QLabel(versionInfo, basic);
    ver->setStyleSheet(QStringLiteral("color:palette(mid);"));
    f->addRow(ver);
    tabs->addTab(basic, QStringLiteral("기본"));

    // ---------------- 단축키
    auto* keys = new QWidget(tabs);
    auto* kl = new QVBoxLayout(keys);
    auto* help = new QLabel(QStringLiteral("PoE 창이 활성일 때만 동작. 값 예시:\n"
                                           "  {Run} 아이템 검색 (지정하면 Ctrl+C 클립보드 감시는 꺼짐)\n"
                                           "  {Wiki} 위키 열기   {Pause} 일시정지   {Restart} 재시작\n"
                                           "  {Link}https://poe.ninja/{Link} 링크 열기\n"
                                           "채팅 매크로/이미지/창고 그리드는 1차 버전에서 미지원."),
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
    auto* ch = new QLabel(QStringLiteral("팝업에서 옵션 체크박스를 우클릭하면 여기에 등록됨 (고정 옵션 제외).\n"
                                         "Ctrl+Alt+C 고급 설명으로 복사하면 1~2등급일 때만 자동 체크함."),
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

Config SettingsDialog::result() const
{
    Config c = m_cfg;
    c.game = m_game->currentData().toString();
    c.league = m_league1->currentText().trimmed();
    c.leaguePoe2 = m_league2->currentText().trimmed();
    c.serverType = m_server->currentIndex();
    c.tradeStatus = m_status->currentData().toString();
    c.searchBeforeDay = m_beforeDay->currentData().toInt();
    c.searchListCount = m_listCount->currentData().toInt();
    c.searchPriceMinimum = m_priceMin->value();
    c.autoPriceCheck = m_autoPrice->isChecked();
    c.searchAutoDelay = c.autoPriceCheck ? qMax(c.searchAutoDelay, 1) : 0;
    c.autoCheckUnique = m_unique->isChecked();
    c.autoSelectPseudo = m_pseudo->isChecked();
    c.autoCheckTotalres = m_totalres->isChecked();
    c.autoSelectCorrupt = m_corrupt->text().trimmed();
    c.autoSelectByType = m_byType->text().trimmed();
    c.serverTimeout = m_timeout->value();
    c.serverRedirect = m_redirect->isChecked();
    c.checkUpdates = m_updates->isChecked();
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
