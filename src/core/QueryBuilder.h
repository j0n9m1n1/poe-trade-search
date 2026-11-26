// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Types.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVector>

namespace pts {

class Keywords;
class TradeDb;

struct StatQuery {
    QString id; // "explicit.stat_123"
    OptDouble min, max;
    QJsonValue option;
    bool disabled = false;
};

// UI 에서 사용자가 고른 값 그대로
struct SearchRequest {
    Game game = Game::Poe1;
    Lang lang = Lang::En;
    QString status = QStringLiteral("available");

    QString name;
    QString type;
    QString disc;
    bool byCategory = false;
    QString category;
    QString rarity; // "" = 모두

    bool useSockets = false;
    OptDouble socketsMin, socketsMax, linksMin, linksMax;

    bool useQuality = false;
    OptDouble qualityMin, qualityMax;

    enum class LevelKind { ItemLevel, GemLevel, MapTier };
    bool useLevel = false;
    LevelKind levelKind = LevelKind::ItemLevel;
    OptDouble levelMin, levelMax;

    int corrupted = 0; // 0 any / 1 yes / 2 no
    QVector<QString> influencePseudo; // "pseudo.pseudo_has_shaper_influence" ...
    bool synthesised = false;
    bool blighted = false;
    bool blightRavaged = false;
    bool transfigured = false;

    QString heistObjective;
    QString ultimatumReward;
    QString ultimatumOutput;

    QVector<StatQuery> stats;
    bool includeDisabledStats = true; // 브라우저용은 꺼진 옵션도 같이 보냄

    QString indexed; // "" = any
    OptDouble priceMin;
    QString saleType; // "" = 거래소 기본(즉시구매/고정가)
};

class QueryBuilder {
public:
    // db: 필터 그룹 이름을 알아내는 데 씀 (없으면 게임별 기본값)
    static QJsonObject build(const SearchRequest& r, const TradeDb* db);
    static QJsonObject buildExchange(const QString& have, const QString& want, const QString& status = QStringLiteral("online"));
    // search_before_day -> indexed 옵션
    static QString indexedFromDays(int days);
};

} // namespace pts
