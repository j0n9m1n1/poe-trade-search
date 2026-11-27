// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/FilterRow.h"

#include "ui/FilterValueStep.h"
#include "ui/ToggleLabel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpressionValidator>
#include <QWheelEvent>
#include <cmath>

namespace pts {

namespace {
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

class FilterValueEdit : public QLineEdit {
public:
    explicit FilterValueEdit(QWidget* parent) : QLineEdit(parent)
    {
        setToolTip(QStringLiteral("이 칸을 선택한 뒤 휠을 돌리면 값을 조절합니다. Shift를 누르면 10배로 조절합니다."));
    }

    void setRowValue(const OptDouble& value)
    {
        m_rowValue = value;
        m_angle = 0;
    }

protected:
    void wheelEvent(QWheelEvent* event) override
    {
        const int delta = event->angleDelta().y();
        if (!hasFocus() || !rect().contains(event->position().toPoint()) || delta == 0) {
            event->ignore();
            return;
        }
        event->accept();
        constexpr int anglePerNotch = 120;
        m_angle += delta;
        const int notches = m_angle / anglePerNotch;
        m_angle %= anglePerNotch;
        if (notches == 0)
            return;
        const OptDouble current = toOpt(text());
        const double next = filterValueStep(current, m_rowValue, notches, event->modifiers() & Qt::ShiftModifier);
        if (current && next == *current)
            return;
        QString value = QString::number(next, 'f', 12);
        while (value.endsWith(QLatin1Char('0')))
            value.chop(1);
        value.chop(value.endsWith(QLatin1Char('.')) ? 1 : 0);
        selectAll();
        insert(value);
    }

    void focusOutEvent(QFocusEvent* event) override
    {
        m_angle = 0;
        QLineEdit::focusOutEvent(event);
    }

private:
    OptDouble m_rowValue;
    int m_angle = 0;
};
} // namespace

FilterRow::FilterRow(QWidget* parent) : QWidget(parent)
{
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 1, 0, 1);
    lay->setSpacing(kFilterColumnSpacing);

    m_type = new QComboBox(this);
    m_type->setFixedWidth(84);
    m_type->setToolTip(QStringLiteral("검색할 옵션 종류"));
    m_tier = new QLabel(this);
    m_tier->setFixedWidth(26);
    m_tier->setAlignment(Qt::AlignCenter);
    m_tier->setToolTip(QStringLiteral("P = 접두어, S = 접미어, 숫자 = 등급 (Ctrl+Alt+C 로 복사했을 때)"));
    m_text = new ToggleLabel(this);
    m_text->setMinimumWidth(330); // 최소 폭에서 줄바꿈한 높이가 최소 높이라 너무 좁으면 목록 아래 빈 공간이 생김
    m_text->setWordWrap(true); // 긴 옵션은 줄바꿈
    m_text->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::MinimumExpanding);
    {
        QFont f = m_text->font();
        f.setPointSizeF(f.pointSizeF() + 1);
        m_text->setFont(f);
    }
    m_roll = new QLabel(this);
    m_roll->setFixedWidth(36);
    m_roll->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_roll->setToolTip(QStringLiteral("롤 % (Ctrl+Alt+C 고급 설명의 범위 기준, 좋은 방향 100%)"));
    m_res = new QCheckBox(this);
    m_res->setToolTip(QStringLiteral("저항 합산 검색"));
    m_min = new FilterValueEdit(this);
    m_max = new FilterValueEdit(this);
    auto* v = new QRegularExpressionValidator(QRegularExpression(QStringLiteral("^-?\\d*(\\.\\d*)?$")), this);
    for (QLineEdit* e : {m_min, m_max}) {
        e->setFixedWidth(kFilterValueWidth);
        e->setValidator(v);
        e->setAlignment(Qt::AlignRight);
    }
    lay->addWidget(m_type);
    lay->addWidget(m_tier);
    lay->addWidget(m_text, 1);
    lay->addWidget(m_roll);
    lay->addWidget(m_res);
    lay->addWidget(m_min);
    lay->addWidget(m_max);

    connect(m_type, qOverload<int>(&QComboBox::currentIndexChanged), this, &FilterRow::changed);
    connect(m_text, &ToggleLabel::toggled, this, &FilterRow::changed);
    connect(m_text, &ToggleLabel::rightClicked, this, [this]() {
        // 우클릭: 이 종류 아이템에서 항상 자동 체크 (켜면 체크도 같이)
        m_remembered = !m_remembered;
        m_text->setMarked(m_remembered);
        m_text->setChecked(m_remembered);
        emit rememberToggled(m_remembered);
    });
    connect(m_res, &QCheckBox::toggled, this, &FilterRow::changed);
    connect(m_min, &QLineEdit::textEdited, this, &FilterRow::changed);
    connect(m_max, &QLineEdit::textEdited, this, &FilterRow::changed);
}

