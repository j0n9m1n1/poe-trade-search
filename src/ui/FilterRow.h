// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/ItemParser.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;

namespace pts {

// 옵션 한 줄: [타입][옵션 텍스트][합산][최소][최대][사용]
class FilterRow : public QWidget {
    Q_OBJECT
public:
    explicit FilterRow(QWidget* parent = nullptr);

    void setMod(const ParsedMod& m);
    void apply(ParsedMod& m) const; // UI 값 -> ParsedMod
    void setChecked(bool on);
    bool isCheckable() const;
    bool isPlain() const { return m_color.isEmpty(); } // 전체 체크 대상 (원본: 색 없는 줄만)

    QLineEdit* minEdit() const { return m_min; }
    QLineEdit* maxEdit() const { return m_max; }

signals:
    void changed();
    void rememberToggled(bool checked); // 우클릭 -> 자동 체크 목록

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void updateStyle();

    QComboBox* m_type;
    QLabel* m_text;
    QCheckBox* m_res;
    QLineEdit* m_min;
    QLineEdit* m_max;
    QCheckBox* m_use;
    QString m_color;
    bool m_optionStat = false;
    bool m_remembered = false;
};

} // namespace pts
