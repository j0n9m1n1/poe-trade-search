// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/PriceSummary.h"
#include "core/Types.h"

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QVector>

namespace pts {

// 화폐 환율. 기준 화폐(PoE1 chaos / PoE2 exalted) 1개 = 1
// 값은 거래소 일괄 교환 API 로 직접 계산함 (poe.ninja 안 씀: 카카오/PoE2 도 그대로 되게)
struct Rates {
    QString base;
    QHash<QString, double> toBase; // 화폐 id -> 기준 화폐 몇 개
    QDateTime at;

    static QString baseFor(Game g) { return g == Game::Poe1 ? QStringLiteral("chaos") : QStringLiteral("exalted"); }
    bool valid() const { return !base.isEmpty(); }
    bool fresh(int maxAgeSec = 1800) const { return valid() && at.isValid() && at.secsTo(QDateTime::currentDateTimeUtc()) < maxAgeSec; }
    bool convert(double amount, const QString& currency, double* out) const;
    double divine() const { return toBase.value(QStringLiteral("divine"), 0); }
};

// 시세 추정: 싼 쪽부터 최대 10개 매물의 중앙값, 20~80% 구간을 범위로, 개수/퍼짐으로 신뢰도
struct Estimate {
    bool ok = false;
    double value = 0, lo = 0, hi = 0; // 기준 화폐 단위
    int used = 0;                     // 추정에 사용한 매물 수
    QString confidence;               // 높음 / 보통 / 낮음
};

Estimate estimatePrice(const QVector<Listing>& listings, const Rates& rates);

// 표시 단위 수량과 거래소 화폐 id (가격 노트 복사용)
struct PriceTag {
    double amount = 0;
    QString currency;
    bool valid() const { return amount > 0 && !currency.isEmpty(); }
};

// 기준 화폐 값 -> 표시 단위로 반올림한 수량·화폐 id (formatValue 와 같은 기준)
PriceTag displayPrice(double baseValue, const Rates& rates);

// 디바인 이상이면 디바인, 아니면 기준 화폐로 ("28.1 디바인", "350 카오스")
QString formatValue(double baseValue, const Rates& rates);
QString formatEstimate(const Estimate& e, const Rates& rates, int total);
// 시세 표 '환산' 칸: 기준 화폐 가격은 디바인으로, 그 외는 기준 화폐로 (같은 단위면 빈 문자열)
QString formatConversion(double amount, const QString& currency, const Rates& rates);
QString shortCurrencyName(const QString& id);

// 일괄 교환 offer 들 (pay / get) 의 중앙값 비율
double medianRatio(const QVector<Listing>& offers, int take = 5);

} // namespace pts
