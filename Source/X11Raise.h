// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <vector>

// Minimal window-manager helper for the external-instrument epic: raise a standalone app's window
// to the front when we launch it (under XWayland an X11 app spawned by another X11 app often opens
// BEHIND the parent without stealing focus). Implemented with EWMH (_NET_CLIENT_LIST diff +
// _NET_ACTIVE_WINDOW), which Mutter/GNOME and other EWMH WMs honour — no dependency on external
// tools (xdotool/wmctrl). This header is deliberately JUCE- and X11-free so it can be included in a
// JUCE translation unit; the X11 calls (and their macro pollution: None/Bool/KeyPress/…) are
// confined to X11Raise.cpp. All functions no-op on non-Linux / when no X display is reachable.
namespace gloopyx
{
    /** The WM's list of managed top-level client windows (EWMH _NET_CLIENT_LIST). Empty if
        unavailable. Snapshot before launch, diff after, to find the app's new window(s). */
    std::vector<unsigned long> listClients();

    /** Ask the WM to activate + raise the given client window (EWMH _NET_ACTIVE_WINDOW, plus a
        raise as a fallback). No-op if there's no X display or the WM ignores it. */
    void activateWindow (unsigned long window);
}
