// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>

// Classify a dropped file by what Gloopy should do with it, so the drag-and-drop
// handler is a thin router over the already-tested load ops (openAny / apiImportMidi
// / apiImportAudio). Kept as a pure function in its own header so it is unit-testable
// without the GUI. Extension check first (string-only); the directory probe (a
// composition folder) is last so the common cases never touch the filesystem.
enum class DroppedFileKind { Unsupported, Project, Midi, Audio };

inline DroppedFileKind classifyDroppedFile (const juce::File& f)
{
    const auto ext = f.getFileExtension().toLowerCase();
    if (ext == ".gloopy" || ext == ".zip")                       return DroppedFileKind::Project;
    if (ext == ".mid" || ext == ".midi")                         return DroppedFileKind::Midi;
    if (ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac")
                                                                 return DroppedFileKind::Audio;
    if (ext.isEmpty() && f.isDirectory())                        return DroppedFileKind::Project;  // composition dir
    return DroppedFileKind::Unsupported;
}
