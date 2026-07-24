// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

/** One block on the playlist: an instance of a pattern placed on a track at a
    time. If lengthBeats exceeds the pattern length, the pattern repeats to fill
    the clip (like dragging a clip wider in FL). Time is in beats. */
struct PlaylistClip
{
    int    patternIndex { 0 };
    int    track        { 0 };
    double startBeat    { 0.0 };
    double lengthBeats  { 4.0 };
};