void FilterRow::setMod(const ParsedMod& m, bool compactCluster)
{
    QSignalBlocker b1(m_type), b2(m_text), b3(m_res);
    m_type->clear();
    for (const auto& c : m.cands)
        m_type->addItem(c.label, c.id);
    if (m.unmatched)
        m_type->addItem(QStringLiteral("검색 불가"));
    m_type->setCurrentIndex(m.unmatched ? 0 : m.selected);
    m_type->setEnabled(m.cands.size() > 1);
    m_unmatched = m.unmatched;

    // 등급 칸: P1 / S2 (고급 설명) 또는 T1 (접두/접미 모름)
    if (m.tier > 0) {
        const bool pre = m.affix == QStringLiteral("prefix"), suf = m.affix == QStringLiteral("suffix");
        m_tier->setText((pre ? QStringLiteral("P") : (suf ? QStringLiteral("S") : QStringLiteral("T"))) +
                        QString::number(m.tier));
        m_tier->setStyleSheet(QStringLiteral("color:%1; font-weight:bold;")
                                  .arg(pre ? QStringLiteral("#e08a8a") : (suf ? QStringLiteral("#7fc4e0") : QStringLiteral("#bbbbbb"))));
    } else {
        m_tier->clear();
    }
    // 글자는 게임 문구 그대로 (수치 포함). 검색에 쓰는 거래소 문구는 툴팁
    QString shown = m.shown;
    if (shown.isEmpty()) {
        shown = m.text;
        const QString tierPrefix = QStringLiteral("T%1 ").arg(m.tier);
        if (m.tier > 0 && shown.startsWith(tierPrefix))
            shown = shown.mid(tierPrefix.size());
    }
    if (compactCluster && m.modType == QStringLiteral("enchant")) {
        shown.replace(QStringLiteral(" / 추가된 소형 패시브 스킬 효과: "), QStringLiteral(" / "));
        shown.replace(QStringLiteral("추가된 소형 패시브 스킬 효과:"), QStringLiteral("소형 패시브:"));
        shown.replace(QStringLiteral(" / Added Small Passive Skills grant: "), QStringLiteral(" / "));
        shown.replace(QStringLiteral("Added Small Passive Skills grant:"), QStringLiteral("Small passives:"));
    }
    if (m.unmatched)
        shown = QStringLiteral("%1  (거래소 옵션을 찾지 못했습니다)").arg(shown);
    m_text->setText(shown);
    m_text->setToolTip(QStringLiteral("거래소 검색: %1\n원문: %2\n\n누르면 검색에 넣거나 뺍니다 / 우클릭: 항상 자동 체크")
                           .arg(m.text, m.raw));
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
    static_cast<FilterValueEdit*>(m_min)->setRowValue(m.value);
    static_cast<FilterValueEdit*>(m_max)->setRowValue(m.value);
    m_min->setText(fromOpt(m.min));
    m_max->setText(fromOpt(m.max));
    m_min->setEnabled(!m.optionStat && !m.unmatched);
    m_max->setEnabled(!m.optionStat && !m.unmatched);
    m_text->setCheckable(!m.lockedOff && !m.unmatched);
    m_text->setChecked(m.checked && !m.lockedOff);
    m_text->setColor(m.unmatched ? QColor() : modColor(m.color));
    m_text->setItalic(m.unmatched);
    m_text->setMarked(m_remembered);
}

void FilterRow::apply(ParsedMod& m) const
{
    m.selected = qMax(0, m_type->currentIndex());
    m.checked = m_text->isChecked() && m_text->isCheckable();
    m.totalRes = m.resistance && m_res->isChecked();
    if (!m.optionStat) {
        m.min = toOpt(m_min->text());
        m.max = toOpt(m_max->text());
    }
    m.autoChecked = m_remembered;
}

void FilterRow::setChecked(bool on)
{
    if (m_text->isCheckable())
        m_text->setChecked(on);
}

bool FilterRow::isCheckable() const
{
    return m_text->isCheckable();
}

