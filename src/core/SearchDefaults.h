// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/ItemParser.h"
#include "core/QueryBuilder.h"

namespace pts {

class Keywords;
struct GameDb;

// 옵션 줄 말고 아래쪽 필터들의 UI 편집 상태 (옵션 줄은 ParsedItem::mods 를 직접 고침)
struct ItemFilterState {
    bool useLevel = false;
    OptDouble levelMin, levelMax;
    bool useQuality = false;
    OptDouble qualityMin, qualityMax;
    bool useSockets = false;
    OptDouble socketsMin, socketsMax, linksMin, linksMax;
    int corrupted = 0;
    QString rarity;
    bool byCategory = false;
    QVector<int> influences; // Keywords::influences() 인덱스
    bool synthesised = false;
    bool blighted = false;
    bool blightRavaged = false;
    QString heistObjective;
    QString ultimatumReward;
    QString ultimatumOutput;
};

ItemFilterState defaultFilterState(const ParsedItem& it);

struct RequestContext {
    Lang server = Lang::En;
    QString status = QStringLiteral("available");
    QString indexed;
    OptDouble priceMin;
    bool forBrowser = true; // false 면 꺼진 옵션은 아예 안 보냄 (시세 조회용)
};

// server 언어가 아이템 언어와 다르면 이름/베이스를 GameDb 로 번역 시도.
// 번역 실패 시 error 에 사유를 넣고 아이템 언어 그대로 씀.
SearchRequest makeRequest(const ParsedItem& it, const ItemFilterState& st, const Keywords& kw, const GameDb* gdb,
                          const RequestContext& ctx, QString* warning = nullptr);

SearchRequest::LevelKind levelKindFor(const ParsedItem& it);

} // namespace pts
