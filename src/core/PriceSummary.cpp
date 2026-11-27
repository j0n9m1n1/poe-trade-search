// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/PriceSummary.h"

#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace pts {

namespace {
QString stripMarkup(QString s)
{
    static const QRegularExpression kMarkup(QStringLiteral("<<[^>]*>>"));
    return s.remove(kMarkup).trimmed();
}

QString frameColor(int frameType)
{
    switch (frameType) {
    case 0:
        return QStringLiteral("#c8c8c8"); // 일반
    case 1:
        return QStringLiteral("#8888ff"); // 마법
    case 2:
        return QStringLiteral("#ffff77"); // 희귀
    case 3:
        return QStringLiteral("#e0823d"); // 고유
    case 4:
        return QStringLiteral("#1ba29b"); // 젬
    case 5:
        return QStringLiteral("#aa9e82"); // 화폐
    case 6:
        return QStringLiteral("#0ebaff"); // 점술 카드
    case 9:
        return QStringLiteral("#82ad6a"); // 유물
    default:
        return QStringLiteral("#e6e6e6");
    }
}

QString modBlock(const QStringList& mods, const QString& color)
{
    if (mods.isEmpty())
        return {};
    QStringList lines;
    for (const QString& m : mods) {
        if (!m.trimmed().isEmpty())
            lines << m.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    }
    if (lines.isEmpty())
        return {};
    return QStringLiteral("<div style=\"color:%1;\">%2</div>").arg(color, lines.join(QStringLiteral("<br>")));
}
} // namespace

QString PriceSummary::formatAmount(double v)
{
    if (std::fabs(v - std::round(v)) < 1e-9)
        return QString::number(static_cast<qint64>(std::round(v)));
    QString s = QString::number(v, 'f', v < 1 ? 3 : (v < 10 ? 2 : 1));
    while (s.endsWith(QLatin1Char('0')))
        s.chop(1);
    if (s.endsWith(QLatin1Char('.')))
        s.chop(1);
    return s;
}

QString PriceSummary::priceNote(double amount, const QString& currencyId)
{
    if (!std::isfinite(amount) || amount <= 0)
        return {};
    const QString id = currencyId.trimmed();
    if (id.isEmpty())
        return {};
    const QString amt = formatAmount(amount);
    if (amt.isEmpty() || amt == QStringLiteral("0"))
        return {};
    return QStringLiteral("~b/o %1 %2").arg(amt, id);
}

