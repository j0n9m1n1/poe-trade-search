// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Types.h"

#include <QList>
#include <algorithm>
#include <utility>

namespace pts {

class ItemHistory {
public:
    struct Entry {
        Game game;
        QString text;
    };

    void record(Game game, QString text)
    {
        const auto previous = std::find_if(m_entries.cbegin(), m_entries.cend(), [game, &text](const Entry& entry) {
            return entry.game == game && entry.text == text;
        });
        if (previous != m_entries.cend())
            m_entries.erase(previous);
        m_entries.append({game, std::move(text)});
        if (m_entries.size() > 10)
            m_entries.removeFirst();
        m_current = m_entries.size() - 1;
    }

    bool canStep(int offset) const
    {
        const qsizetype next = m_current + offset;
        return next >= 0 && next < m_entries.size();
    }

    const Entry* step(int offset)
    {
        if (!canStep(offset))
            return nullptr;
        m_current += offset;
        return &m_entries.at(m_current);
    }

private:
    QList<Entry> m_entries;
    qsizetype m_current = -1;
};

} // namespace pts
