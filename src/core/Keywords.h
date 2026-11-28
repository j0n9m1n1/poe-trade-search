// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Types.h"

#include <QHash>
#include <QJsonValue>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace pts {

// resources/parser.json 로더. [ko, en] 키워드 테이블.
class Keywords {
public:
    struct Texts {
        QStringList t[2]; // [ko, en] 대체 표기 목록
        QString first(Lang l) const { return t[li(l)].isEmpty() ? QString() : t[li(l)].first(); }
        bool has(Lang l, const QString& s) const { return t[li(l)].contains(s); }
    };
    struct IdTexts {
        QString id;
        Texts text;
    };
    struct Influence {
        QString flag;
        Texts name;
        QString pseudo;
    };

    bool load(const QString& path, QString* error = nullptr);

    // 키워드 (property 이름 등)
    const Texts& kw(const QString& key) const;
    bool isKw(const QString& key, Lang l, const QString& s) const { return kw(key).has(l, s); }

    // 한 줄 전체가 플래그(Corrupted 등)이면 그 키, 아니면 빈 문자열
    QString flagOf(Lang l, const QString& line) const;
    const Texts& flag(const QString& key) const;

    // 첫 줄 "Item Class: ..." 의 언어 판별
    bool detectLang(const QString& firstLine, Lang* lang, QString* value) const;

    QString rarityId(Lang l, const QString& text) const;
    QString rarityText(const QString& id, Lang l) const;
    const QVector<IdTexts>& rarities() const { return m_rarity; }

    // Item Class 텍스트 -> 거래소 카테고리 id (없으면 빈 문자열)
    QString classToCategory(Game g, Lang l, const QString& text) const;
    // Item Class 텍스트 -> 같은 종류의 다른 언어 표기들 (없으면 빈 목록)
    QStringList classTexts(Game g, Lang from, const QString& text, Lang to) const;

    const QVector<Influence>& influences() const { return m_influences; }
    QString pseudoFor(const QString& statKey) const { return m_pseudoMap.value(statKey); }
    bool isResistance(const QString& statKey) const { return resistanceWeight(statKey) > 0; }
    int resistanceWeight(const QString& statKey) const;
    bool isReverse(const QString& statKey) const { return m_reverse.contains(statKey); }
    bool isDisabled(const QString& statKey) const { return m_disable.contains(statKey); }
    QString localAttackSpeed() const { return m_localAttackSpeed; }
    QString physicalDamageIncrease() const { return m_physIncr; }
    const QVector<IdTexts>& ultimatumRewards() const { return m_ultimatumRewards; }
    QString scopeOfQualifier(const QString& enQualifier) const { return m_scopes.value(enQualifier); }
    // 게임 표기와 거래소 stat 표기가 다른 줄 치환 (예: 숫자 빠진 100% 확률 옵션)
    QString alias(Lang l, const QString& line) const { return m_alias[li(l)].value(line); }

private:
    static Texts readTexts(const QJsonValue& v);

    QHash<QString, Texts> m_kw;
    QHash<QString, Texts> m_flags;
    QVector<IdTexts> m_rarity;
    QVector<IdTexts> m_classes[2];
    QVector<Influence> m_influences;
    QHash<QString, QString> m_pseudoMap;
    QSet<QString> m_resistance, m_reverse, m_disable;
    QString m_localAttackSpeed, m_physIncr;
    QVector<IdTexts> m_ultimatumRewards;
    QHash<QString, QString> m_scopes;
    QHash<QString, QString> m_alias[2];
    Texts m_empty;
};

} // namespace pts
