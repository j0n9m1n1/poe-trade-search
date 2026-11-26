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
    OptDouble min, max;
    bool optionStat = false; // min/max 대신 option 값으로 검색 (성단 주얼 인챈트 등)
    bool resistance = false; // 저항 합산 가능
    bool totalRes = false;   // 저항 합산 체크
    bool lockedOff = false;  // 검색 불가 옵션
    bool checked = false;
    bool autoChecked = false; // Checked.txt 로 자동 체크됨
    QString color;            // implicit / crafted / enchant / scourge / rune / desecrated / ambiguous / ""

    const ModCandidate* current() const { return selected >= 0 && selected < cands.size() ? &cands[selected] : nullptr; }
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
    bool mirrored = false;
    bool fractured = false;
    bool synthesised = false;
    bool searing = false;
    bool tangled = false;
    bool foulborn = false;
    bool blighted = false;
    bool blightRavaged = false;
    bool vaalGem = false;
    bool transfigured = false;
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

    QVector<ParsedMod> mods;
    QStringList unmatched;

    // UI 기본값
    bool checkLevel = false, checkQuality = false, checkSockets = false, byCategory = false;
    bool highlightQuality = false, highlightLevel = false;
    int corruptDefault = 0; // 0 any / 1 yes / 2 no
    QString rarityFilter;   // "" / "unique" / "nonunique" ...
    bool checkAllUnique = false;
};

struct ParseOptions {
    bool autoSelectPseudo = true;
    bool autoCheckTotalRes = true;
    bool autoCheckUnique = true;
    QString autoSelectCorrupt; // "", "yes", "no"
    QStringList autoSelectByType;
};

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
        QString modType;
        int tier = 0;
    };

    QString resolveCategory(const ParsedItem& it, const TradeDb& db) const;
    QString findBaseInName(const TradeDb& db, const QString& s) const;
    void buildMod(ParsedItem& it, const TradeDb& db, const QVector<StatMatch>& ms, const CleanLine& first,
                  const QString& raw, const ParseOptions& opt, bool* optionStatSeen) const;

    const Keywords& m_kw;
    const GameDb* m_dbs;
    const CheckedStore* m_checked;
};

} // namespace pts