QString PriceSummary::listingTooltip(const Listing& l)
{
    const QString name = stripMarkup(l.itemName);
    const QString type = stripMarkup(!l.typeLine.isEmpty() ? l.typeLine : l.baseType);
    QStringList titleLines;
    if (!name.isEmpty())
        titleLines << name.toHtmlEscaped();
    if (!type.isEmpty() && type != name)
        titleLines << type.toHtmlEscaped();

    QStringList meta;
    if (l.ilvl > 0)
        meta << QStringLiteral("아이템 레벨: %1").arg(l.ilvl);
    if (l.quality > 0)
        meta << QStringLiteral("퀄리티: +%1%").arg(l.quality);
    if (l.dps > 0)
        meta << QStringLiteral("DPS: %1").arg(QString::number(l.dps, 'f', 1));

    QStringList flags;
    if (l.corrupted)
        flags << QStringLiteral("<span style=\"color:#e06666;\">타락</span>");
    if (!l.identified)
        flags << QStringLiteral("<span style=\"color:#e06666;\">미확인</span>");
    if (l.fractured)
        flags << QStringLiteral("<span style=\"color:#a29162;\">분열</span>");
    if (l.synthesised)
        flags << QStringLiteral("<span style=\"color:#88c8ff;\">결합</span>");
    if (l.mirrored)
        flags << QStringLiteral("<span style=\"color:#88c8ff;\">복제</span>");
    if (l.split)
        flags << QStringLiteral("<span style=\"color:#c8c8c8;\">분할</span>");

    QStringList sections;
    QString header;
    if (!titleLines.isEmpty())
        header += QStringLiteral("<div style=\"color:%1; font-weight:bold;\">%2</div>")
                      .arg(frameColor(l.frameType), titleLines.join(QStringLiteral("<br>")));
    if (!meta.isEmpty())
        header += QStringLiteral("<div style=\"color:#b8b8b8;\">%1</div>").arg(meta.join(QStringLiteral(" · ")).toHtmlEscaped());
    if (!flags.isEmpty())
        header += QStringLiteral("<div>%1</div>").arg(flags.join(QStringLiteral(" · ")));
    if (!header.isEmpty())
        sections << header;

    const QString ench = modBlock(l.enchantMods, QStringLiteral("#b4b4ff"));
    if (!ench.isEmpty())
        sections << ench;
    const QString rune = modBlock(l.runeMods, QStringLiteral("#b4b4ff"));
    if (!rune.isEmpty())
        sections << rune;
    const QString imp = modBlock(l.implicitMods, QStringLiteral("#8888ff"));
    if (!imp.isEmpty())
        sections << imp;

    QString mainMods;
    mainMods += modBlock(l.fracturedMods, QStringLiteral("#a29162"));
    mainMods += modBlock(l.explicitMods, QStringLiteral("#8888ff"));
    mainMods += modBlock(l.craftedMods, QStringLiteral("#b4b4ff"));
    if (!mainMods.isEmpty())
        sections << mainMods;

    if (sections.isEmpty())
        return {};
    return QStringLiteral("<div>") + sections.join(QStringLiteral("<hr>")) + QStringLiteral("</div>");
}

QString PriceSummary::priceKey(double amount, const QString& currency)
{
    // 10 미만은 소수 1자리, 그 이상은 정수로 묶어서 빈도 계산
    const double r = amount < 10 ? std::round(amount * 10.0) / 10.0 : std::round(amount);
    return formatAmount(r) + QLatin1Char(' ') + currency;
}

QString PriceSummary::summarize(const QVector<Listing>& listings, const NameFn& name)
{
    if (listings.isEmpty())
        return {};
    auto label = [&](const Listing& l) {
        const QString n = name ? name(l.currency) : l.currency;
        return priceKey(l.amount, n.isEmpty() ? l.currency : n);
    };

    QHash<QString, int> counts;
    QStringList order;
    for (const Listing& l : listings) {
        const QString k = label(l);
        if (!counts.contains(k))
            order << k;
        counts[k]++;
    }
    QString msg = label(listings.first()) + QStringLiteral(" ~ ") + label(listings.last());

    QStringList keys = order;
    std::stable_sort(keys.begin(), keys.end(), [&](const QString& a, const QString& b) { return counts[a] > counts[b]; });
    QStringList top;
    for (const QString& k : keys) {
        if (top.size() == 2)
            break;
        if (counts[k] < 2)
            continue;
        top << QStringLiteral("%1[%2]").arg(k).arg(counts[k]);
    }
    msg += QStringLiteral(" = ") + (top.isEmpty() ? QStringLiteral("두 번 이상 나온 가격 없음") : top.join(QStringLiteral(", ")));
    return msg;
}

QString PriceSummary::ago(const QDateTime& t, const QDateTime& now)
{
    if (!t.isValid())
        return {};
    const qint64 s = qMax<qint64>(0, t.secsTo(now));
    if (s < 60)
        return QStringLiteral("%1초 전").arg(s);
    if (s < 3600)
        return QStringLiteral("%1분 전").arg(s / 60);
    if (s < 86400)
        return QStringLiteral("%1시간 전").arg(s / 3600);
    const qint64 d = s / 86400;
    if (d < 7)
        return QStringLiteral("%1일 전").arg(d);
    if (d < 31)
        return QStringLiteral("%1주 전").arg(d / 7);
    return t.toLocalTime().toString(QStringLiteral("yyyy-MM-dd"));
}

} // namespace pts
