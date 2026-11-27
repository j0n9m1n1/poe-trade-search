// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/PriceEstimate.h"

#include <QHash>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace pts {

bool Rates::convert(double amount, const QString& currency, double* out) const
{
    if (!valid() || !std::isfinite(amount))
        return false;
    if (currency == base) {
        *out = amount;
        return true;
    }
    const double r = toBase.value(currency, 0);
    if (!std::isfinite(r) || r <= 0)
        return false;
    *out = amount * r;
    return std::isfinite(*out);
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

struct PriceSample {
    double value;
    QString account;
};

QVector<double> marketPrices(QVector<PriceSample>& samples, double baitRatio)
{
    std::stable_sort(samples.begin(), samples.end(), [](const PriceSample& a, const PriceSample& b) {
        return a.value < b.value;
    });
    QHash<QString, int> perSeller;
    samples.erase(std::remove_if(samples.begin(), samples.end(), [&](const PriceSample& sample) {
        return !sample.account.isEmpty() && ++perSeller[sample.account] > 2;
    }), samples.end());
    QVector<double> values;
    for (const PriceSample& sample : samples)
        values.push_back(sample.value);
    if (values.size() <= 3)
        return values;

    // 싼 가격대에 독립 판매자가 둘 이상이면 미끼로 빼지 않음.
    const double cutoff = quantile(values, 0.5) * baitRatio;
    qsizetype first = 0;
    for (; first < samples.size() && samples[first].value < cutoff; ++first) {
        const PriceSample& low = samples[first];
        if (low.account.isEmpty())
            continue;
        const auto nextSeller = std::find_if(samples.cbegin() + first + 1, samples.cend(), [&](const PriceSample& sample) {
            return !sample.account.isEmpty() && sample.account != low.account;
        });
        if (nextSeller != samples.cend() && nextSeller->value < cutoff && nextSeller->value <= low.value * 1.5)
            break;
    }
    return values.mid(first);
}

double roundedPrice(double value, bool inDivine)
{
    const double rounded = inDivine || value < 10 ? std::round(value * 10.0) / 10.0 : std::round(value);
    return rounded > 0 ? rounded : value;
}
} // namespace

Estimate estimatePrice(const QVector<Listing>& listings, const Rates& rates)
{
    Estimate e;
    QVector<PriceSample> samples;
    qsizetype unconverted = 0;
    for (const Listing& l : listings) {
        if (!std::isfinite(l.amount) || l.amount <= 0 || l.currency.isEmpty())
            continue;
        double v = 0;
        if (rates.convert(l.amount, l.currency, &v) && v > 0)
            samples.push_back({v, l.account});
        else
            ++unconverted;
    }
    // 환산 못 한 매물이 20% 이상이면 원화폐별 시세 범위로 표시.
    if (samples.isEmpty() || unconverted * 4 >= samples.size())
        return e;
    const QVector<double> win = marketPrices(samples, 0.4).mid(0, 10);
    e.used = win.size();
    e.value = quantile(win, 0.5);
    e.lo = win.size() <= 3 ? win.first() : quantile(win, 0.2);
    e.hi = win.size() <= 3 ? win.last() : quantile(win, 0.8);
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

PriceTag displayPrice(double v, const Rates& rates)
{
    if (!rates.valid() || !std::isfinite(v) || v <= 0)
        return {};
    const double div = rates.divine();
    if (div > 0 && v >= div)
        return {roundedPrice(v / div, true), QStringLiteral("divine")};
    return {roundedPrice(v, false), rates.base};
}

QString formatValue(double v, const Rates& rates)
{
    const PriceTag p = displayPrice(v, rates);
    if (!p.valid())
        return {};
    return PriceSummary::formatAmount(p.amount) + QLatin1Char(' ') + shortCurrencyName(p.currency);
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
        return PriceSummary::formatAmount(roundedPrice(x, inDiv));
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
    if (take <= 0)
        return 0;
    QVector<PriceSample> samples;
    for (const Listing& l : offers) {
        if (l.payAmount <= 0 || l.getAmount <= 0)
            continue;
        const double ratio = l.payAmount / l.getAmount;
        if (std::isfinite(ratio) && ratio > 0)
            samples.push_back({ratio, l.account});
    }
    return quantile(marketPrices(samples, 0.5).mid(0, take), 0.5);
}

} // namespace pts
