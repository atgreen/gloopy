// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cctype>

// ParamModel id helpers that run on the AUDIO THREAD (applyParamValue is called per
// automation/modulation lane every block), so they must not allocate (principle 4).
// `ieq` is an ASCII case-insensitive compare of a NUL-terminated C string against a literal —
// the alloc-free replacement for `name.toLowerCase() == "..."`. The id itself is parsed by
// splitting a copy on '/' in a stack buffer (see MainComponent::applyParamValue).
namespace gloopy
{
    inline bool ieq (const char* a, const char* b) noexcept
    {
        for (;; ++a, ++b)
        {
            const int ca = std::tolower (static_cast<unsigned char> (*a));
            const int cb = std::tolower (static_cast<unsigned char> (*b));
            if (ca != cb) return false;
            if (ca == 0)  return true;   // both hit NUL together
        }
    }
}
