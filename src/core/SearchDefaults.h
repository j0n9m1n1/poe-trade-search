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

// 프리셋 (APT 참고)
//  기본: 파서 결과 그대로 (pseudo 우선, 자동 체크)
//  정확: pseudo 대신 원래 옵션, T1~T2 만 체크 (등급 모르면 일반 옵션 전부), 정확값
//  베이스: 옵션 전부 해제, 아이템 레벨/영향 위주
enum class Preset { Default = 0, Exact = 1, Base = 2 };
ParsedItem applyPreset(const ParsedItem& parsed, Preset p, int rangePct, ItemFilterState* st);

struct RequestContext {
    Lang server = Lang::En;
    QString status = QStringLiteral("available");
    QString indexed;
    OptDouble priceMin;
    QString priceOption;    // 가격 화폐 필터 ("" = 등가물)
    bool collapse = false;  // 계정별 묶기
    bool forBrowser = true; // false 면 꺼진 옵션은 아예 안 보냄 (시세 조회용)
};

// server 언어가 아이템 언어와 다르면 이름/베이스를 GameDb 로 번역 시도.
// 번역 실패 시 error 에 사유를 넣고 아이템 언어 그대로 씀.
SearchRequest makeRequest(const ParsedItem& it, const ItemFilterState& st, const Keywords& kw, const GameDb* gdb,
                          const RequestContext& ctx, QString* warning = nullptr);

SearchRequest::LevelKind levelKindFor(const ParsedItem& it);

} // namespace pts
