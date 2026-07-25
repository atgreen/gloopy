// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

// Clip / region operations — the everyday editor moves (split, duplicate, reverse)
// on the Clip model (Source/Clip.h), exposed through the control API so scripts and
// the future editor UI share one implementation. Clips already serialise, so these
// just mutate the clips vector under the engine lock; no new persistence.

#include "MainComponent.h"
#include "NoteEdits.h"
#include <algorithm>

int MainComponent::apiSplitClip (int trackId, int index, double beat)
{
    return callOnMessageThread ([&] () -> int
    {
        Track* t = resolveTrack (trackId);
        if (t == nullptr) return -1;
        // Validate the split point before snapshotting, so a no-op split leaves the
        // undo stack untouched.
        {
            const juce::ScopedLock sl (engineLock);
            if (! juce::isPositiveAndBelow (index, (int) t->clips.size())) return -1;
            const Clip& c = t->clips[(size_t) index];
            const double local = beat - c.startBeat;
            if (local <= 0.0 || local >= c.lengthBeats) return -1;
        }
        pushUndoSnapshot();
        int newIndex = -1;
        {
            const juce::ScopedLock sl (engineLock);
            if (! juce::isPositiveAndBelow (index, (int) t->clips.size())) return -1;
            Clip& a = t->clips[(size_t) index];
            const double local = beat - a.startBeat;                 // split offset within the clip
            if (local <= 0.0 || local >= a.lengthBeats) return -1;   // split point outside the clip

            Clip b = a;                                              // copies type/audio/gain/name/etc.
            b.startBeat      = a.startBeat + local;
            b.lengthBeats    = a.lengthBeats - local;
            b.contentLenBeats = b.lengthBeats;
            b.looped         = false;
            b.name           = a.name + " (2)";
            b.notes.clear();

            std::vector<Note> aNotes;
            for (auto& n : a.notes)
            {
                if (n.startBeat < local)
                {
                    Note na = n;
                    if (na.startBeat + na.lengthBeats > local) na.lengthBeats = local - na.startBeat;  // truncate straddler
                    aNotes.push_back (na);
                }
                else
                {
                    Note nb = n; nb.startBeat = n.startBeat - local;
                    b.notes.push_back (nb);
                }
            }
            a.notes           = std::move (aNotes);
            a.lengthBeats     = local;
            a.contentLenBeats = local;
            a.looped          = false;

            t->clips.insert (t->clips.begin() + index + 1, std::move (b));
            newIndex = index + 1;
        }
        emitChange ("clip_changed", trackId);
        if (arrangeView) arrangeView->repaint();
        return newIndex;
    });
}

int MainComponent::apiDuplicateClip (int trackId, int index, double atBeat)
{
    return callOnMessageThread ([&] () -> int
    {
        pushUndoSnapshot();
        Track* t = resolveTrack (trackId);
        if (t == nullptr) return -1;
        int newIndex = -1;
        {
            const juce::ScopedLock sl (engineLock);
            if (! juce::isPositiveAndBelow (index, (int) t->clips.size())) return -1;
            Clip copy = t->clips[(size_t) index];
            copy.startBeat = atBeat >= 0.0 ? atBeat : copy.endBeat();   // default: butt up to the right
            copy.name = t->clips[(size_t) index].name + " copy";
            t->clips.push_back (std::move (copy));
            newIndex = (int) t->clips.size() - 1;
        }
        emitChange ("clip_changed", trackId);
        if (arrangeView) arrangeView->repaint();
        return newIndex;
    });
}

bool MainComponent::apiReverseClip (int trackId, int index)
{
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        Track* t = resolveTrack (trackId);
        if (t == nullptr) return false;
        bool ok = false;
        {
            const juce::ScopedLock sl (engineLock);
            if (! juce::isPositiveAndBelow (index, (int) t->clips.size())) return false;
            Clip& c = t->clips[(size_t) index];
            if (c.isAudio())
            {
                if (c.audio != nullptr)
                    for (int ch = 0; ch < c.audio->getNumChannels(); ++ch)
                        std::reverse (c.audio->getWritePointer (ch),
                                      c.audio->getWritePointer (ch) + c.audio->getNumSamples());
            }
            else
            {
                // Mirror each note within the clip's content window.
                const double span = c.contentLenBeats > 0.0 ? c.contentLenBeats : c.lengthBeats;
                for (auto& n : c.notes)
                    n.startBeat = juce::jmax (0.0, span - (n.startBeat + n.lengthBeats));
            }
            ok = true;
        }
        emitChange ("clip_changed", trackId);
        if (arrangeView) arrangeView->repaint();
        return ok;
    });
}

std::vector<Note> MainComponent::apiGetClipNotes (int trackId, int index)
{
    return callOnMessageThread ([&] () -> std::vector<Note>
    {
        const juce::ScopedLock sl (engineLock);
        Track* t = resolveTrack (trackId);
        if (t == nullptr || ! juce::isPositiveAndBelow (index, (int) t->clips.size())) return {};
        return t->clips[(size_t) index].notes;
    });
}

// Note-transform ops (piano-roll editing modes) — apply a shared NoteEdits transform
// to a MIDI clip's notes. Verifiable via GetClipNotes; the PianoRoll UI uses the same
// NoteEdits functions so the API and the editor make identical edits.
#define GLOOPY_EDIT_CLIP_NOTES(BODY)                                          \
    return callOnMessageThread ([&] () -> bool {                              \
        Track* t = resolveTrack (trackId);                                    \
        if (t == nullptr) return false;                                       \
        pushUndoSnapshot();                                                   \
        {                                                                     \
            const juce::ScopedLock sl (engineLock);                           \
            if (! juce::isPositiveAndBelow (index, (int) t->clips.size())) return false; \
            auto& c = t->clips[(size_t) index];                               \
            if (c.isAudio()) return false;                                    \
            auto& notes = c.notes; (void) notes; BODY;                        \
        }                                                                     \
        emitChange ("clip_changed", trackId);                                 \
        return true;                                                          \
    });

bool MainComponent::apiQuantizeClip (int trackId, int index, double grid)
{ GLOOPY_EDIT_CLIP_NOTES (quantizeNotes (notes, grid)) }

bool MainComponent::apiTransposeClip (int trackId, int index, int semitones)
{ GLOOPY_EDIT_CLIP_NOTES (transposeNotes (notes, semitones)) }

bool MainComponent::apiHumanizeClip (int trackId, int index, double timing, double velocity)
{ juce::Random rng; GLOOPY_EDIT_CLIP_NOTES (humanizeNotes (notes, timing, velocity, rng)) }

bool MainComponent::apiStrumClip (int trackId, int index, double stepBeats, bool down)
{ GLOOPY_EDIT_CLIP_NOTES (strumNotes (notes, stepBeats, down)) }

bool MainComponent::apiAddChord (int trackId, int index, int root, const juce::String& type,
                                 double startBeat, double lengthBeats, float velocity, int inversion)
{ GLOOPY_EDIT_CLIP_NOTES ( { auto ch = makeChord (root, type, inversion, startBeat, lengthBeats, velocity);
                             for (auto& n : ch) notes.push_back (n); } ) }

#undef GLOOPY_EDIT_CLIP_NOTES
