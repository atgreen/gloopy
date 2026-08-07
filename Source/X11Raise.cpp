// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#include "X11Raise.h"

// This TU is intentionally JUCE-free: Xlib defines macros (None, Bool, Status, KeyPress, …) that
// collide with JUCE identifiers, so all X11 use is quarantined here behind the plain-C++ API in
// X11Raise.h. Linux-only; a no-op stub elsewhere.
#if defined(__linux__)

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <cstring>

namespace gloopyx
{

std::vector<unsigned long> listClients()
{
    std::vector<unsigned long> out;
    Display* d = XOpenDisplay (nullptr);
    if (d == nullptr) return out;                       // no X display (pure Wayland / headless)

    const Atom prop = XInternAtom (d, "_NET_CLIENT_LIST", True);
    if (prop != None)
    {
        Atom type = None; int format = 0;
        unsigned long count = 0, after = 0; unsigned char* data = nullptr;
        if (XGetWindowProperty (d, DefaultRootWindow (d), prop, 0, 4096, False, XA_WINDOW,
                                &type, &format, &count, &after, &data) == Success && data != nullptr)
        {
            const Window* wins = reinterpret_cast<const Window*> (data);
            out.reserve (count);
            for (unsigned long i = 0; i < count; ++i) out.push_back ((unsigned long) wins[i]);
            XFree (data);
        }
    }
    XCloseDisplay (d);
    return out;
}

void activateWindow (unsigned long window)
{
    Display* d = XOpenDisplay (nullptr);
    if (d == nullptr) return;

    const Window root  = DefaultRootWindow (d);
    const Atom   activ = XInternAtom (d, "_NET_ACTIVE_WINDOW", True);
    if (activ != None)
    {
        XEvent e; std::memset (&e, 0, sizeof e);
        e.xclient.type         = ClientMessage;
        e.xclient.window       = (Window) window;
        e.xclient.message_type = activ;
        e.xclient.format       = 32;
        e.xclient.data.l[0]    = 2;                     // source indication: 2 = direct user action (WMs trust it)
        e.xclient.data.l[1]    = CurrentTime;
        XSendEvent (d, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
    }
    XRaiseWindow (d, (Window) window);                  // fallback for non-EWMH WMs
    XFlush (d);
    XCloseDisplay (d);
}

} // namespace gloopyx

#else  // non-Linux: no X11

namespace gloopyx
{
    std::vector<unsigned long> listClients() { return {}; }
    void activateWindow (unsigned long) {}
}

#endif
