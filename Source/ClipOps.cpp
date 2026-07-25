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

// Crop a MIDI clip to the absolute beat range [startBeat, endBeat): the clip moves/shrinks
// to the intersection, and notes overlapping the window are kept (onset/length clamped into
// it). MIDI clips only for now (audio crop needs buffer trimming). false on empty/no-op.
bool MainComponent::apiCropClip (int trackId, int index, double startBeat, double endBeat)
{
    return callOnMessageThread ([&] () -> bool
    {
        Track* t = resolveTrack (trackId);
        if (t == nullptr) return false;
        pushUndoSnapshot();
        bool ok = false;
        {
            const juce::ScopedLock sl (engineLock);
            if (! juce::isPositiveAndBelow (index, (int) t->clips.size())) return false;
            Clip& c = t->clips[(size_t) index];
            const double clipEnd = c.startBeat + c.lengthBeats;
            const double s = juce::jmax (startBeat, c.startBeat);
            const double e = juce::jmin (endBeat,   clipEnd);
            if (e - s < 1.0e-6) return false;                    // empty intersection
            const double newLen = e - s;

            if (c.isAudio())
            {
                if (c.audio == nullptr) return false;
                // Audio plays at natural speed, so map the [s,e) beat window to
                // source-sample offsets via wall-clock seconds (tempo-map aware) and
                // trim the buffer to that sub-range.
                const double secStart = apiBeatsToSeconds (c.startBeat);
                const juce::int64 headSrc = (juce::int64) std::llround ((apiBeatsToSeconds (s) - secStart) * c.audioSourceRate);
                const juce::int64 tailSrc = (juce::int64) std::llround ((apiBeatsToSeconds (e) - secStart) * c.audioSourceRate);
                const int frames = c.audio->getNumSamples();
                const juce::int64 a0 = juce::jlimit ((juce::int64) 0, (juce::int64) frames, headSrc);
                const juce::int64 a1 = juce::jlimit (a0, (juce::int64) frames, tailSrc);
                const int newFrames = (int) (a1 - a0);
                if (newFrames <= 0) return false;
                const int nch = c.audio->getNumChannels();
                auto trimmed = std::make_shared<juce::AudioBuffer<float>> (nch, newFrames);
                for (int ch = 0; ch < nch; ++ch)
                    trimmed->copyFrom (ch, 0, *c.audio, ch, (int) a0, newFrames);
                c.audio           = trimmed;
                c.peaks           = std::make_shared<std::vector<float>> (buildPeaks (*trimmed));
                c.startBeat       = s;
                c.lengthBeats     = newLen;
                c.contentLenBeats = newLen;
                c.fadeInBeats     = 0.0;   // old fade edges no longer align to the new clip bounds
                c.fadeOutBeats    = 0.0;
                c.audioFile       = {};    // trimmed buffer no longer matches the source file — embed it
                c.takeId          = {};
                ok = true;
            }
            else
            {
                const double head = s - c.startBeat;             // clip-relative beats dropped from the front
                std::vector<Note> kept;
                for (auto& n : c.notes)
                {
                    const double onsetRel = n.startBeat - head;
                    const double endRel   = onsetRel + n.lengthBeats;
                    if (endRel <= 0.0 || onsetRel >= newLen) continue;   // note doesn't sound in the window
                    Note nn = n;
                    nn.startBeat   = juce::jmax (0.0, onsetRel);
                    nn.lengthBeats = juce::jmax (0.0625, juce::jmin (newLen, endRel) - nn.startBeat);
                    kept.push_back (nn);
                }
                c.notes           = std::move (kept);
                c.startBeat       = s;
                c.lengthBeats     = newLen;
                c.contentLenBeats = newLen;
                c.looped          = false;
                ok = true;
            }
        }
        emitChange ("clip_changed", trackId);
        if (arrangeView) arrangeView->repaint();
        return ok;
    });
}

// Set an audio clip's playback gain (dB). Audio clips only — MIDI dynamics are per-note
// velocity. false if the clip isn't found or isn't audio.
bool MainComponent::apiSetClipGain (int trackId, int index, float gainDb)
{
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        Track* t = resolveTrack (trackId);
        if (t == nullptr) return false;
        {
            const juce::ScopedLock sl (engineLock);
            if (! juce::isPositiveAndBelow (index, (int) t->clips.size())) return false;
            Clip& c = t->clips[(size_t) index];
            if (! c.isAudio()) return false;
            c.audioGain = juce::Decibels::decibelsToGain (gainDb);
        }
        emitChange ("clip_changed", trackId);
        if (arrangeView) arrangeView->repaint();
        return true;
    });
}

