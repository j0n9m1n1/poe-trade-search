// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Config.h"
#include "core/ItemParser.h"
#include "core/SearchDefaults.h"

#include <QHash>
#include <QPoint>
#include <QTimer>
#include <QWidget>

class QCheckBox;
class QFrame;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QTableWidgetItem;
class QTableWidget;
class QVBoxLayout;

namespace pts {

class FilterRow;
class PropRow;
class ToggleLabel;
class Keywords;
struct GameDb;

// 원본 WinMain 팝업
class MainPopup : public QWidget {
    Q_OBJECT
public:
    explicit MainPopup(const Keywords& kw, QWidget* parent = nullptr);

    void showItem(const ParsedItem& parsed, const GameDb* gdb, const Config& cfg, Lang server);
    // 설정 변경(서버/판매방식/가격화폐)을 열려 있는 팝업에 반영. 바뀐 값이 있으면 true
    bool syncConfig(const Config& cfg, Lang server);
    void fitToAvailableHeight(int height);
private:
    void refitHeight();      // 저장한 화면 높이 기준으로 창 높이를 내용에 다시 맞춤
    void scheduleRefit();    // 대기 중인 이벤트(새 옵션 줄 표시 등) 처리 뒤 한 번 더 맞춤
    int m_availHeight = 0;
    bool m_refitPending = false;
public:
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
    QString indexed() const;       // 등록 기간 ("" = 언제든)

    // 일괄 교환: 선택된 받을 화폐 static id (없으면 빈 문자열)

    void setIcon(const QString& imagePath);

    // 시세 표시
    void priceBusy(const QString& msg);   // 새 조회 시작 (표 비움)
    void priceStatus(const QString& msg); // 진행 문구만 바꿈
    void priceColumns(const QStringList& headers, const QVector<int>& hidden = {});
    void priceAddRow(const QStringList& cells, bool dim, int online, const QString& tooltip = QString(),
                     double priceAmount = 0, const QString& priceCurrency = QString());
    void priceDone(const QString& summary, const QString& count, const QString& tooltip = QString(),
                   double noteAmount = 0, const QString& noteCurrency = QString());
    void pricePageStatus(bool hasMore, int loaded, int total, const QString& status = {}, int exchangeCount = -1);
    void priceIdle(const QString& msg);
    void showCopyFeedback(const QString& note);
    void clearPrices() { priceBusy(m_idleText); }
    void setSearchBusy(bool busy);

