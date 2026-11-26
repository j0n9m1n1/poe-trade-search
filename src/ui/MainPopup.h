// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Config.h"
#include "core/ItemParser.h"
#include "core/SearchDefaults.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;
class QTableWidget;
class QVBoxLayout;

namespace pts {

class FilterRow;
class Keywords;
struct GameDb;

// 원본 WinMain 팝업
class MainPopup : public QWidget {
    Q_OBJECT
public:
    explicit MainPopup(const Keywords& kw, QWidget* parent = nullptr);

    void showItem(const ParsedItem& parsed, const GameDb* gdb, const Config& cfg, Lang server);
    bool hasItem() const { return m_hasItem; }
    const ParsedItem& parsedItem() const { return m_parsed; }

    ParsedItem collectItem() const; // UI 에서 고친 값 반영
    ItemFilterState collectState() const;
    Lang server() const;
    OptDouble priceMin() const;
    int listCount() const;
    bool collapse() const;
    QString tradeStatus() const;
    QString priceCurrency() const; // 가격 화폐 필터 ("" = 등가물)

    // 일괄 교환: 선택된 받을 화폐 static id (없으면 빈 문자열)
    QString exchangeWant() const;
    QString exchangeHave() const { return m_parsed.exchangeId; }

    void setIcon(const QString& imagePath);

    // 시세 표시
    void priceBusy(const QString& msg);   // 새 조회 시작 (표 비움)
    void priceStatus(const QString& msg); // 진행 문구만 바꿈
    void priceColumns(const QStringList& headers, const QVector<int>& hidden = {});
    void priceAddRow(const QStringList& cells, bool dim, int online, const QString& tooltip = QString());
    void priceDone(const QString& summary, const QString& count, const QString& tooltip = QString());
    void priceIdle(const QString& msg);

    void setLeagueTitle(const QString& league);

    bool hasPrevious() const { return m_prev.valid; }
    void swapWithPrevious(); // 직전 아이템과 바꿔 보기

signals:
    void openTradeRequested();
    void priceCheckRequested();
    void settingsRequested();
    void rememberToggled(const QString& statKey, const QString& top, bool on);
    void popupHidden();
    void itemSwapped(); // 직전 아이템으로 바뀜 (진행 중 시세 조회 취소용)

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void closeEvent(QCloseEvent* e) override;
    void hideEvent(QHideEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    struct PriceRow {
        QStringList cells;
        bool dim = false;
        int online = -1;
        QString tip;
    };
    // 직전 아이템 보관용 (파서 결과 + 시세 결과 그대로)
    struct Snapshot {
        bool valid = false;
        ParsedItem parsed;
        const GameDb* gdb = nullptr;
        Lang server = Lang::En;
        QString icon;
        QStringList headers;
        QVector<int> hidden;
        QVector<PriceRow> rows;
        QString summary, count, tip;
        bool priced = false;
    };
    Snapshot snapshot() const;
    void restore(const Snapshot& s);
    void showItemImpl(const ParsedItem& parsed, const GameDb* gdb, Lang server);
    void updatePrevBar();

    void buildUi();
    void clearRows();
    void installActivator(QWidget* w);
    void markStale();
    void applyPresetAndRender();
    void renderRows();
    void renderMisc(const ItemFilterState& st);
    int rangePct() const;

    const Keywords& m_kw;
    const GameDb* m_gdb = nullptr;
    ParsedItem m_parsed; // 파서 결과 그대로
    ParsedItem m_item;   // 프리셋/범위 적용된 것 (행 렌더 기준)
    bool m_hasItem = false;
    QString m_idleText;
    int m_cfgRange = 10;
    Config m_cfg;

    // 현재 시세 결과 (스냅샷용 사본)
    QString m_curIcon;
    QStringList m_curHeaders;
    QVector<int> m_curHidden;
    QVector<PriceRow> m_curRows;
    QString m_curSummary, m_curCount, m_curTip;
    bool m_curPriced = false;
    Snapshot m_prev;

    QWidget* m_prevBar;
    QLabel* m_prevLabel;
    QPushButton* m_prevBtn;

    QComboBox* m_rarity;
    QComboBox* m_preset;
    QComboBox* m_range;
    QComboBox* m_server;
    QPushButton* m_settings;
    QLabel* m_icon;
    QLabel* m_name;
    QLabel* m_sub;
    QLabel* m_info;
    QCheckBox* m_all;

    QTabWidget* m_tabs;
    QWidget* m_rowsBox;
    QVBoxLayout* m_rowsLayout;
    QVector<FilterRow*> m_rows;
    QLabel* m_detail;

    QWidget* m_exchangeBox;
    QComboBox* m_want1;
    QComboBox* m_want2;

    QWidget* m_miscBox;
    QCheckBox* m_lv;
    QLineEdit* m_lvMin;
    QLineEdit* m_lvMax;
    QCheckBox* m_q;
    QLineEdit* m_qMin;
    QLineEdit* m_qMax;
    QCheckBox* m_sock;
    QLineEdit* m_sMin;
    QLineEdit* m_sMax;
    QLabel* m_linkLabel;
    QLineEdit* m_lMin;
    QLineEdit* m_lMax;
    QCheckBox* m_special1;
    QCheckBox* m_special2;
    QComboBox* m_corrupt;
    QComboBox* m_infl1;
    QComboBox* m_infl2;
    QComboBox* m_alt;
    QCheckBox* m_byCategory;

    QPushButton* m_price;
    QTableWidget* m_prices;
    QLineEdit* m_priceMin;
    QComboBox* m_listCount;
    QComboBox* m_status;
    QComboBox* m_currency;
    QCheckBox* m_collapse;
    QLabel* m_priceCount;

    QPushButton* m_search;
    QPushButton* m_close;
};

} // namespace pts