PropRow::PropRow(QWidget* parent) : QWidget(parent)
{
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 1, 0, 1);
    lay->setSpacing(kFilterColumnSpacing);
    m_name = new QLabel(this); // FilterRow 의 종류 콤보 + 등급 열
    m_name->setFixedWidth(84 + 4 + 26);
    m_name->setStyleSheet(QStringLiteral("QLabel{color:#9a9a9a;}"));
    m_name->setCursor(Qt::PointingHandCursor);
    m_name->installEventFilter(this);
    lay->addWidget(m_name);
    m_text = new ToggleLabel(this);
    m_text->setColor(QColor(0xc8, 0xc8, 0xc8)); // 게임 속성 글씨 (회색)
    {
        QFont f = m_text->font();
        f.setPointSizeF(f.pointSizeF() + 1);
        m_text->setFont(f);
    }
    m_text->setToolTip(QStringLiteral("누르면 검색에 넣거나 뺍니다"));
    lay->addWidget(m_text);
    lay->addStretch(1);
    lay->addSpacing(36); // FilterRow 의 롤 % 칸
    m_min = new FilterValueEdit(this);
    m_max = new FilterValueEdit(this);
    auto* v = new QRegularExpressionValidator(QRegularExpression(QStringLiteral("^-?\\d*(\\.\\d*)?$")), this);
    for (QLineEdit* e : {m_min, m_max}) {
        e->setFixedWidth(kFilterValueWidth);
        e->setValidator(v);
        e->setAlignment(Qt::AlignRight);
        lay->addWidget(e);
        connect(e, &QLineEdit::textEdited, this, &PropRow::changed);
    }
    connect(m_text, &ToggleLabel::toggled, this, &PropRow::changed);
}

void PropRow::setLabel(const QString& name, const QString& value)
{
    m_name->setText(name);
    m_text->setText(value);
}

bool PropRow::eventFilter(QObject* o, QEvent* e)
{
    if (o == m_name && e->type() == QEvent::MouseButtonPress &&
        static_cast<QMouseEvent*>(e)->button() == Qt::LeftButton && m_text->isCheckable()) {
        m_text->setChecked(!m_text->isChecked()); // toggled -> changed
        return true;
    }
    return QWidget::eventFilter(o, e);
}

void PropRow::setChecked(bool on)
{
    QSignalBlocker b(m_text);
    m_text->setChecked(on);
}

bool PropRow::isChecked() const
{
    return m_text->isChecked() && m_text->isCheckable();
}

void PropRow::setCheckable(bool on)
{
    m_text->setCheckable(on);
    m_text->setCursor(on ? Qt::PointingHandCursor : Qt::ArrowCursor);
    m_name->setCursor(on ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void PropRow::setColor(const QColor& c)
{
    m_text->setColor(c);
}

void PropRow::setExtra(QWidget* w)
{
    m_min->hide();
    m_max->hide();
    w->setParent(this);
    w->setFixedWidth(42 * 2 + 4 + 40); // 최소+최대 칸보다 조금 넓게 (롤 칸 쪽으로)
    static_cast<QHBoxLayout*>(layout())->insertWidget(layout()->count() - 2, w); // 최소/최대 자리
}

void PropRow::setEditsVisible(bool min, bool max)
{
    // 자리는 유지 (다른 줄과 열 맞춤), 칸만 안 보이게
    for (QLineEdit* e : {m_min, m_max}) {
        QSizePolicy sp = e->sizePolicy();
        sp.setRetainSizeWhenHidden(true);
        e->setSizePolicy(sp);
        e->setVisible(e == m_min ? min : max);
    }
}

void PropRow::setValues(const OptDouble& mn, const OptDouble& mx, double rowValue)
{
    static_cast<FilterValueEdit*>(m_min)->setRowValue(rowValue);
    static_cast<FilterValueEdit*>(m_max)->setRowValue(rowValue);
    m_min->setText(mn ? QString::number(*mn) : QString());
    m_max->setText(mx ? QString::number(*mx) : QString());
}

OptDouble PropRow::minValue() const
{
    return toOpt(m_min->text());
}

OptDouble PropRow::maxValue() const
{
    return toOpt(m_max->text());
}

QColor FilterRow::modColor(const QString& c)
{
    // 게임 아이템 설명 글씨 색 (어두운 배경 기준)
    if (c == QStringLiteral("crafted") || c == QStringLiteral("enchant") || c == QStringLiteral("rune"))
        return QColor(0xb4, 0xb4, 0xff);
    if (c == QStringLiteral("fractured"))
        return QColor(0xa2, 0x91, 0x62);
    if (c == QStringLiteral("scourge"))
        return QColor(0xff, 0x6e, 0x25);
    if (c == QStringLiteral("desecrated"))
        return QColor(0xb8, 0xd0, 0x8a);
    if (c == QStringLiteral("ambiguous"))
        return QColor(0xff, 0x9e, 0xc4); // 우리 쪽 표시: 종류가 애매한 줄
    return QColor(0x88, 0x88, 0xff); // 일반 / 고정 옵션
}

} // namespace pts
