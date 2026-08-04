// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>

#if JUCE_LINUX
 #include <unistd.h>
 #include <climits>
#endif

/** Where is the running executable, really?

    JUCE's `File::currentExecutableFile` resolves argv[0] (via dladdr's dli_fname) against the
    current working directory. Launch Gloopy by a bare name found on $PATH — e.g. typing
    `gloopy` — and argv[0] is just "gloopy", so JUCE reports <cwd>/gloopy instead of the real
    /usr/bin/gloopy. Every `<exeDir>/../share/gloopy/...` data lookup then resolves against the
    wrong directory and misses, so demos and presets come up empty unless you happen to launch
    from the install prefix. On Linux read /proc/self/exe — what the kernel actually exec'd,
    independent of argv[0] and the cwd; fall back to JUCE on failure and on platforms where its
    own resolution is already correct (macOS/Windows). */
namespace gloopy
{
    inline juce::File executableFile()
    {
       #if JUCE_LINUX
        char buf[PATH_MAX + 1];
        const auto n = ::readlink ("/proc/self/exe", buf, sizeof (buf) - 1);
        if (n > 0) { buf[(size_t) n] = '\0'; return juce::File (juce::CharPointer_UTF8 (buf)); }
       #endif
        return juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    }

    inline juce::File executableDir() { return executableFile().getParentDirectory(); }
}
