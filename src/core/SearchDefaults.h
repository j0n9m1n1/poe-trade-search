// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/ItemParser.h"
#include "core/QueryBuilder.h"

namespace pts {

class Keywords;
struct GameDb;

// 옵션 줄 말고 아래쪽 필터들의 UI 편집 상태 (옵션 줄은 ParsedItem::mods 를 직접 고침)
// 아이템 속성 필터 (방어도/회피/DPS ...). key = 거래소 필터 id
struct PropFilterState {
    QString key;
    bool use = false;
    OptDouble min, max;
};

struct MinFilterState {
    bool use = false;
    OptDouble min;
};

struct ItemFilterState {
    bool useLevel = false;
    OptDouble levelMin, levelMax;
    bool useQuality = false;
    OptDouble qualityMin, qualityMax;
    bool useSockets = false;
    OptDouble socketsMin, socketsMax, linksMin, linksMax;
    int corrupted = 0;      // 0 any / 1 yes / 2 no
    int twiceCorrupted = 0; // 0 any / 1 yes / 2 no
    int mutated = 0;        // 삿된(PoE1) / 함양된 바알 고유(PoE2) 0 any / 1 yes / 2 no
    QString rarity;
    bool byCategory = false; // 이름/베이스 둘 다 무시하고 유형으로
    bool useName = true;     // 고유 이름으로 검색
    bool useType = true;     // 베이스로 검색 (끄면 아이템 유형으로)
    QVector<int> influences; // Keywords::influences() 인덱스
    bool synthesised = false;
    bool searing = false;
    bool tangled = false;
    bool blighted = false;
    bool blightRavaged = false;
    QString heistObjective;
    QString ultimatumReward;
    QString ultimatumOutput;
    QVector<PropFilterState> props;
    bool exact = false; // 정확 프리셋 (분열 아님 필터 등)
    int mirrored = 0;   // 복제된 아이템 0 무관 / 1 복제만 / 2 복제 제외
    bool notFractured = false; // 분열된 매물 제외 (분열 안 된 고유 제외 장비 기본 켬)
    bool modifiable = false;   // 수정 가능 (PoE Overlay): 타락·복제 안 된 매물만. 켜면 타락/복제 선택보다 우선
    MinFilterState emptyPrefixes, emptySuffixes;
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
    QString league;         // 영구 리그(스탠다드 등)면 분할 아님 필터를 안 넣음
    bool collapse = false;  // 계정별 묶기
    bool forBrowser = true; // false 면 꺼진 옵션은 아예 안 보냄 (시세 조회용)
};

// server 언어가 아이템 언어와 다르면 이름/베이스를 GameDb 로 번역 시도.
// 번역 실패 시 error 에 사유를 넣고 아이템 언어 그대로 씀.
SearchRequest makeRequest(const ParsedItem& it, const ItemFilterState& st, const Keywords& kw, const GameDb* gdb,
                          const RequestContext& ctx, QString* warning = nullptr);

SearchRequest::LevelKind levelKindFor(const ParsedItem& it);

} // namespace pts
