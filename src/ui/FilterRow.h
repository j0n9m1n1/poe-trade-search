// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/ItemParser.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;

namespace pts {

class ToggleLabel;

// 옵션 한 줄: [타입][등급 P1/S1][옵션 문구(누르면 검색에 넣고 뺌)][롤%][합산][최소][최대]
class FilterRow : public QWidget {
    Q_OBJECT
public:
    explicit FilterRow(QWidget* parent = nullptr);

    void setMod(const ParsedMod& m);
    void apply(ParsedMod& m) const; // UI 값 -> ParsedMod
    void setChecked(bool on);
    bool isCheckable() const;
    // 전체 체크 대상 (원본: 색 없는 줄만. 분열은 일반 옵션 취급)
    bool isPlain() const { return m_color.isEmpty() || m_color == QStringLiteral("fractured"); }

    QLineEdit* minEdit() const { return m_min; }
    QLineEdit* maxEdit() const { return m_max; }

    // 게임 글씨 색 (옵션 종류별)
    static QColor modColor(const QString& colorKey);

signals:
    void changed();
    void rememberToggled(bool checked); // 우클릭 -> 자동 체크 목록

private:
    QComboBox* m_type;
    QLabel* m_tier;
    ToggleLabel* m_text;
    QLabel* m_roll;
    QCheckBox* m_res;
    QLineEdit* m_min;
    QLineEdit* m_max;
    QString m_color;
    bool m_optionStat = false;
    bool m_remembered = false;
    bool m_unmatched = false;
};

// 아이템 속성 한 줄 (레어도 / 아이템 레벨 / 퀄리티 / 방어도 ...): PoE Overlay 처럼 옵션 줄과 같은 열에
// [빈칸(종류+등급 폭)][이름: 값 (누르면 켜고 끔)][최소][최대]  또는  [이름: 값][콤보]
class PropRow : public QWidget {
    Q_OBJECT
public:
    explicit PropRow(QWidget* parent = nullptr);

    void setText(const QString& t);
    void setChecked(bool on); // changed 안 냄
    bool isChecked() const;
    void setCheckable(bool on);
    void setColor(const QColor& c);
    void setExtra(QWidget* w); // 최소/최대 대신 (레어도 콤보 등)
    void setValues(const OptDouble& mn, const OptDouble& mx);
    OptDouble minValue() const;
    OptDouble maxValue() const;
    QLineEdit* minEdit() const { return m_min; }
    QLineEdit* maxEdit() const { return m_max; }

signals:
    void changed();

private:
    ToggleLabel* m_text;
    QLineEdit* m_min;
    QLineEdit* m_max;
};

} // namespace pts
