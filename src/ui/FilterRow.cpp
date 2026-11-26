// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/FilterRow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QRegularExpressionValidator>
#include <cmath>

namespace pts {

namespace {
QString colorHex(const QString& c)
{
    if (c == QStringLiteral("implicit"))
        return QStringLiteral("#8B0000");
    if (c == QStringLiteral("crafted") || c == QStringLiteral("enchant"))
        return QStringLiteral("#1f3fbf");
    if (c == QStringLiteral("scourge"))
        return QStringLiteral("#d2691e");
    if (c == QStringLiteral("rune"))
        return QStringLiteral("#6a0dad");
    if (c == QStringLiteral("desecrated"))
        return QStringLiteral("#4b6b2f");
    if (c == QStringLiteral("ambiguous"))
        return QStringLiteral("#e75480");
    return {};
}

OptDouble toOpt(const QString& s)
{
    bool ok = false;
    const double v = s.trimmed().toDouble(&ok);
    return ok ? OptDouble(v) : std::nullopt;
}

QString fromOpt(const OptDouble& v)
{
    return v ? QString::number(*v) : QString();
}
} // namespace

FilterRow::FilterRow(QWidget* parent) : QWidget(parent)
{
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 1, 0, 1);
    lay->setSpacing(4);

    m_type = new QComboBox(this);
    m_type->setFixedWidth(84);
    m_type->setToolTip(QStringLiteral("검색할 옵션 종류"));
    m_text = new QLabel(this);
    m_text->setMinimumWidth(200);
    m_text->setWordWrap(true); // 긴 옵션은 줄바꿈
    m_text->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::MinimumExpanding);
    m_text->setTextInteractionFlags(Qt::NoTextInteraction);
    m_roll = new QLabel(this);
    m_roll->setFixedWidth(36);
    m_roll->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_roll->setToolTip(QStringLiteral("롤 % (Ctrl+Alt+C 고급 설명의 범위 기준, 좋은 방향 100%)"));
    m_res = new QCheckBox(this);
    m_res->setToolTip(QStringLiteral("저항 합산 검색"));
    m_min = new QLineEdit(this);
    m_max = new QLineEdit(this);
    auto* v = new QRegularExpressionValidator(QRegularExpression(QStringLiteral("^-?\\d*(\\.\\d*)?$")), this);
    for (QLineEdit* e : {m_min, m_max}) {
        e->setFixedWidth(42);
        e->setValidator(v);
        e->setAlignment(Qt::AlignRight);
    }
    m_use = new QCheckBox(this);
    m_use->setToolTip(QStringLiteral("이 옵션으로 검색\n우클릭: 이 종류 아이템에서 항상 자동 체크"));
    m_use->installEventFilter(this);

    lay->addWidget(m_type);
    lay->addWidget(m_text, 1);
    lay->addWidget(m_roll);
    lay->addWidget(m_res);
    lay->addWidget(m_min);
    lay->addWidget(m_max);
    lay->addWidget(m_use);

    connect(m_type, qOverload<int>(&QComboBox::currentIndexChanged), this, &FilterRow::changed);
    connect(m_use, &QCheckBox::toggled, this, &FilterRow::changed);
    connect(m_res, &QCheckBox::toggled, this, &FilterRow::changed);
    connect(m_min, &QLineEdit::textEdited, this, &FilterRow::changed);
    connect(m_max, &QLineEdit::textEdited, this, &FilterRow::changed);
}

void FilterRow::setMod(const ParsedMod& m)
{
    QSignalBlocker b1(m_type), b2(m_use), b3(m_res);
    m_type->clear();
    for (const auto& c : m.cands)
        m_type->addItem(c.label, c.id);
    if (m.unmatched)
        m_type->addItem(QStringLiteral("검색 불가"));
    m_type->setCurrentIndex(m.unmatched ? 0 : m.selected);
    m_type->setEnabled(m.cands.size() > 1);
    m_unmatched = m.unmatched;

    QString shown = m.text;
    if (m.unmatched)
        shown = QStringLiteral("%1  (거래소 옵션을 찾지 못했습니다)").arg(m.text);
    m_text->setText(shown);
    m_text->setToolTip(m.text == m.raw ? m.raw : m.text + QStringLiteral("\n\n") + m.raw);
    m_color = m.color;
    m_optionStat = m.optionStat;
    m_remembered = m.autoChecked;

    if (m.rollPct) {
        const int p = static_cast<int>(std::lround(*m.rollPct));
        m_roll->setText(QStringLiteral("%1%").arg(p));
        const QString col = p >= 90 ? QStringLiteral("#2e9e5b") : (p < 40 ? QStringLiteral("#9a9a9a") : QString());
        m_roll->setStyleSheet(col.isEmpty() ? QString() : QStringLiteral("color:%1;").arg(col));
        if (m.rollLo && m.rollHi)
            m_roll->setToolTip(QStringLiteral("롤 %1% (범위 %2~%3)").arg(p).arg(*m.rollLo).arg(*m.rollHi));
    } else {
        m_roll->clear();
    }
    m_res->setVisible(m.resistance);
    m_res->setChecked(m.totalRes);
    m_min->setText(fromOpt(m.min));
    m_max->setText(fromOpt(m.max));
    m_min->setEnabled(!m.optionStat && !m.unmatched);
    m_max->setEnabled(!m.optionStat && !m.unmatched);
    m_use->setChecked(m.checked && !m.lockedOff);
    m_use->setEnabled(!m.lockedOff);
    updateStyle();
}

void FilterRow::apply(ParsedMod& m) const
{
    m.selected = qMax(0, m_type->currentIndex());
    m.checked = m_use->isChecked();
    m.totalRes = m.resistance && m_res->isChecked();
    if (!m.optionStat) {
        m.min = toOpt(m_min->text());
        m.max = toOpt(m_max->text());
    }
    m.autoChecked = m_remembered;
}

void FilterRow::setChecked(bool on)
{
    if (m_use->isEnabled())
        m_use->setChecked(on);
}

bool FilterRow::isCheckable() const
{
    return m_use->isEnabled();
}

void FilterRow::updateStyle()
{
    const QString c = colorHex(m_color);
    if (m_unmatched)
        m_text->setStyleSheet(QStringLiteral("QLabel{color:palette(mid);font-style:italic;border-bottom:1px dotted palette(mid);}"));
    else
        m_text->setStyleSheet(c.isEmpty() ? QStringLiteral("QLabel{border-bottom:1px solid palette(mid);}")
                                          : QStringLiteral("QLabel{color:%1;border-bottom:1px solid %1;}").arg(c));
    QFont f = m_use->font();
    f.setBold(m_remembered);
    m_use->setFont(f);
    m_use->setStyleSheet(m_remembered ? QStringLiteral("QCheckBox::indicator{border:2px solid #1f3fbf;}") : QString());
}

bool FilterRow::eventFilter(QObject* o, QEvent* e)
{
    if (o == m_use && e->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(e);
        if (me->button() == Qt::RightButton && m_use->isEnabled()) {
            m_remembered = !m_remembered;
            m_use->setChecked(m_remembered);
            updateStyle();
            emit rememberToggled(m_remembered);
            return true;
        }
    }
    return QWidget::eventFilter(o, e);
}

} // namespace pts
