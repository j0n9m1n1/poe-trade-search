// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/MatcherDb.h"

#include "core/TradeDb.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace pts {

void MatcherDb::addMatcher(const QString& s, int stat, bool hasValue, double value, bool negate)
{
    if (s.isEmpty())
        return;
    Matcher m;
    m.stat = stat;
    m.hasValue = hasValue;
    m.value = value;
    m.negate = negate;
    m.lines = s.count(QLatin1Char('\n')) + 1;
    m.toks = text::tokens(s);
    const int idx = m_matchers.size();
    m_matchers.push_back(m);
    const QString norm = text::normalizeStat(s);
    m_index[norm].push_back(idx);
    if (m.lines > 1)
        m_multiFirst[norm.section(QLatin1Char('\n'), 0, 0)].push_back(idx);
}

bool MatcherDb::load(const QString& path, QString* error)
{
    *this = MatcherDb();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    while (!f.atEnd()) {
        const QByteArray line = f.readLine().trimmed();
        if (line.isEmpty())
            continue;
        const QJsonObject o = QJsonDocument::fromJson(line).object();
        const QJsonArray matchers = o.value(QStringLiteral("matchers")).toArray();
        const QJsonObject trade = o.value(QStringLiteral("trade")).toObject();
        if (matchers.isEmpty() || trade.isEmpty())
            continue;

        MatcherStat st;
        st.ref = o.value(QStringLiteral("ref")).toString();
        st.better = o.value(QStringLiteral("better")).toInt(1);
        st.inverted = trade.value(QStringLiteral("inverted")).toBool();
        const QJsonObject ids = trade.value(QStringLiteral("ids")).toObject();
        for (auto it = ids.begin(); it != ids.end(); ++it) {
            QStringList list;
            for (const auto& v : it.value().toArray())
                list << v.toString();
            st.ids.push_back({it.key(), list});
            for (const QString& id : list) {
                const QString key = id.section(QLatin1Char('.'), 1).section(QLatin1Char('|'), 0, 0);
                if (!key.isEmpty() && !m_better.contains(key))
                    m_better.insert(key, st.better);
            }
        }
        const int si = m_stats.size();
        m_stats.push_back(st);

        for (const auto& mv : matchers) {
            const QJsonObject m = mv.toObject();
            const bool hasValue = m.contains(QStringLiteral("value"));
            const double value = m.value(QStringLiteral("value")).toDouble();
            const bool negate = m.value(QStringLiteral("negate")).toBool();
            addMatcher(m.value(QStringLiteral("string")).toString(), si, hasValue, value, negate);
            if (m.contains(QStringLiteral("advanced")))
                addMatcher(m.value(QStringLiteral("advanced")).toString(), si, hasValue, value, negate);
        }
    }
    if (m_stats.isEmpty() && error)
        *error = QStringLiteral("no matchers");
    return !m_stats.isEmpty();
}

QVector<MatcherHit> MatcherDb::match(const QStringList& lines, int start, int* consumed) const
{
    *consumed = 0;
    QVector<MatcherHit> out;
    if (start < 0 || start >= lines.size())
        return out;

    auto tryOne = [&](int mi, const QString& raw) {
        const Matcher& m = m_matchers[mi];
        MatcherHit h;
        h.stat = m.stat;
        h.lines = m.lines;
        if (!TradeDb::verify(m.toks, raw, &h.values, &h.hashIdx))
            return false;
        if (m.hasValue) {
            h.values = {m.value};
            h.hashIdx = {-1};
        }
        if (m.negate) {
            for (double& v : h.values)
                v = -v;
        }
        out.push_back(h);
        return true;
    };

    const QString n0 = text::normalizeStat(lines[start]);
    auto mit = m_multiFirst.constFind(n0);
    if (mit != m_multiFirst.constEnd()) {
        for (int mi : *mit) {
            const int n = m_matchers[mi].lines;
            if (start + n > lines.size())
                continue;
            const QString joined = lines.mid(start, n).join(QLatin1Char('\n'));
            if (m_index.value(text::normalizeStat(joined)).contains(mi) && tryOne(mi, joined)) {
                *consumed = n;
                return out;
            }
        }
    }
    for (int mi : m_index.value(n0)) {
        if (tryOne(mi, lines[start]))
            break; // 한 문구에 매처 하나면 충분
    }
    if (!out.isEmpty())
        *consumed = 1;
    return out;
}

std::optional<int> MatcherDb::betterOf(const QString& statKey) const
{
    auto it = m_better.constFind(statKey);
    if (it == m_better.constEnd())
        return std::nullopt;
    return *it;
}

} // namespace pts
