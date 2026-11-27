// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Types.h"

#include <cmath>

namespace pts {

inline double filterValueStep(OptDouble current, OptDouble rowValue, int notches, bool shift)
{
    const double value = current.value_or(rowValue.value_or(0.0));
    const bool fractional = value != std::trunc(value) || (rowValue && *rowValue != std::trunc(*rowValue));
    return value + notches * (shift ? 10.0 : 1.0) * (fractional ? 0.1 : 1.0);
}

} // namespace pts
