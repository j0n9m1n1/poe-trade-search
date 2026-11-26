// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/PriceSummary.h"

#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <QtMath>
#include <algorithm>

namespace pts {

QString PriceSummary::formatAmount(double v)
{
    if (std::fabs(v - std::round(v)) < 1e-9)
        return QString::number(static_cast<qint64>(std::round(v)));
    return QString::number(v, 'f', v < 1 ? 3 : (v < 10 ? 2 : 1)).remove(QRegularExpression(QStringLiteral("0+$")));
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
    msg += QStringLiteral(" = ") + (top.isEmpty() ? QStringLiteral("가장 많은 가격 없음") : top.join(QStringLiteral(", ")));
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
