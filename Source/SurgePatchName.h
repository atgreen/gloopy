// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <string>

/** The current patch name of a hosted **Surge XT** plugin, or "" if it isn't Surge / can't be
    read. The user loads patches from Surge's own browser, and there is no clean host API for the
    live patch name, so we read it out of the plugin state: Surge's LV2 state serialises the patch
    as a `juce::MemoryBlock` base64 ("<size>.<data>") in a `StateString` property — decode that to
    the raw Surge patch bytes, which carry the cleartext `<meta name="...">` XML.

    Call on the **message thread only** — `getStateInformation` is not real-time safe, and at ~78 KB
    it is far too heavy for a paint loop, so callers must cache the result and refresh it sparingly. */
inline juce::String surgePatchName (juce::AudioProcessor& p)
{
    if (! p.getName().containsIgnoreCase ("Surge XT"))
        return {};

    juce::MemoryBlock state;
    p.getStateInformation (state);
    const std::string turtle (static_cast<const char*> (state.getData()), state.getSize());

    const std::string key = "StateString> \"";
    const auto k = turtle.find (key);
    if (k == std::string::npos) return {};
    const auto s = k + key.size();
    const auto e = turtle.find ('"', s);
    if (e == std::string::npos) return {};

    juce::MemoryBlock patch;                                  // "<size>.<base64>" -> raw patch bytes
    if (! patch.fromBase64Encoding (juce::String (turtle.substr (s, e - s))))
        return {};

    // Search the raw bytes (std::string survives the binary patch_header's embedded NULs) for the
    // <meta name="..."> attribute Surge writes for the patch name.
    const std::string raw (static_cast<const char*> (patch.getData()), patch.getSize());
    const auto mi = raw.find ("<meta ");
    if (mi == std::string::npos) return {};
    const auto ni = raw.find ("name=\"", mi);
    if (ni == std::string::npos) return {};
    const auto ns = ni + 6;
    const auto ne = raw.find ('"', ns);
    if (ne == std::string::npos) return {};

    return juce::String (juce::CharPointer_UTF8 (raw.substr (ns, ne - ns).c_str()));
}
