// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>
#include <functional>

namespace pts {

struct Listing {
    double amount = 0;
    QString currency;
    QString account;
    QDateTime indexed;
    // 일괄 교환용
    double payAmount = 0; // 내가 내는 양 (have)
    double getAmount = 0; // 받는 양 (want)
    int stock = 0;
};

class PriceSummary {
public:
    using NameFn = std::function<QString(const QString& currencyId)>;

    // "최저 ~ 최고 = 최빈1[n], 최빈2[n]" (원본 표기 방식)
    static QString summarize(const QVector<Listing>& listings, const NameFn& name);
    static QString priceKey(double amount, const QString& currency);
    // "5분 전" 같은 경과 시간
    static QString ago(const QDateTime& t, const QDateTime& now = QDateTime::currentDateTimeUtc());
    static QString formatAmount(double v);
};

} // namespace pts
