// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

namespace pts {

struct Listing {
    double amount = 0;
    QString currency;
    QString account;
    QDateTime indexed;
    // 아이콘 캐시용 (fetch 결과의 item)
    QString icon;
    QString itemName;
    QString baseType;
    // 매물 툴팁용 (fetch 결과의 item)
    QString typeLine;
    int frameType = -1; // 0 일반 / 1 마법 / 2 희귀 / 3 고유 / 4 젬 / 5 화폐 / 6 카드
    bool corrupted = false;
    bool fractured = false;
    bool synthesised = false;
    bool mirrored = false;
    bool split = false;
    bool identified = true;
    QStringList enchantMods;
    QStringList implicitMods;
    QStringList fracturedMods;
    QStringList explicitMods;
    QStringList craftedMods;
    QStringList runeMods;
    // 시세 표 컬럼
    int ilvl = -1;
    int quality = -1;
    double dps = 0;
    int online = -1; // 0 오프라인 / 1 온라인 / 2 자리비움(afk)
    int stack = 0;
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
    // PoE 가격 노트 ("~b/o 13 divine", "~b/o 1.5 chaos")
    static QString priceNote(double amount, const QString& currencyId);
    // 일반 검색 매물 아이템 요약 HTML 툴팁
    static QString listingTooltip(const Listing& listing);
    // "5분 전" 같은 경과 시간
    static QString ago(const QDateTime& t, const QDateTime& now = QDateTime::currentDateTimeUtc());
    static QString formatAmount(double v);
};

} // namespace pts
