// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/PriceEstimate.h"

#include <QtMath>
#include <algorithm>

namespace pts {

bool Rates::convert(double amount, const QString& currency, double* out) const
{
    if (!valid())
        return false;
    if (currency == base) {
        *out = amount;
        return true;
    }
    const double r = toBase.value(currency, 0);
    if (r <= 0)
        return false;
    *out = amount * r;
    return true;
}

namespace {
double quantile(const QVector<double>& sorted, double q)
{
    if (sorted.isEmpty())
        return 0;
    const double pos = q * (sorted.size() - 1);
    const int i = static_cast<int>(std::floor(pos));
    const int j = qMin(i + 1, static_cast<int>(sorted.size()) - 1);
    return sorted[i] + (sorted[j] - sorted[i]) * (pos - i);
}
} // namespace

Estimate estimatePrice(const QVector<Listing>& listings, const Rates& rates)
{
    Estimate e;
    QVector<double> vals;
    for (const Listing& l : listings) {
        double v = 0;
        if (rates.convert(l.amount, l.currency, &v) && v > 0)
            vals.push_back(v);
    }
    std::sort(vals.begin(), vals.end());
    e.used = vals.size();
    if (vals.isEmpty())
        return e;
    // 전체 중앙값의 40% 미만은 덤핑/가격 낚시로 보고 제외 (다 걸러지면 원래대로)
    const double allMid = quantile(vals, 0.5);
    QVector<double> kept;
    for (double v : vals) {
        if (v >= allMid * 0.4)
            kept.push_back(v);
    }
    if (!kept.isEmpty())
        vals = kept;
    const QVector<double> win = vals.mid(0, qMin<qsizetype>(10, vals.size()));
    e.value = quantile(win, 0.5);
    e.lo = quantile(win, 0.2);
    e.hi = quantile(win, 0.8);
    const double spread = e.value > 0 ? (e.hi - e.lo) / e.value : 1.0;
    if (win.size() >= 8 && spread <= 0.35)
        e.confidence = QStringLiteral("높음");
    else if (win.size() >= 4 && spread <= 0.8)
        e.confidence = QStringLiteral("보통");
    else
        e.confidence = QStringLiteral("낮음");
    e.ok = true;
    return e;
}

QString shortCurrencyName(const QString& id)
{
    if (id == QStringLiteral("chaos"))
        return QStringLiteral("카오스");
    if (id == QStringLiteral("divine"))
        return QStringLiteral("디바인");
    if (id == QStringLiteral("exalted"))
        return QStringLiteral("엑잘");
    if (id == QStringLiteral("mirror"))
        return QStringLiteral("미러");
    return id;
}

QString formatValue(double v, const Rates& rates)
{
    const double div = rates.divine();
    if (div > 0 && v >= div)
        return PriceSummary::formatAmount(std::round(v / div * 10.0) / 10.0) + QLatin1Char(' ') +
               shortCurrencyName(QStringLiteral("divine"));
    const double r = v < 10 ? std::round(v * 10.0) / 10.0 : std::round(v);
    return PriceSummary::formatAmount(r) + QLatin1Char(' ') + shortCurrencyName(rates.base);
}

QString formatEstimate(const Estimate& e, const Rates& rates, int total)
{
    if (!e.ok)
        return {};
    // 범위는 추정값과 같은 단위로
    const double div = rates.divine();
    const bool inDiv = div > 0 && e.value >= div;
    auto unit = [&](double v) {
        const double x = inDiv ? v / div : v;
        return PriceSummary::formatAmount(x < 10 ? std::round(x * 10.0) / 10.0 : std::round(x));
    };
    return QStringLiteral("≈ %1  (%2~%3 · 신뢰도 %4 · 매물 %5)")
        .arg(formatValue(e.value, rates), unit(e.lo), unit(e.hi), e.confidence)
        .arg(total);
}

QString formatConversion(double amount, const QString& currency, const Rates& rates)
{
    double v = 0;
    if (!rates.convert(amount, currency, &v))
        return {};
    auto fmt = [](double x) { return PriceSummary::formatAmount(x < 10 ? std::round(x * 100.0) / 100.0 : std::round(x)); };
    if (currency == rates.base) {
        const double div = rates.divine();
        if (div <= 0)
            return {};
        return fmt(v / div) + QLatin1Char(' ') + shortCurrencyName(QStringLiteral("divine"));
    }
    return fmt(v) + QLatin1Char(' ') + shortCurrencyName(rates.base);
}

double medianRatio(const QVector<Listing>& offers, int take)
{
    // 응답 순서는 믿을 수 없음 (QJsonObject 가 키 순으로 정렬함) -> 직접 정렬
    QVector<double> r;
    for (const Listing& l : offers) {
        if (l.payAmount > 0 && l.getAmount > 0)
            r.push_back(l.payAmount / l.getAmount);
    }
    if (r.isEmpty())
        return 0;
    std::sort(r.begin(), r.end());
    // 전체 중앙값의 절반도 안 되는 미끼/실수 매물 제외 후, 가장 싼 take 개의 중앙값
    const double all = quantile(r, 0.5);
    QVector<double> ok;
    for (double v : r) {
        if (v >= all * 0.5)
            ok.push_back(v);
    }
    if (ok.isEmpty())
        ok = r;
    return quantile(ok.mid(0, take), 0.5);
}

} // namespace pts
