// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <optional>

namespace pts {

enum class Game { Poe1 = 0, Poe2 = 1 };
enum class Lang { Ko = 0, En = 1 };

inline int li(Lang l) { return static_cast<int>(l); }
inline int gi(Game g) { return static_cast<int>(g); }

inline QString gameKey(Game g) { return g == Game::Poe1 ? QStringLiteral("poe1") : QStringLiteral("poe2"); }
inline QString langKey(Lang l) { return l == Lang::Ko ? QStringLiteral("ko") : QStringLiteral("en"); }
inline Lang otherLang(Lang l) { return l == Lang::Ko ? Lang::En : Lang::Ko; }

using OptDouble = std::optional<double>;

} // namespace pts
