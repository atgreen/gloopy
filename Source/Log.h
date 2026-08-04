// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <sstream>
#include <iostream>

/** Tiny leveled logger. Verbosity is set once from the command line (-v, -vv, -vvv…)
    and read globally; GLOG(n) emits one line to stderr only when the current level is
    >= n. stderr (not stdout) so the headless commands' JSON output stays clean.

    Level 0 = silent (default). 1 = -v (headline resolution / lifecycle). 2 = -vv
    (per-candidate filepath probing). 3 = -vvv (fine detail). */
namespace gloopy
{
    inline std::atomic<int>& verbosityRef() { static std::atomic<int> v { 0 }; return v; }
    inline void setVerbosity (int level)   { verbosityRef().store (juce::jmax (0, level)); }
    inline int  verbosity()                { return verbosityRef().load(); }

    // RAII: accumulates a message and flushes a single line to stderr when destroyed,
    // but only if `active` (verbosity >= the requested level). Inactive lines build nothing.
    struct LogLine
    {
        bool active;
        std::ostringstream buf;
        explicit LogLine (bool a) : active (a) {}
        ~LogLine() { if (active) std::cerr << buf.str() << std::endl; }
        template <typename T>
        LogLine& operator<< (const T& v) { if (active) buf << v; return *this; }
    };
}

// GLOG(n) << "…" << path;  — logs only when the -v level is >= n.
#define GLOG(level) ::gloopy::LogLine (::gloopy::verbosity() >= (level)) << "[gloopy] "
