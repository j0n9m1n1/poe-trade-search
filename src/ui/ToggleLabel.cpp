// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/ToggleLabel.h"

#include <QMouseEvent>

namespace pts {

namespace {
QColor mix(const QColor& a, const QColor& b, double t)
{
    return QColor::fromRgbF(a.redF() * (1 - t) + b.redF() * t, a.greenF() * (1 - t) + b.greenF() * t,
                            a.blueF() * (1 - t) + b.blueF() * t);
}
} // namespace

ToggleLabel::ToggleLabel(QWidget* parent) : QLabel(parent)
{
    setCursor(Qt::PointingHandCursor);
    setTextInteractionFlags(Qt::NoTextInteraction);
    updateStyle();
}

void ToggleLabel::setChecked(bool on)
{
    if (m_checked == on)
        return;
    m_checked = on;
    updateStyle();
    emit toggled(on);
}

void ToggleLabel::setCheckable(bool on)
{
    m_checkable = on;
    setCursor(on ? Qt::PointingHandCursor : Qt::ArrowCursor);
    updateStyle();
}

void ToggleLabel::setColor(const QColor& c)
{
    m_color = c;
    updateStyle();
}

void ToggleLabel::setMarked(bool on)
{
    m_marked = on;
    updateStyle();
}

void ToggleLabel::setItalic(bool on)
{
    m_italic = on;
    updateStyle();
}

QSize ToggleLabel::minimumSizeHint() const
{
    QSize s = QLabel::minimumSizeHint();
    if (wordWrap())
        s.setHeight(fontMetrics().height() + 6);
    return s;
}

void ToggleLabel::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::RightButton) {
        if (m_checkable)
            emit rightClicked();
        e->accept();
        return;
    }
    if (e->button() == Qt::LeftButton && m_checkable) {
        setChecked(!m_checked);
        e->accept();
        return;
    }
    QLabel::mousePressEvent(e);
}

void ToggleLabel::updateStyle()
{
    const QColor bg = palette().color(QPalette::Window);
    const QColor base = m_color.isValid() ? m_color : palette().color(QPalette::WindowText);
    const bool on = m_checked && m_checkable;
    // 꺼진 쪽은 배경 쪽으로 45% 섞어서 흐리게 (색은 남김)
    const QColor fg = on ? base : mix(base, bg, m_checkable ? 0.45 : 0.6);
    const QString box = on ? QStringLiteral("background:rgba(255,255,255,24); border:1px solid rgba(255,255,255,60);")
                           : QStringLiteral("background:transparent; border:1px solid transparent;");
    const QString mark = m_marked ? QStringLiteral("border-left:3px solid #c8a04a;") : QString();
    setStyleSheet(QStringLiteral("QLabel{color:%1; %2 %3 border-radius:3px; padding:1px 5px; %4}")
                      .arg(fg.name(), box, mark, m_italic ? QStringLiteral("font-style:italic;") : QString()));
}

} // namespace pts
