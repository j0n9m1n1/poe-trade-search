// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Config.h"

#include <QDialog>
#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QTableWidget;

namespace pts {

class CheckedStore;
struct GameDb;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    using StatTextFn = std::function<QString(const QString& statKey)>;

    SettingsDialog(const Config& cfg, const GameDb* dbs, CheckedStore* checked, StatTextFn statText,
                   const QString& versionInfo, QWidget* parent = nullptr);

    Config result() const;

signals:
    void updateDataRequested(int game); // 0 = poe1, 1 = poe2

private:
    void addShortcutRow(const Shortcut& s);
    void fillLeagues(QComboBox* c, const GameDb& db, const QString& current);
    void refreshChecked();

    Config m_cfg;
    CheckedStore* m_checked;
    StatTextFn m_statText;

    QComboBox* m_game;
    QComboBox* m_league1;
    QComboBox* m_league2;
    QComboBox* m_server;
    QComboBox* m_status;
    QComboBox* m_currency1; // 가격 화폐 PoE1
    QComboBox* m_currency2; // 가격 화폐 PoE2
    QComboBox* m_beforeDay;
    QComboBox* m_listCount;
    QSpinBox* m_priceMin;
    QSpinBox* m_statRange;
    QCheckBox* m_collapse;
    QSpinBox* m_timeout;
    QSpinBox* m_dataAge;
    QCheckBox* m_autoPrice;
    QCheckBox* m_nearCursor;
    QCheckBox* m_outsideClose;
    QCheckBox* m_unique;
    QCheckBox* m_pseudo;
    QCheckBox* m_totalres;
    QCheckBox* m_updates;
    QComboBox* m_appUpdate;
    QCheckBox* m_redirect;
    QLineEdit* m_corrupt;
    QLineEdit* m_byType;
    QTableWidget* m_shortcuts;
    QListWidget* m_checkedList;
};

} // namespace pts
