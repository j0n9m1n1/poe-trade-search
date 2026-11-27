// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/TradeDb.h"
#include "core/Types.h"

#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

namespace pts {

class Keywords;
class CheckedStore;

struct ModCandidate {
    int entry = -1;    // 아이템 언어 DB의 stat 인덱스
    QString group;     // explicit / pseudo / implicit ...
    QString label;     // 그룹 표시명
    QString id;        // 검색용 stat id ("explicit.stat_123")
    QJsonValue option; // '|N' 형식 stat 의 옵션 값
};

struct ParsedMod {
    QString raw;     // 원본 줄
    QString text;    // 표시 텍스트
    QString modType; // 줄 끝 "(implicit)" 등이나 고급 설명 헤더로 알아낸 그룹, 모르면 ""
    int tier = 0;    // Ctrl+Alt+C 등급 (0 = 모름)
    QString statKey; // "stat_123" (자동 체크 / pseudo / 저항 판별 키)
    QVector<ModCandidate> cands;
    int selected = 0;
    OptDouble value;          // 대표값 (범위형 "# to #" 은 평균)
    OptDouble rollLo, rollHi; // 고급 설명의 롤 범위
    OptDouble rollPct;        // 롤 % (0~100, 좋은 방향 기준)
    int better = 1;           // 1 높을수록 좋음 / -1 낮을수록 좋음 / 0 비교 불가
    OptDouble min, max;       // 검색 값 (범위 채우기 결과, UI 에서 수정)
    bool optionStat = false; // min/max 대신 option 값으로 검색 (성단 주얼 인챈트 등)
    bool resistance = false; // 저항 합산 가능
    bool totalRes = false;   // 저항 합산 체크
    bool lockedOff = false;  // 검색 불가 옵션
    bool rangeLocked = false; // min/max 를 규칙으로 정함 (값 완화로 덮어쓰지 않음, 클러스터 패시브 개수 등)
    bool checked = false;
    bool autoChecked = false; // Checked.txt 로 자동 체크됨
    bool unmatched = false;   // 거래소 stat 을 못 찾은 줄 (표시만, 검색 불가)
    QString color;            // implicit / crafted / enchant / scourge / rune / desecrated / fractured / ambiguous / ""
    QString shown;            // 화면 표시용: 실제 수치가 들어간 게임 문구 (여러 줄은 " / ")
    QString affix;            // 고급 설명 기준 prefix / suffix / "" (모름)

    const ModCandidate* current() const { return selected >= 0 && selected < cands.size() ? &cands[selected] : nullptr; }
};

// 아이템 자체 속성 (방어도/회피/DPS 등). key 는 거래소 필터 id (ar/ev/es/ward/block/spirit/dps/pdps/edps)
struct ItemProp {
    QString key;
    double value = 0; // 거래소 기준값 (방어/물리 DPS 는 퀄리티 20 기준)
    bool use = false; // 기본으로 검색에 넣을지 (APT 규칙)
};

struct ParsedItem {
    Game game = Game::Poe1;
    Lang lang = Lang::En;

    QString itemClass;
    QString category; // "weapon.basesword" / "armour" / "" ...
    QString top;      // "weapon"
    QString rarityId;
    QString rarityText;

    QString name; // 검색용 (고유만)
    QString type; // 검색용 베이스
    QString rawName, rawType;
    QString disc; // 변형 젬 discriminator

    bool unidentified = false;
    bool corrupted = false;
    bool twiceCorrupted = false; // "두 번 타락" (corrupted 도 true)
    bool mirrored = false;
    bool split = false;
    bool advanced = false; // Ctrl+Alt+C 고급 설명 (롤 범위가 있음)
    bool fractured = false;
    bool synthesised = false;
    bool searing = false;
    bool tangled = false;
    bool foulborn = false;
    bool blighted = false;
    bool blightRavaged = false;
    bool vaalGem = false;
    bool transfigured = false;
    bool imbued = false; // 부여된 젬 (보조 젬 내장, "부여된"/"Imbued" 한 줄)
    QVector<int> influences; // Keywords::influences() 인덱스

    int quality = -1;
    int itemLevel = -1;
    int level = -1;
    bool levelMax = false;
    int mapTier = -1;
    int sockets = -1;
    int links = -1;
    int stackSize = -1;

