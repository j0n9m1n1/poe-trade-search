// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace pts {

// 원본 Checked.txt 호환: 옵션 자동 체크 목록 + 베이스 자동 ilvl 체크 목록
class CheckedStore {
public:
    struct Entry {
        QString id;  // "stat_3299347043" / "pseudo_total_life"
        QString key; // "accessory/armour/"
    };

    bool load(const QString& path);
    bool save() const;
    QString path() const { return m_path; }

    bool isChecked(const QString& statKey, const QString& topCategory) const;
    // 우클릭 토글. 반환값 = 토글 후 체크 상태
    bool toggle(const QString& statKey, const QString& topCategory, bool checked);
    void remove(int index);

    const QVector<Entry>& entries() const { return m_entries; }

    // bases: {id: "모두"/"All"/영향 이름, key: "베이스(82)/베이스2(75)/"}
    // 해당 베이스의 기준 ilvl (없으면 -1)
    int baseMinLevel(const QString& baseType, const QStringList& influenceNames) const;

private:
    QString m_path;
    QVector<Entry> m_entries;
    QVector<Entry> m_bases;
};

} // namespace pts
