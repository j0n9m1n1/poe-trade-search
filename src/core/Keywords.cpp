// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Keywords.h"

#include "core/JsonUtil.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

namespace pts {

Keywords::Texts Keywords::readTexts(const QJsonValue& v)
{
    Texts t;
    const QJsonArray a = v.toArray();
    for (int i = 0; i < 2 && i < a.size(); ++i) {
        if (a[i].isArray()) {
            for (const auto& x : a[i].toArray())
                t.t[i] << x.toString();
        } else if (a[i].isString()) {
            t.t[i] << a[i].toString();
        }
    }
    return t;
}

bool Keywords::load(const QString& path, QString* error)
{
    QJsonDocument doc;
    if (!json::readFile(path, &doc, error))
        return false;
    const QJsonObject root = doc.object();

    m_kw.clear();
    const QJsonObject kw = root.value(QStringLiteral("keywords")).toObject();
    for (auto it = kw.begin(); it != kw.end(); ++it)
        m_kw.insert(it.key(), readTexts(it.value()));

    m_flags.clear();
    const QJsonObject fl = root.value(QStringLiteral("flags")).toObject();
    for (auto it = fl.begin(); it != fl.end(); ++it)
        m_flags.insert(it.key(), readTexts(it.value()));

    auto readIdTexts = [](const QJsonArray& a) {
        QVector<IdTexts> out;
        for (const auto& v : a) {
            const QJsonObject o = v.toObject();
            out.push_back({o.value(QStringLiteral("id")).toString(), readTexts(o.value(QStringLiteral("text")))});
        }
        return out;
    };

    m_rarity = readIdTexts(root.value(QStringLiteral("rarity")).toArray());
    const QJsonObject classes = root.value(QStringLiteral("item_classes")).toObject();
    m_classes[gi(Game::Poe1)] = readIdTexts(classes.value(QStringLiteral("poe1")).toArray());
    m_classes[gi(Game::Poe2)] = readIdTexts(classes.value(QStringLiteral("poe2")).toArray());
    m_ultimatumRewards = readIdTexts(root.value(QStringLiteral("ultimatum_rewards")).toArray());

    m_influences.clear();
    for (const auto& v : root.value(QStringLiteral("influences")).toArray()) {
        const QJsonObject o = v.toObject();
        m_influences.push_back({o.value(QStringLiteral("flag")).toString(), readTexts(o.value(QStringLiteral("name"))),
                                o.value(QStringLiteral("pseudo")).toString()});
    }

    m_pseudoMap.clear();
    const QJsonObject pm = root.value(QStringLiteral("pseudo_map")).toObject();
    for (auto it = pm.begin(); it != pm.end(); ++it)
        m_pseudoMap.insert(it.key(), it.value().toString());

    auto readSet = [&](const char* key) {
        QSet<QString> s;
        for (const auto& v : root.value(QLatin1String(key)).toArray())
            s.insert(v.toString());
        return s;
    };
    m_resistance = readSet("resistance");
    m_reverse = readSet("reverse");
    m_disable = readSet("disable");

    m_localAttackSpeed = root.value(QStringLiteral("local_attack_speed")).toString();
    m_physIncr = root.value(QStringLiteral("physical_damage_increase")).toString();

    m_scopes.clear();
    const QJsonObject sc = root.value(QStringLiteral("scope_qualifiers")).toObject();
    for (auto it = sc.begin(); it != sc.end(); ++it)
        m_scopes.insert(it.key(), it.value().toString());

    for (Lang l : {Lang::Ko, Lang::En}) {
        m_alias[li(l)].clear();
        const QJsonObject al = root.value(QStringLiteral("aliases")).toObject().value(langKey(l)).toObject();
        for (auto it = al.begin(); it != al.end(); ++it)
            m_alias[li(l)].insert(it.key(), it.value().toString());
    }

    return !m_kw.isEmpty();
}

const Keywords::Texts& Keywords::kw(const QString& key) const
{
    auto it = m_kw.constFind(key);
    return it == m_kw.constEnd() ? m_empty : it.value();
}

const Keywords::Texts& Keywords::flag(const QString& key) const
{
    auto it = m_flags.constFind(key);
    return it == m_flags.constEnd() ? m_empty : it.value();
}

QString Keywords::flagOf(Lang l, const QString& line) const
{
    for (auto it = m_flags.constBegin(); it != m_flags.constEnd(); ++it) {
        if (it.value().has(l, line))
            return it.key();
    }
    return {};
}

bool Keywords::detectLang(const QString& firstLine, Lang* lang, QString* value) const
{
    const Texts& t = kw(QStringLiteral("item_class"));
    for (Lang l : {Lang::Ko, Lang::En}) {
        for (const QString& k : t.t[li(l)]) {
            const QString prefix = k + QStringLiteral(": ");
            if (firstLine.startsWith(prefix)) {
                if (lang)
                    *lang = l;
                if (value)
                    *value = firstLine.mid(prefix.size()).trimmed();
                return true;
            }
        }
    }
    return false;
}

QString Keywords::rarityId(Lang l, const QString& text) const
{
    for (const auto& r : m_rarity) {
        if (r.text.has(l, text))
            return r.id;
    }
    return {};
}

QString Keywords::rarityText(const QString& id, Lang l) const
{
    for (const auto& r : m_rarity) {
        if (r.id == id)
            return r.text.first(l);
    }
    return id;
}

QString Keywords::classToCategory(Game g, Lang l, const QString& text) const
{
    for (const auto& c : m_classes[gi(g)]) {
        if (c.text.has(l, text))
            return c.id;
    }
    return {};
}

QStringList Keywords::classTexts(Game g, Lang from, const QString& text, Lang to) const
{
    for (const auto& c : m_classes[gi(g)]) {
        if (c.text.has(from, text))
            return c.text.t[li(to)];
    }
    return {};
}

int Keywords::resistanceWeight(const QString& statKey) const
{
    static const QHash<QString, int> weights = {
        {QStringLiteral("stat_2915988346"), 2}, {QStringLiteral("stat_4277795662"), 2},
        {QStringLiteral("stat_3441501978"), 2}, {QStringLiteral("stat_378817135"), 2},
        {QStringLiteral("stat_3465022881"), 2}, {QStringLiteral("stat_3393628375"), 2},
        {QStringLiteral("stat_2901986750"), 3},
        {QStringLiteral("stat_2016723660"), 4}, {QStringLiteral("stat_3128852541"), 4},
    };
    return weights.value(statKey, m_resistance.contains(statKey) ? 1 : 0);
}

} // namespace pts