    QString heistValue;      // heist_objective_value 옵션 id
    QString ultimatumReward; // ultimatum_reward 옵션 id
    QString ultimatumOutput; // 고유 아이템 보상 이름

    bool isGem = false, isCurrency = false, isCard = false, isMap = false, isWaystone = false, isMapFragment = false;
    bool isGear = false, isWeapon = false, isJewel = false, isFlask = false, isHeist = false, isUltimatum = false;
    bool isBeast = false;

    bool detail = false; // 옵션 대신 설명 표시
    QString detailText;
    QString exchangeId; // static id (일괄 교환 가능하면)

    bool hasDps = false;
    double pdps = 0, edps = 0, cdps = 0;
    int emptyPrefixes = -1, emptySuffixes = -1; // -1 = 개수를 모르거나 제작 대상 아님

    QVector<ItemProp> props;  // 방어도/회피/에너지 보호막/DPS ... (있는 것만)
    QVector<ParsedMod> mods; // 매칭 못 한 옵션 줄도 unmatched=true 로 순서대로 들어감
    QStringList unmatched;   // 매칭 못 한 줄 전부 (설명문 포함, 디버그용)

    // UI 기본값
    bool checkLevel = false, checkQuality = false, checkSockets = false, byCategory = false;
    bool highlightQuality = false, highlightLevel = false;
    int corruptDefault = 0; // 0 any / 1 yes / 2 no
    int ilvlFloor = -1, ilvlCeil = -1; // 아이템 레벨 필터 기본 범위 (클러스터 주얼 구간)
    int twiceCorruptDefault = 0; // 두 번 타락 필터 0 any / 1 yes / 2 no
    int mutatedDefault = 0;      // 삿된(PoE1) 필터 0 any / 1 yes / 2 no
    QString rarityFilter;   // "" / "unique" / "nonunique" ...
    bool checkAllUnique = false;
};

struct ParseOptions {
    int statRangePct = 0; // 최소값을 값의 N% 만큼 낮춤 (0 = 정확값). 앱 기본 10
    bool autoSelectPseudo = true;
    bool autoCheckTotalRes = true;
    bool autoCheckUnique = true;
    QString autoSelectCorrupt; // "", "yes", "no"
    QStringList autoSelectByType;
};

// 검색 범위 채우기 (APT 방식): 좋은 방향 쪽으로 값의 pct% 만큼 완화.
// 고유는 롤 범위 폭의 pct%, 최대 롤 고유/미러/타락 매직은 정확값. better=0 은 min/max 둘 다.
void applyStatRange(ParsedMod& m, int pct, bool unique);
void applyStatRanges(ParsedItem& it, int pct);
bool exactValuesOnly(const ParsedItem& it);

class ItemParser {
public:
    // dbs: [poe1, poe2]
    ItemParser(const Keywords& kw, const GameDb* dbs, const CheckedStore* checked);

    std::optional<ParsedItem> parse(const QString& text, Game game, const ParseOptions& opt,
                                    QString* error = nullptr) const;

    bool looksLikeItem(const QString& text) const;
    // 창 제목을 못 얻었을 때 텍스트로 게임 추정
    Game guessGame(const QString& text, Game fallback) const;

private:
    struct CleanLine {
        QString text;
        QString raw;
        QString rangeSrc; // 범위 뽑을 텍스트 (별칭 치환된 줄은 빈 문자열)
        QString modType;
        QString affix;   // prefix / suffix / ""
        QString display; // 별칭 치환 전 문구 (수치 포함, 범위/접미 제거)
        int tier = 0;
    };

    QString resolveCategory(const ParsedItem& it, const TradeDb& db) const;
    QString findBaseInName(const TradeDb& db, const QString& s) const;
    ParsedMod buildMod(const ParsedItem& it, const TradeDb& db, const MatcherDb& md, const QVector<StatMatch>& ms,
                       const CleanLine& first, const QString& raw, const QString& rangeSrc, const ParseOptions& opt,
                       bool* optionStatSeen) const;

    const Keywords& m_kw;
    const GameDb* m_dbs;
    const CheckedStore* m_checked;
};

} // namespace pts
