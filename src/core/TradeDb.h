// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/MatcherDb.h"
#include "core/TextUtil.h"
#include "core/Types.h"

#include <QHash>
#include <QJsonValue>
#include <QString>
#include <QVector>

namespace pts {

class Keywords;

struct StatEntry {
    QString id;        // "explicit.stat_123" (옵션 접미 '|N' 제외)
    QString key;       // "stat_123"
    QString group;     // "explicit"
    QString text;      // 표시용 텍스트 (끝의 "(Local)" 같은 한정어 제거)
    QString qualifier; // 한정어 원문 ("Local", "특정" ...)
    QString scope;     // local / staff / shield / map / legacy / global / jewel / charm / flask / tablet
    QJsonValue option; // "enchant.stat_3948993189|17" -> 17
    QString norm;      // 정규화 키
    int lines = 1;
    QVector<text::Token> toks;
    bool optionList = false; // 옵션 목록을 따로 고르는 형식 (사원 방 등) - 자동 매칭 안 함
};

struct StatGroup {
    QString id;
    QString label;
};

struct ItemEntry {
    QString group; // accessory / armour / ...
    QString type;  // 베이스
    QString text;  // "이름 베이스" 또는 변형 젬 이름
    QString name;  // 고유 이름
    QString disc;  // 변형 젬 discriminator (alt_x ...)
    bool unique = false;
    int order = 0; // 그룹 안 순번 (한/영 매칭용)
};

struct StaticEntry {
    QString id;
    QString text;
    QString group;
    QString groupLabel;
    QString image; // "/gen/image/..." (web.poecdn.com 기준 상대 경로)
};

struct FilterOption {
    QString id; // null 이면 빈 문자열
    QString text;
};

struct StatMatch {
    int entry = -1;
    QVector<double> values; // '#' 자리 값
    QVector<int> hashIdx;   // 각 값이 줄의 몇 번째 숫자였는지 (고급 설명 범위 찾기용)
};

// 거래소 /api/trade(2)/data/* 응답 하나(게임 x 언어)를 들고 있는 DB
class TradeDb {
public:
    bool load(const QString& dir, QString* error = nullptr);
    bool isLoaded() const { return m_loaded; }
    QString dir() const { return m_dir; }

    // 영어 DB의 한정어를 기준으로 scope 결정 (한국어 DB는 같은 id의 영어 한정어를 따라감)
    void resolveScopes(const Keywords& kw, const TradeDb* english);

    // stats
    const QVector<StatGroup>& groups() const { return m_groups; }
    bool hasGroup(const QString& id) const;
    QString groupLabel(const QString& id) const;
    const StatEntry& stat(int i) const { return m_stats[i]; }
    int statCount() const { return m_stats.size(); }
    // id: "explicit.stat_123" 또는 "enchant.stat_1|17"
    int findStat(const QString& fullId) const { return m_statById.value(fullId, -1); }
    QVector<StatMatch> match(const QStringList& lines, int start, int* consumed) const;

    // items
    const ItemEntry& item(int i) const { return m_items[i]; }
    QVector<int> itemsByType(const QString& type) const { return m_itemByType.value(type); }
    QVector<int> itemsByText(const QString& text) const { return m_itemByText.value(text); }
    QVector<int> itemsByName(const QString& name) const { return m_itemByName.value(name); }
    bool hasType(const QString& type) const { return m_itemByType.contains(type); }
    int groupItemCount(const QString& group) const { return m_itemsInGroup.value(group).size(); }
    int itemAt(const QString& group, int order) const;

    // static (일괄 교환용)
    const QVector<StaticEntry>& statics() const { return m_static; }
    const StaticEntry* staticById(const QString& id) const;
    const StaticEntry* staticByText(const QString& text) const;

    // filters
    QVector<FilterOption> filterOptions(const QString& filterId) const { return m_filterOptions.value(filterId); }
    QString filterGroup(const QString& filterId) const { return m_filterGroup.value(filterId); }
    QString filterText(const QString& filterId) const { return m_filterText.value(filterId); }

    // leagues
    const QVector<FilterOption>& leagues() const { return m_leagues; }

    static bool verify(const QVector<text::Token>& pattern, const QString& raw, QVector<double>* values,
                       QVector<int>* hashIdx);

private:

    bool m_loaded = false;
    QString m_dir;

    QVector<StatGroup> m_groups;
    QVector<StatEntry> m_stats;
    QHash<QString, int> m_statById;
    QHash<QString, QVector<int>> m_byNorm;
    QHash<QString, QVector<int>> m_multiFirst;

    QVector<ItemEntry> m_items;
    QHash<QString, QVector<int>> m_itemByType, m_itemByText, m_itemByName;
    QHash<QString, QVector<int>> m_itemsInGroup;

    QVector<StaticEntry> m_static;
    QHash<QString, int> m_staticById, m_staticByText;

    QHash<QString, QVector<FilterOption>> m_filterOptions;
    QHash<QString, QString> m_filterGroup, m_filterText;

    QVector<FilterOption> m_leagues;
};

// 한 게임의 한/영 DB 묶음
struct GameDb {
    TradeDb db[2];          // [ko, en]
    MatcherDb matchers[2];  // APT/EE2 매처 (없어도 동작)
    const TradeDb& operator[](Lang l) const { return db[li(l)]; }
    TradeDb& operator[](Lang l) { return db[li(l)]; }
    bool loaded(Lang l) const { return db[li(l)].isLoaded(); }

    // 한 언어의 베이스/이름을 다른 언어로 (그룹 크기가 같을 때만 순번으로 매칭)
    bool translateItem(Lang from, Lang to, const QString& type, const QString& name, QString* outType,
                       QString* outName) const;
};

} // namespace pts
