// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/CheckedStore.h"

#include "core/JsonUtil.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

namespace pts {

namespace {
QVector<CheckedStore::Entry> readEntries(const QJsonArray& a)
{
    QVector<CheckedStore::Entry> out;
    for (const auto& v : a) {
        const QJsonObject o = v.toObject();
        out.push_back({o.value(QStringLiteral("id")).toString(), o.value(QStringLiteral("key")).toString()});
    }
    return out;
}

QJsonArray writeEntries(const QVector<CheckedStore::Entry>& v)
{
    QJsonArray a;
    for (const auto& e : v)
        a.append(QJsonObject{{QStringLiteral("id"), e.id}, {QStringLiteral("key"), e.key}});
    return a;
}
} // namespace

bool CheckedStore::load(const QString& path)
{
    m_path = path;
    m_entries.clear();
    m_bases.clear();
    QJsonDocument doc;
    if (!json::readFile(path, &doc))
        return false;
    const QJsonObject o = doc.object();
    m_entries = readEntries(o.value(QStringLiteral("entries")).toArray());
    m_bases = readEntries(o.value(QStringLiteral("bases")).toArray());
    return true;
}

bool CheckedStore::save() const
{
    if (m_path.isEmpty())
        return false;
    QJsonObject o;
    o.insert(QStringLiteral("entries"), writeEntries(m_entries));
    o.insert(QStringLiteral("bases"), writeEntries(m_bases));
    return json::writeFile(m_path, QJsonDocument(o), true, true);
}

bool CheckedStore::isChecked(const QString& statKey, const QString& topCategory) const
{
    if (statKey.isEmpty() || topCategory.isEmpty())
        return false;
    for (const auto& e : m_entries) {
        if (e.id == statKey && e.key.split(QLatin1Char('/'), Qt::SkipEmptyParts).contains(topCategory))
            return true;
    }
    return false;
}

bool CheckedStore::toggle(const QString& statKey, const QString& topCategory, bool checked)
{
    if (statKey.isEmpty() || topCategory.isEmpty())
        return false;
    int idx = -1;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].id == statKey) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        if (checked)
            m_entries.push_back({statKey, topCategory + QLatin1Char('/')});
        return checked;
    }
    QStringList keys = m_entries[idx].key.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    keys.removeAll(topCategory);
    if (checked)
        keys << topCategory;
    if (keys.isEmpty())
        m_entries.removeAt(idx);
    else
        m_entries[idx].key = keys.join(QLatin1Char('/')) + QLatin1Char('/');
    return checked;
}

void CheckedStore::remove(int index)
{
    if (index >= 0 && index < m_entries.size())
        m_entries.removeAt(index);
}

int CheckedStore::baseMinLevel(const QString& baseType, const QStringList& influenceNames) const
{
    if (baseType.isEmpty())
        return -1;
    const QRegularExpression re(QRegularExpression::escape(baseType) + QStringLiteral("\\(([0-9]+)\\)/"));
    int best = -1;
    for (const auto& b : m_bases) {
        const bool any = b.id == QStringLiteral("모두") || b.id.compare(QStringLiteral("all"), Qt::CaseInsensitive) == 0;
        if (!any && !influenceNames.contains(b.id))
            continue;
        const auto m = re.match(b.key);
        if (m.hasMatch()) {
            const int lv = m.captured(1).toInt();
            if (best < 0 || lv < best)
                best = lv;
        }
    }
    return best;
}

} // namespace pts
