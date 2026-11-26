// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/TradeDb.h"

#include "core/JsonUtil.h"
#include "core/Keywords.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QtMath>

namespace pts {

namespace {

QJsonArray resultOf(const QString& path, QString* error)
{
    QJsonDocument doc;
    if (!json::readFile(path, &doc, error))
        return {};
    return doc.object().value(QStringLiteral("result")).toArray();
}

// 끝의 "(Local)" / "(특정)" 같은 한정어 분리. 숫자/# 들어간 괄호는 본문으로 취급
void splitQualifier(const QString& text, QString* body, QString* qualifier)
{
    static const QRegularExpression re(QStringLiteral("^(.*\\S)\\s*\\(([^()#0-9]+)\\)\\s*$"),
                                       QRegularExpression::DotMatchesEverythingOption);
    const auto m = re.match(text);
    if (m.hasMatch()) {
        *body = m.captured(1);
        *qualifier = m.captured(2).trimmed();
    } else {
        *body = text;
        qualifier->clear();
    }
}

} // namespace

bool TradeDb::load(const QString& dir, QString* error)
{
    *this = TradeDb();
    m_dir = dir;
    const QDir d(dir);

    // ---- stats
    const QJsonArray stats = resultOf(d.filePath(QStringLiteral("stats.json")), error);
    if (stats.isEmpty())
        return false;
    for (const auto& gv : stats) {
        const QJsonObject g = gv.toObject();
        const QString gid = g.value(QStringLiteral("id")).toString();
        m_groups.push_back({gid, g.value(QStringLiteral("label")).toString()});
        for (const auto& ev : g.value(QStringLiteral("entries")).toArray()) {
            const QJsonObject e = ev.toObject();
            StatEntry s;
            QString full = e.value(QStringLiteral("id")).toString();
            const int bar = full.indexOf(QLatin1Char('|'));
            if (bar > 0) {
                const QString opt = full.mid(bar + 1);
                bool ok = false;
                const int n = opt.toInt(&ok);
                s.option = ok ? QJsonValue(n) : QJsonValue(opt);
                s.id = full.left(bar);
            } else {
                s.id = full;
            }
            const int dot = s.id.indexOf(QLatin1Char('.'));
            s.key = dot >= 0 ? s.id.mid(dot + 1) : s.id;
            s.group = gid;
            splitQualifier(e.value(QStringLiteral("text")).toString(), &s.text, &s.qualifier);
            s.norm = text::normalizeStat(s.text);
            s.lines = s.text.count(QLatin1Char('\n')) + 1;
            s.toks = text::tokens(s.text);
            s.optionList = e.value(QStringLiteral("option")).toObject().contains(QStringLiteral("options"));

            const int idx = m_stats.size();
            m_stats.push_back(s);
            m_statById.insert(full, idx);
            if (!s.optionList) {
                m_byNorm[s.norm].push_back(idx);
                if (s.lines > 1)
                    m_multiFirst[s.norm.section(QLatin1Char('\n'), 0, 0)].push_back(idx);
            }
        }
    }

    // ---- items
    for (const auto& gv : resultOf(d.filePath(QStringLiteral("items.json")), nullptr)) {
        const QJsonObject g = gv.toObject();
        const QString gid = g.value(QStringLiteral("id")).toString();
        int order = 0;
        for (const auto& ev : g.value(QStringLiteral("entries")).toArray()) {
            const QJsonObject e = ev.toObject();
            ItemEntry it;
            it.group = gid;
            it.type = e.value(QStringLiteral("type")).toString();
            it.name = e.value(QStringLiteral("name")).toString();
            it.text = e.value(QStringLiteral("text")).toString();
            if (it.text.isEmpty())
                it.text = it.name.isEmpty() ? it.type : it.name + QLatin1Char(' ') + it.type;
            it.disc = e.value(QStringLiteral("disc")).toString();
            it.unique = e.value(QStringLiteral("flags")).toObject().value(QStringLiteral("unique")).toBool();
            it.order = order++;
            const int idx = m_items.size();
            m_items.push_back(it);
            m_itemByType[it.type].push_back(idx);
            m_itemByText[it.text].push_back(idx);
            if (!it.name.isEmpty())
                m_itemByName[it.name].push_back(idx);
            m_itemsInGroup[gid].push_back(idx);
        }
    }

    // ---- static
    for (const auto& gv : resultOf(d.filePath(QStringLiteral("static.json")), nullptr)) {
        const QJsonObject g = gv.toObject();
        const QString gid = g.value(QStringLiteral("id")).toString();
        const QString glabel = g.value(QStringLiteral("label")).toString();
        for (const auto& ev : g.value(QStringLiteral("entries")).toArray()) {
            const QJsonObject e = ev.toObject();
            StaticEntry s{e.value(QStringLiteral("id")).toString(), e.value(QStringLiteral("text")).toString(), gid,
                          glabel};
            if (s.id.isEmpty() || s.text.isEmpty())
                continue;
            const int idx = m_static.size();
            m_static.push_back(s);
            m_staticById.insert(s.id, idx);
            if (!m_staticByText.contains(s.text))
                m_staticByText.insert(s.text, idx);
        }
    }

    // ---- filters
    for (const auto& gv : resultOf(d.filePath(QStringLiteral("filters.json")), nullptr)) {
        const QJsonObject g = gv.toObject();
        const QString gid = g.value(QStringLiteral("id")).toString();
        for (const auto& fv : g.value(QStringLiteral("filters")).toArray()) {
            const QJsonObject f = fv.toObject();
            const QString fid = f.value(QStringLiteral("id")).toString();
            m_filterGroup.insert(fid, gid);
            m_filterText.insert(fid, f.value(QStringLiteral("text")).toString());
            QVector<FilterOption> opts;
            for (const auto& ov : f.value(QStringLiteral("option")).toObject().value(QStringLiteral("options")).toArray()) {
                const QJsonObject o = ov.toObject();
                const QJsonValue idv = o.value(QStringLiteral("id"));
                QString id;
                if (idv.isString())
                    id = idv.toString();
                else if (idv.isDouble())
                    id = QString::number(idv.toInt());
                opts.push_back({id, o.value(QStringLiteral("text")).toString()});
            }
            m_filterOptions.insert(fid, opts);
        }
    }

    // ---- leagues
    for (const auto& lv : resultOf(d.filePath(QStringLiteral("leagues.json")), nullptr)) {
        const QJsonObject l = lv.toObject();
        m_leagues.push_back({l.value(QStringLiteral("id")).toString(), l.value(QStringLiteral("text")).toString()});
    }

    m_loaded = !m_stats.isEmpty();
    return m_loaded;
}

void TradeDb::resolveScopes(const Keywords& kw, const TradeDb* english)
{
    for (StatEntry& s : m_stats) {
        if (s.qualifier.isEmpty())
            continue;
        QString enQ = s.qualifier;
        if (english && english != this) {
            QString full = s.id;
            if (!s.option.isUndefined() && !s.option.isNull())
                full += QLatin1Char('|') + (s.option.isDouble() ? QString::number(s.option.toInt()) : s.option.toString());
            const int i = english->findStat(full);
            if (i >= 0)
                enQ = english->stat(i).qualifier;
        }
        s.scope = kw.scopeOfQualifier(enQ);
        if (s.scope.isEmpty() && (s.qualifier == QStringLiteral("특정")))
            s.scope = QStringLiteral("local");
    }
}

bool TradeDb::hasGroup(const QString& id) const
{
    for (const auto& g : m_groups) {
        if (g.id == id)
            return true;
    }
    return false;
}

QString TradeDb::groupLabel(const QString& id) const
{
    for (const auto& g : m_groups) {
        if (g.id == id)
            return g.label;
    }
    return id;
}

bool TradeDb::verify(const StatEntry& e, const QString& raw, QVector<double>* values)
{
    const QVector<text::Token> t = text::tokens(raw);
    if (t.size() != e.toks.size())
        return false;
    values->clear();
    for (int i = 0; i < t.size(); ++i) {
        if (e.toks[i].hash)
            values->push_back(t[i].value);
        else if (qAbs(qAbs(t[i].value) - qAbs(e.toks[i].value)) > 1e-6)
            return false;
    }
    return true;
}

QVector<StatMatch> TradeDb::match(const QStringList& lines, int start, int* consumed) const
{
    *consumed = 0;
    QVector<StatMatch> out;
    if (start < 0 || start >= lines.size())
        return out;

    const QString n0 = text::normalizeStat(lines[start]);
    int bestN = 1;

    auto mit = m_multiFirst.constFind(n0);
    if (mit != m_multiFirst.constEnd()) {
        for (int idx : *mit) {
            const StatEntry& e = m_stats[idx];
            if (start + e.lines > lines.size() || e.lines < bestN)
                continue;
            const QString joined = lines.mid(start, e.lines).join(QLatin1Char('\n'));
            if (text::normalizeStat(joined) != e.norm)
                continue;
            QVector<double> vals;
            if (!verify(e, joined, &vals))
                continue;
            if (e.lines > bestN) {
                out.clear();
                bestN = e.lines;
            }
            out.push_back({idx, vals});
        }
    }

    if (out.isEmpty()) {
        bestN = 1;
        auto it = m_byNorm.constFind(n0);
        if (it != m_byNorm.constEnd()) {
            for (int idx : *it) {
                QVector<double> vals;
                if (verify(m_stats[idx], lines[start], &vals))
                    out.push_back({idx, vals});
            }
        }
    }

    if (!out.isEmpty())
        *consumed = bestN;
    return out;
}

int TradeDb::itemAt(const QString& group, int order) const
{
    const QVector<int> v = m_itemsInGroup.value(group);
    return order >= 0 && order < v.size() ? v[order] : -1;
}

const StaticEntry* TradeDb::staticById(const QString& id) const
{
    auto it = m_staticById.constFind(id);
    return it == m_staticById.constEnd() ? nullptr : &m_static[*it];
}

const StaticEntry* TradeDb::staticByText(const QString& text) const
{
    auto it = m_staticByText.constFind(text);
    return it == m_staticByText.constEnd() ? nullptr : &m_static[*it];
}

bool GameDb::translateItem(Lang from, Lang to, const QString& type, const QString& name, QString* outType,
                           QString* outName) const
{
    if (from == to) {
        *outType = type;
        *outName = name;
        return true;
    }
    const TradeDb& a = (*this)[from];
    const TradeDb& b = (*this)[to];
    if (!a.isLoaded() || !b.isLoaded())
        return false;

    int src = -1;
    if (!name.isEmpty()) {
        for (int i : a.itemsByName(name)) {
            if (a.item(i).type == type) {
                src = i;
                break;
            }
        }
    }
    if (src < 0) {
        const QVector<int> v = a.itemsByType(type);
        for (int i : v) {
            if (a.item(i).name.isEmpty()) {
                src = i;
                break;
            }
        }
        if (src < 0 && !v.isEmpty())
            src = v.first();
    }
    if (src < 0)
        return false;

    const ItemEntry& s = a.item(src);
    if (a.groupItemCount(s.group) != b.groupItemCount(s.group))
        return false; // 순번이 어긋날 수 있어서 포기
    const int dst = b.itemAt(s.group, s.order);
    if (dst < 0)
        return false;
    const ItemEntry& d = b.item(dst);
    if (d.unique != s.unique || d.disc != s.disc)
        return false;
    *outType = d.type;
    *outName = name.isEmpty() ? QString() : d.name;
    return true;
}

} // namespace pts
