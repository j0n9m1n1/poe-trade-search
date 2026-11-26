// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QLabel>

namespace pts {

// 체크박스 대신 쓰는 글자 토글 (PoE Overlay 처럼).
// 켜짐 = 밝은 글씨 + 옅은 상자, 꺼짐 = 흐린 글씨. 좌클릭 토글, 우클릭은 rightClicked 신호만.
class ToggleLabel : public QLabel {
    Q_OBJECT
public:
    explicit ToggleLabel(QWidget* parent = nullptr);

    bool isChecked() const { return m_checked; }
    void setChecked(bool on);   // toggled 신호 냄 (값이 바뀔 때만)
    void setCheckable(bool on); // false 면 클릭 무시, 흐리게
    bool isCheckable() const { return m_checkable; }
    void setColor(const QColor& c); // 글씨 색 (켜짐 기준)
    void setMarked(bool on);        // 왼쪽 강조선 (자동 체크 기억 표시 등)
    void setItalic(bool on);
    // 줄바꿈 라벨도 최소 높이는 한 줄 (실제 높이는 heightForWidth 로) -> 목록 아래 빈 공간 안 생김
    QSize minimumSizeHint() const override;

signals:
    void toggled(bool on);
    void rightClicked();

protected:
    void mousePressEvent(QMouseEvent* e) override;

private:
    void updateStyle();

    bool m_checked = false;
    bool m_checkable = true;
    bool m_marked = false;
    bool m_italic = false;
    QColor m_color;
};

} // namespace pts
