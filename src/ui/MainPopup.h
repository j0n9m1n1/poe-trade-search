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
class QListWidget;
class QPushButton;
class QTabWidget;
class QVBoxLayout;
class QWidget;

namespace pts {

class FilterRow;
class Keywords;
struct GameDb;

// 원본 WinMain 팝업
class MainPopup : public QWidget {
    Q_OBJECT
public:
    explicit MainPopup(const Keywords& kw, QWidget* parent = nullptr);

    void showItem(const ParsedItem& it, const GameDb* gdb, const Config& cfg, Lang server);
    bool hasItem() const { return m_hasItem; }

    ParsedItem collectItem() const; // UI 에서 고친 값 반영
    ItemFilterState collectState() const;
    Lang server() const;
    OptDouble priceMin() const;
    int listCount() const;

    // 일괄 교환: 선택된 받을 화폐 static id (없으면 빈 문자열)
    QString exchangeWant() const;
    QString exchangeHave() const { return m_item.exchangeId; }

    // 시세 표시
    void priceBusy(const QString& msg);
    void priceAppend(const QStringList& lines);
    void priceDone(const QString& summary, const QString& count);
    void priceIdle(const QString& msg);

    void setLeagueTitle(const QString& league);

signals:
    void openTradeRequested();
    void priceCheckRequested();
    void wikiRequested();
    void settingsRequested();
    void rememberToggled(const QString& statKey, const QString& top, bool on);
    void popupHidden();

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void closeEvent(QCloseEvent* e) override;
    void hideEvent(QHideEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void buildUi();
    void clearRows();
    void installActivator(QWidget* w);
    void markStale();

    const Keywords& m_kw;
    const GameDb* m_gdb = nullptr;
    ParsedItem m_item;
    bool m_hasItem = false;
    QString m_idleText;

    QComboBox* m_rarity;
    QComboBox* m_server;
    QPushButton* m_wiki;
    QPushButton* m_settings;
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
    QListWidget* m_prices;
    QLineEdit* m_priceMin;
    QComboBox* m_listCount;
    QLabel* m_priceCount;

    QPushButton* m_search;
    QPushButton* m_close;
};

} // namespace pts