    void setLeagueTitle(const QString& league);
    QString leagueTitle() const { return m_leagueText; }
    // 화폐 이름(카오스/디바인/전체 이름) 아이콘. 가격 칸과 추정가에 이름 대신 아이콘
    void setUnitIcon(const QString& unitName, const QString& path);
    // 영향/엘드리치/분열 배지 아이콘: (게임 이름판 문양 이름, 못 받았을 때 쓸 거래소 정적 데이터 id)
    QVector<QPair<QString, QString>> badgeIconSources() const;
    void setBadgeSymbol(const QString& symbol, const QString& path);
    void setBadgeIcon(const QString& staticId, const QString& path);
    QVector<QPair<QString, QString>> influenceIconSources() const; // 영향 콤보 아이콘 (문양, 오브 정적 id)

signals:
    void openTradeRequested();
    void priceCheckRequested();
    void moreListingsRequested();
    void priceFiltersChanged();
    void settingsRequested();
    void leagueRequested(Game game, const QString& league);
    void rememberToggled(const QString& statKey, const QString& top, bool on);
    void copyPriceNoteRequested(const QString& note);
    void popupHidden();

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void paintEvent(QPaintEvent* e) override;
    void closeEvent(QCloseEvent* e) override;
    void hideEvent(QHideEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void showItemImpl(const ParsedItem& parsed, const GameDb* gdb, Lang server);

    void buildUi();
    void clearRows();
    void installActivator(QWidget* w);
    void markStale();
    void applyPresetAndRender();
    void applyRangeAndRender();
    void renderRows();
    void renderMisc(const ItemFilterState& st);
    void applyRarityColor(const ParsedItem& it);
    void setPriceText(const QString& plain, const QString& color = QString());
    QString withUnitIcons(const QString& plain) const;
    void applyUnitIcon(QTableWidgetItem* item) const;
    void copyRowPriceNote(int row);
    void showPriceContextMenu(const QPoint& pos);
    void fitPriceColumns();
    void refreshCurrencyCombo(); // 화폐 콤보를 아이콘으로 (폭 줄이기)
    void fitRowsArea(); // 속성 줄 + 옵션 줄 높이에 맞춰 영역 크기
    void fillLeagueCombo();
    int rangePct() const;

    const Keywords& m_kw;
    const GameDb* m_gdb = nullptr;
    ParsedItem m_parsed; // 파서 결과 그대로
    ParsedItem m_item;   // 프리셋/범위 적용된 것 (행 렌더 기준)
    bool m_hasItem = false;
    QString m_idleText;
    int m_cfgRange = 10;
    Config m_cfg;


    QComboBox* m_rarity;
    QComboBox* m_preset;
    QComboBox* m_range;
    QComboBox* m_league = nullptr; // 검색할 리그
    QString m_leagueText;
    bool m_dragging = false;
    QPoint m_dragOffset;
    QComboBox* m_server;
    QPushButton* m_settings;
    QLabel* m_icon;
    QFrame* m_header;   // 이름/베이스 띠
    ToggleLabel* m_useName; // 고유 이름 (켜짐 = 검색에 넣음)
    ToggleLabel* m_useType; // 베이스 (끄면 유형으로 검색)
    QLabel* m_inflL;        // 영향 표시 (왼쪽/오른쪽)
    QLabel* m_inflR;
    struct Badge {
        QString symbol; // 게임 이름판 문양 (fractured / shaper / searing ...)
        QString iconId; // 문양을 못 받았을 때 쓸 거래소 정적 데이터 id
        QString name;
        QString color; // 아이콘 못 받았을 때 글자 배지 색
    };
    QVector<Badge> m_badges;
    QHash<QString, QString> m_badgeIcons; // 정적 id -> 아이콘 파일
    void renderInfluenceIcons();
    void renderBadges();
    QLabel* m_name;     // 희귀 이름 표시용
    QLabel* m_info;
    ToggleLabel* m_all;
    QHBoxLayout* m_filterHeader;

    QScrollArea* m_scroll;
    struct PropWidgets {
        QString key;
        QString label;
        PropRow* row = nullptr;
    };
    QVector<PropWidgets> m_props; // 아이템 속성 줄 (방어도/DPS/지도 속성 ...)
    QWidget* m_propRows;          // 옵션 목록 위 속성 줄 묶음
    QWidget* m_propSep;
    PropRow* m_rarityRow;
    PropRow* m_lvRow;
    PropRow* m_qRow;
    PropRow* m_sockRow;
    PropRow* m_linkRow;
    PropRow* m_mirrorRow; // 복제된 아이템
    PropRow* m_notFracRow; // 분열 속성 없음
    PropRow* m_eldritchRow;
    PropRow* m_emptyPrefixRow;
    PropRow* m_emptySuffixRow;
    QWidget* m_rowsBox;
    QVBoxLayout* m_rowsLayout;
    QVector<FilterRow*> m_rows;
    QLabel* m_detail;


    QWidget* m_miscBox;
    QLineEdit* m_lvMin;
    QLineEdit* m_lvMax;
    QLineEdit* m_qMin;
    QLineEdit* m_qMax;
    QLineEdit* m_sMin;
    QLineEdit* m_sMax;
    QLineEdit* m_lMin;
    QLineEdit* m_lMax;
    QCheckBox* m_special1;
    QCheckBox* m_special2;
    QComboBox* m_corrupt;
    QCheckBox* m_modifiable; // 수정 가능 (타락·복제 아님)
    QComboBox* m_mutated;
    QComboBox* m_infl1;
    QComboBox* m_infl2;
    QComboBox* m_alt;

    QLabel* m_price; // 추정가 줄 (누르면 시세 조회)
    QPushButton* m_refresh = nullptr; // 다시 검색
    int m_lastPriceCol = -1;
    QString m_lastRowKey; // 같은 판매자·같은 가격 묶기
    int m_lastRowCount = 0;
    QString m_priceText, m_priceColor;
    bool m_priceStale = false;
    double m_estimateAmount = 0;
    QString m_estimateCurrency;
    QTimer m_copyFeedbackTimer;
    QHash<QString, QString> m_unitIcons; // 화폐 이름 -> 아이콘 파일
    QTableWidget* m_prices;
    bool m_priceMoreAvailable = false;
    QLineEdit* m_priceMin;
    QComboBox* m_listCount;
    QComboBox* m_status;
    QComboBox* m_currency;
    QComboBox* m_indexed;
    bool m_indexedAuto = true; // 등록일이 판매 방식을 따라가는 중
    QCheckBox* m_collapse;
    QLabel* m_priceCount;

    QPushButton* m_search;
};

} // namespace pts
