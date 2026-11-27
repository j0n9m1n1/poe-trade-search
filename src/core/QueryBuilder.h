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

// 아이템 속성 필터 (거래소 필터 id: ar/ev/es/ward/block/spirit/dps/pdps/edps)
struct PropQuery {
    QString id;
    OptDouble min, max;
};

// UI 에서 사용자가 고른 값 그대로
struct SearchRequest {
    Game game = Game::Poe1;
    Lang lang = Lang::En;
    QString status = QStringLiteral("available");

    QString name;
    QString type;
    QString disc;
    bool byCategory = false; // 이름/베이스 무시, 유형으로
    bool useName = true;     // 고유 이름
    bool useType = true;     // 베이스 (끄면 유형으로)
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
    int twiceCorrupted = 0; // 두 번 타락 0 any / 1 yes / 2 no
    int mutated = 0;        // 삿된 / 함양된 바알 고유 0 any / 1 yes / 2 no
    int mirrored = 0;          // 복제된 매물 0 무관 / 1 복제만 / 2 복제 제외
    bool notSplit = false;     // 분할된 매물 제외 (PoE1)
    bool notFractured = false; // 분열 매물 제외
    QVector<QString> influencePseudo; // "pseudo.pseudo_has_shaper_influence" ...
    bool synthesised = false;
    bool blighted = false;
    bool blightRavaged = false;
    bool transfigured = false;

    QString heistObjective;
    QString ultimatumReward;
    QString ultimatumOutput;

    QVector<PropQuery> props;
    QVector<StatQuery> stats;
    bool includeDisabledStats = true; // 브라우저용은 꺼진 옵션도 같이 보냄

    QString indexed; // "" = any
    OptDouble priceMin;
    bool collapse = false; // 계정별 묶기
    QString saleType; // "" = 거래소 기본(즉시 구입/고정 가격)
    QString priceOption; // 가격 화폐 필터. "" = 등가물(모든 화폐), "chaos_divine", "chaos" ...
};

// 거래소 드롭다운 선택지 (id, 카카오 거래소 표기). id "" = 필터 안 넣음
struct TradeChoice {
    QString id;
    QString text;
};
// "즉시 구입 가격" 화폐. 첫 항목 = 등가물
QVector<TradeChoice> priceCurrencies(Game g);
// "등록 완료" 기간. 첫 항목 = 언제든
QVector<TradeChoice> indexedChoices();

class QueryBuilder {
public:
    // db: 필터 그룹 이름을 알아내는 데 씀 (없으면 게임별 기본값)
    static QJsonObject build(const SearchRequest& r, const TradeDb* db);
    static QJsonObject buildExchange(const QString& have, const QString& want, const QString& status = QStringLiteral("online"));
    // search_before_day -> indexed 옵션
    static QString indexedFromDays(int days);
    // 설정값 -> 등록일 옵션. days < 0 (자동) 이면 즉시 구입만은 언제든, 직접 거래 포함은 3일
    // (APT/EE2 기본은 언제든 + 즉시 구입. 즉시 구입 매물은 판매자가 오프라인이어도 살 수 있어서 오래돼도 유효)
    static QString indexedDefault(int days, const QString& status);
};

} // namespace pts
