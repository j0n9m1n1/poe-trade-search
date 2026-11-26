// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/TextUtil.h"
#include "core/Types.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

namespace pts {

// Awakened PoE Trade / Exiled Exchange 2 의 stats.ndjson (MIT) 매처 데이터.
// 게임 표기 ↔ 거래소 stat id 를 이어줌: 숫자 없는 100% 문구, "감소" 표기(negate), 좋은 방향(better)
//   {"ref":..., "better":1, "matchers":[{"string":"..."},{"string":"...","value":100},{"string":"...","negate":true}],
//    "trade":{"ids":{"explicit":["explicit.stat_1"],...},"inverted":true}}
struct MatcherStat {
    QString ref;
    int better = 1; // 1 높을수록 좋음 / -1 낮을수록 좋음 / 0 비교 불가
    bool inverted = false;
    QVector<QPair<QString, QStringList>> ids; // 그룹 -> 거래소 id 목록
};

struct MatcherHit {
    int stat = -1;
    QVector<double> values;
    QVector<int> hashIdx;
    int lines = 1;
};

class MatcherDb {
public:
    bool load(const QString& path, QString* error = nullptr);
    bool isLoaded() const { return !m_stats.isEmpty(); }
    int size() const { return m_stats.size(); }

    const MatcherStat& stat(int i) const { return m_stats[i]; }
    QVector<MatcherHit> match(const QStringList& lines, int start, int* consumed) const;
    std::optional<int> betterOf(const QString& statKey) const;

private:
    struct Matcher {
        int stat = -1;
        bool hasValue = false;
        double value = 0;
        bool negate = false;
        int lines = 1;
        QVector<text::Token> toks;
    };
    void addMatcher(const QString& s, int stat, bool hasValue, double value, bool negate);

    QVector<MatcherStat> m_stats;
    QVector<Matcher> m_matchers;
    QHash<QString, QVector<int>> m_index;      // 정규화 문자열 -> matcher
    QHash<QString, QVector<int>> m_multiFirst; // 여러 줄 매처의 첫 줄 -> matcher
    QHash<QString, int> m_better;              // "stat_123" -> better
};

} // namespace pts