// Normalize an audio clip: set its gain so the loudest sample sits at targetDbfs.
// Returns the applied linear gain, or -1 on error (not audio / silent clip).
float MainComponent::apiNormalizeClip (int trackId, int index, float targetDbfs)
{
    return callOnMessageThread ([&] () -> float
    {
        pushUndoSnapshot();
        Track* t = resolveTrack (trackId);
        if (t == nullptr) return -1.0f;
        float applied = -1.0f;
        {
            const juce::ScopedLock sl (engineLock);
            if (! juce::isPositiveAndBelow (index, (int) t->clips.size())) return -1.0f;
            Clip& c = t->clips[(size_t) index];
            if (! c.isAudio() || c.audio == nullptr) return -1.0f;
            float peak = 0.0f;
            for (int ch = 0; ch < c.audio->getNumChannels(); ++ch)
                peak = juce::jmax (peak, c.audio->getMagnitude (ch, 0, c.audio->getNumSamples()));
            if (peak <= 0.0f) return -1.0f;                     // silent clip — nothing to normalize
            applied = juce::Decibels::decibelsToGain (targetDbfs) / peak;
            c.audioGain = applied;
        }
        emitChange ("clip_changed", trackId);
        if (arrangeView) arrangeView->repaint();
        return applied;
    });
}

// Set an audio clip's linear fade-in / fade-out lengths (beats). false if not audio.
bool MainComponent::apiSetClipFades (int trackId, int index, double fadeInBeats, double fadeOutBeats)
{
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        Track* t = resolveTrack (trackId);
        if (t == nullptr) return false;
        {
            const juce::ScopedLock sl (engineLock);
            if (! juce::isPositiveAndBelow (index, (int) t->clips.size())) return false;
            Clip& c = t->clips[(size_t) index];
            if (! c.isAudio()) return false;
            c.fadeInBeats  = juce::jmax (0.0, fadeInBeats);
            c.fadeOutBeats = juce::jmax (0.0, fadeOutBeats);
        }
        emitChange ("clip_changed", trackId);
        if (arrangeView) arrangeView->repaint();
        return true;
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

bool MainComponent::apiArpeggiateClip (int trackId, int index, double stepBeats, int mode)
{ GLOOPY_EDIT_CLIP_NOTES (arpeggiateNotes (notes, stepBeats, mode)) }

// Recompute a track's live-arp expansion for all its MIDI clips. Caller holds engineLock.
void MainComponent::applyArpToTrack (Track& t)
{
    for (auto& c : t.clips)
    {
        if (t.arp.enabled && c.type == ClipType::Midi)
        {
            const double len = c.contentLenBeats > 0.0 ? c.contentLenBeats : c.lengthBeats;
            c.arpNotes = expandArp (c.notes, t.arp.rate, t.arp.octaves, t.arp.gate, t.arp.mode,
                                    t.arp.swing, t.arp.hold, len);
        }
        else
            c.arpNotes.clear();
    }
}

bool MainComponent::apiSetTrackArp (int trackId, bool enabled, double rate, int octaves, float gate, int mode,
                                   float swing, bool hold)
{
    return callOnMessageThread ([&] () -> bool
    {
        Track* t = resolveTrack (trackId);
        if (t == nullptr) return false;
        pushUndoSnapshot();
        {
            const juce::ScopedLock sl (engineLock);
            t->arp.enabled = enabled;
            t->arp.rate    = juce::jmax (0.03125, rate);
            t->arp.octaves = juce::jlimit (1, 6, octaves);
            t->arp.gate    = juce::jlimit (0.05f, 1.0f, gate);
            t->arp.mode    = juce::jlimit (0, 3, mode);
            t->arp.swing   = juce::jlimit (0.0f, 0.9f, swing);
            t->arp.hold    = hold;
            applyArpToTrack (*t);
        }
        emitChange ("track_arp", trackId);
        return true;
    });
}

bool MainComponent::apiGetTrackArp (int trackId, bool& enabled, double& rate, int& octaves, float& gate, int& mode,
                                   float& swing, bool& hold)
{
    return callOnMessageThread ([&] () -> bool
    {
        const juce::ScopedLock sl (engineLock);
        Track* t = resolveTrack (trackId);
        if (t == nullptr) return false;
        enabled = t->arp.enabled; rate = t->arp.rate; octaves = t->arp.octaves;
        gate = t->arp.gate; mode = t->arp.mode; swing = t->arp.swing; hold = t->arp.hold;
        return true;
    });
}

bool MainComponent::apiAddChord (int trackId, int index, int root, const juce::String& type,
                                 double startBeat, double lengthBeats, float velocity, int inversion)
{ GLOOPY_EDIT_CLIP_NOTES ( { auto ch = makeChord (root, type, inversion, startBeat, lengthBeats, velocity);
                             for (auto& n : ch) notes.push_back (n); } ) }

#undef GLOOPY_EDIT_CLIP_NOTES
