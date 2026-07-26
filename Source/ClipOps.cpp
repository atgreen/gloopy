// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

// Clip / region operations — the everyday editor moves (split, duplicate, reverse)
// on the Clip model (Source/Clip.h), exposed through the control API so scripts and
// the future editor UI share one implementation. Clips already serialise, so these
// just mutate the clips vector under the engine lock; no new persistence.

#include "MainComponent.h"
#include "NoteEdits.h"
#include "NotesJson.h"
#include "Onsets.h"
#include <algorithm>

// Split a clip at a named timeline location (marker/section/range start). Resolves the
// marker's beat from `locations`, then delegates to apiSplitClip (which no-ops if the
// beat falls outside the clip). Returns the new right-clip index, or -1.
int MainComponent::apiSplitClipAtMarker (int trackId, int index, const juce::String& marker)
{
    return callOnMessageThread ([&] () -> int
    {
        double beat = -1.0;
        {
            const juce::ScopedLock sl (engineLock);
            for (auto& l : locations)
                if (l.name == marker) { beat = l.startBeat; break; }
        }
        if (beat < 0.0) return -1;                    // no such marker
        return apiSplitClip (trackId, index, beat);   // re-enters on the message thread; no-op if outside the clip
    });
}

// Slice an audio clip at its transients: detect onsets in the clip's buffer, then split
// the clip at each onset beat (audio split now trims each half, so the slices play back
// seamlessly). `sensitivity` scales the detection threshold (higher => fewer slices).
// Returns the resulting slice count (1 = no onsets found), or -1 on error.
int MainComponent::apiSliceClipAtTransients (int trackId, int index, float sensitivity)
{
    return callOnMessageThread ([&] () -> int
    {
        // Snapshot the clip's mono buffer + anchors under the lock; detect off-lock.
        std::vector<float> mono;
        double srcRate = 44100.0, clipStart = 0.0, clipLenBeats = 0.0;
        {
            const juce::ScopedLock sl (engineLock);
            Track* t = resolveTrack (trackId);
            if (t == nullptr || ! juce::isPositiveAndBelow (index, (int) t->clips.size())) return -1;
            const Clip& c = t->clips[(size_t) index];
            if (! c.isAudio() || c.audio == nullptr) return -1;
            const auto& ab = *c.audio;
            const int frames = ab.getNumSamples(), nch = juce::jmax (1, ab.getNumChannels());
            mono.resize ((size_t) frames);
            for (int i = 0; i < frames; ++i)
            {
                float s = 0.0f;
                for (int ch = 0; ch < nch; ++ch) s += ab.getSample (ch, i);
                mono[(size_t) i] = s / (float) nch;
            }
            srcRate      = c.audioSourceRate;
            clipStart    = c.startBeat;
            clipLenBeats = c.lengthBeats;
        }

        const auto onsetSamples = detectOnsets (mono.data(), (int) mono.size(), srcRate, juce::jmax (0.0f, sensitivity));
        if (onsetSamples.empty()) return 1;   // no transients -> a single (unchanged) slice

        // Map onset source-samples -> absolute beats (natural-speed playback, tempo-aware),
        // keep only those strictly inside the clip, ascending.
        const double clipStartSec = apiBeatsToSeconds (clipStart);
        const double clipEndBeat  = clipStart + clipLenBeats;
        std::vector<double> beats;
        for (int s : onsetSamples)
        {
            const double b = apiSecondsToBeats (clipStartSec + (double) s / srcRate);
            if (b > clipStart + 1.0e-4 && b < clipEndBeat - 1.0e-4) beats.push_back (b);
        }
        std::sort (beats.begin(), beats.end());

        // Split left-to-right, always cutting the freshly-created right piece so later
        // (rightward) onsets land inside it.
        int cur = index, slices = 1;
        for (double b : beats)
        {
            const int right = apiSplitClip (trackId, cur, b);
            if (right > 0) { cur = right; ++slices; }
        }
        return slices;
    });
}

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

            // Audio: trim each half's buffer to its own span so the right half continues
            // from the cut (not a replay from the buffer start). The split's source-sample
            // offset maps beats -> wall-clock seconds (tempo-aware) -> source samples.
            if (a.isAudio() && a.audio != nullptr)
            {
                const int    frames  = a.audio->getNumSamples();
                const int    nch     = a.audio->getNumChannels();
                const double secBase = apiBeatsToSeconds (b.startBeat - local);   // == original clip start
                const juce::int64 splitSrc = (juce::int64) std::llround ((apiBeatsToSeconds (b.startBeat) - secBase) * a.audioSourceRate);
                const juce::int64 cut = juce::jlimit ((juce::int64) 0, (juce::int64) frames, splitSrc);
                const int leftFrames  = (int) cut;
                const int rightFrames = frames - (int) cut;

                auto trim = [nch] (const juce::AudioBuffer<float>& src, int start, int len)
                {
                    auto out = std::make_shared<juce::AudioBuffer<float>> (nch, juce::jmax (1, len));
                    out->clear();
                    for (int ch = 0; ch < nch && len > 0; ++ch) out->copyFrom (ch, 0, src, ch, start, len);
                    return out;
                };
                auto left  = trim (*a.audio, 0, leftFrames);
                auto right = trim (*a.audio, (int) cut, rightFrames);
                a.audio = left;   a.peaks = std::make_shared<std::vector<float>> (buildPeaks (*left));
                b.audio = right;  b.peaks = std::make_shared<std::vector<float>> (buildPeaks (*right));
                a.fadeOutBeats = 0.0;   // the cut edges are hard; keep only the outer fades
                b.fadeInBeats  = 0.0;
                a.audioFile = {}; a.takeId = {};   // trimmed buffers no longer match the source file — embed
                b.audioFile = {}; b.takeId = {};
            }

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

// Flatten a looped MIDI clip: write out the notes of every repetition as explicit
// notes at their absolute positions, then un-loop the clip (contentLen = length).
// This "bakes" what you hear into editable notes so each repetition can diverge.
// MIDI clips only; a one-shot (non-looped, or content >= length) clip is already
// flat, so it's a no-op success. Mirrors the render's beat-space tiling (collectClip).
bool MainComponent::apiScaleClipTime (int trackId, int index, double factor)
{
    return callOnMessageThread ([&] () -> bool
    {
        Track* t = resolveTrack (trackId);
        if (t == nullptr) return false;
        pushUndoSnapshot();
        {
            const juce::ScopedLock sl (engineLock);
            if (! juce::isPositiveAndBelow (index, (int) t->clips.size())) return false;
            Clip& c = t->clips[(size_t) index];
            if (c.isAudio()) return false;                       // MIDI clips only
            const double f = juce::jlimit (0.125, 8.0, factor);
            scaleNoteTimes (c.notes, f);                         // stretch/compress the note rhythm
            c.contentLenBeats = juce::jmax (0.25, c.contentLenBeats * f);   // content window follows
            c.lengthBeats     = juce::jmax (0.25, c.lengthBeats * f);       // and the arrangement slot
        }
        if (t->arp.enabled) applyArpToTrack (*t);
        emitChange ("clip_changed", trackId);
        if (arrangeView) arrangeView->repaint();
        return true;
    });
}

bool MainComponent::apiConsolidateClip (int trackId, int index)
{
    return callOnMessageThread ([&] () -> bool
    {
        Track* t = resolveTrack (trackId);
        if (t == nullptr) return false;
        pushUndoSnapshot();
        {
            const juce::ScopedLock sl (engineLock);
            if (! juce::isPositiveAndBelow (index, (int) t->clips.size())) return false;
            Clip& c = t->clips[(size_t) index];
            if (c.isAudio()) return false;                       // MIDI clips only
            const double content = c.contentLenBeats > 0.0 ? c.contentLenBeats : c.lengthBeats;
            if (c.looped && content > 0.0 && content < c.lengthBeats - 1.0e-9)
            {
                std::vector<Note> flat;
                for (double off = 0.0; off < c.lengthBeats - 1.0e-9; off += content)
                    for (auto& n : c.notes)
                    {
                        const double onset = off + n.startBeat;
                        if (onset >= c.lengthBeats - 1.0e-9) continue;   // starts at/after the clip end
                        Note nn = n;
                        nn.startBeat   = onset;
                        nn.lengthBeats = juce::jmin (n.lengthBeats, c.lengthBeats - onset);   // don't ring past the clip
                        flat.push_back (nn);
                    }
                c.notes = std::move (flat);
            }
            c.contentLenBeats = c.lengthBeats;
            c.looped          = false;
        }
        if (t->arp.enabled) applyArpToTrack (*t);
        emitChange ("clip_changed", trackId);
        if (arrangeView) arrangeView->repaint();
        return true;
    });
}

// Set the transport loop region to a clip's [start, end) span and enable looping — for
// auditioning a clip on repeat. Delegates to apiSetLoop.
bool MainComponent::apiSetLoopToClip (int trackId, int index)
{
    return callOnMessageThread ([&] () -> bool
    {
        double s = 0.0, e = 0.0;
        {
            const juce::ScopedLock sl (engineLock);
            Track* t = resolveTrack (trackId);
            if (t == nullptr || ! juce::isPositiveAndBelow (index, (int) t->clips.size())) return false;
            const Clip& c = t->clips[(size_t) index];
            s = c.startBeat;
            e = c.startBeat + c.lengthBeats;
        }
        if (e <= s) return false;
        apiSetLoop (true, s, e);
        return true;
    });
}

// Tile a clip: append `copies` back-to-back duplicates after it (each one clip-length
// further along), turning a one-bar loop into an N-bar run. Returns the number of copies
// added, or -1. Copies share audio buffers (read-only) and duplicate MIDI notes.
int MainComponent::apiRepeatClip (int trackId, int index, int copies)
{
    return callOnMessageThread ([&] () -> int
    {
        copies = juce::jlimit (0, 256, copies);
        if (copies == 0) return 0;
        pushUndoSnapshot();
        Track* t = resolveTrack (trackId);
        if (t == nullptr) return -1;
        {
            const juce::ScopedLock sl (engineLock);
            if (! juce::isPositiveAndBelow (index, (int) t->clips.size())) return -1;
            const Clip base = t->clips[(size_t) index];   // copy before the vector grows/reallocates
            const double len = juce::jmax (0.0625, base.lengthBeats);
            for (int k = 1; k <= copies; ++k)
            {
                Clip c = base;
                c.startBeat = base.startBeat + (double) k * len;
                c.name = base.name + " " + juce::String (k + 1);
                t->clips.push_back (std::move (c));
            }
        }
        emitChange ("clip_changed", trackId);
        if (arrangeView) arrangeView->repaint();
        return copies;
    });
}

// Mute (disable) or enable a clip in the arrangement without deleting it — a muted clip
// is skipped by both the MIDI collector and the audio renderer. Works for any clip.
bool MainComponent::apiSetClipMuted (int trackId, int index, bool muted)
{
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        Track* t = resolveTrack (trackId);
        if (t == nullptr) return false;
        {
            const juce::ScopedLock sl (engineLock);
            if (! juce::isPositiveAndBelow (index, (int) t->clips.size())) return false;
            t->clips[(size_t) index].muted = muted;
        }
        emitChange ("clip_changed", trackId);
        if (arrangeView) arrangeView->repaint();
        return true;
    });
}

// Bounce (freeze) a clip to audio: render just this track over the clip's [start,end)
// region — offline and soloed, so only its instrument + inserts print — then load the
// result back as an embedded audio clip on a fresh "(bounce)" track. Non-destructive:
// the source clip/track are left untouched. Returns the new track id, or -1. Works for
// MIDI clips (prints the synth/sampler) and audio clips (prints the insert chain).
int MainComponent::apiBounceClip (int trackId, int index)
{
    return callOnMessageThread ([&] () -> int
    {
        double startBeat = 0.0, endBeat = 0.0;
        juce::String srcName;
        {
            const juce::ScopedLock sl (engineLock);
            Track* t = resolveTrack (trackId);
            if (t == nullptr || ! juce::isPositiveAndBelow (index, (int) t->clips.size())) return -1;
            const Clip& c = t->clips[(size_t) index];
            startBeat = c.startBeat;
            endBeat   = c.startBeat + c.lengthBeats;
            srcName   = t->name;
        }
        if (endBeat <= startBeat) return -1;

        // Offline soloed bounce of the region to a temp WAV, then re-import it (the clip
        // embeds its buffer, so the temp file is safe to delete afterwards). A short tail
        // captures instrument release / insert tails without much overhang.
        juce::File tmp = juce::File::createTempFile (".wav");
        if (! apiRenderToFile (tmp.getFullPathName(), 0.5, startBeat, endBeat, true, trackId))
        { tmp.deleteFile(); return -1; }

        const int newTrack = apiAddAudioTrack (srcName + " (bounce)");
        if (newTrack < 0) { tmp.deleteFile(); return -1; }
        const int clipIdx = apiAddAudioClip (newTrack, startBeat, tmp.getFullPathName(), 1.0f);
        tmp.deleteFile();
        if (clipIdx < 0) { apiRemoveTrack (newTrack); return -1; }

        {
            const juce::ScopedLock sl (engineLock);
            Track* nt = resolveTrack (newTrack);
            if (nt != nullptr && juce::isPositiveAndBelow (clipIdx, (int) nt->clips.size()))
                nt->clips[(size_t) clipIdx].name = srcName + " (bounce)";
        }
        emitChange ("track_added", newTrack);
        if (arrangeView) arrangeView->rebuild();
        return newTrack;
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

bool MainComponent::apiSetClipFadeShape (int trackId, int index, int shape)
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
            c.fadeShape = juce::jlimit (0, 2, shape);   // 0 linear, 1 equal-power, 2 exponential
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

// Non-destructive playback transpose (semitones) on a MIDI clip — the stored notes are
// untouched; collectNotes offsets each pitch at render time. Distinct from apiTransposeClip
// (which edits the notes). Clamped to +/-48 semitones.
bool MainComponent::apiSetClipTranspose (int trackId, int index, int semitones)
{
    return callOnMessageThread ([&] () -> bool
    {
        Track* t = resolveTrack (trackId);
        if (t == nullptr) return false;
        pushUndoSnapshot();
        {
            const juce::ScopedLock sl (engineLock);
            if (! juce::isPositiveAndBelow (index, (int) t->clips.size())) return false;
            auto& c = t->clips[(size_t) index];
            if (c.isAudio()) return false;                       // MIDI clips only
            c.transpose = juce::jlimit (-48, 48, semitones);
        }
        emitChange ("clip_changed", trackId);
        return true;
    });
}

// Non-destructive playback velocity scale (multiplier) on a MIDI clip — each note's
// velocity is scaled at render time, stored notes untouched. Clamped to 0..2.
bool MainComponent::apiSetClipVelocity (int trackId, int index, float scale)
{
    return callOnMessageThread ([&] () -> bool
    {
        Track* t = resolveTrack (trackId);
        if (t == nullptr) return false;
        pushUndoSnapshot();
        {
            const juce::ScopedLock sl (engineLock);
            if (! juce::isPositiveAndBelow (index, (int) t->clips.size())) return false;
            auto& c = t->clips[(size_t) index];
            if (c.isAudio()) return false;                       // MIDI clips only
            c.velocityScale = juce::jlimit (0.0f, 2.0f, scale);
        }
        emitChange ("clip_changed", trackId);
        return true;
    });
}

// Set every note's fire probability in a MIDI clip (0..1) — the bulk generative control.
// Per-note probabilities can be set individually via AddClip; this is the clip-wide op.
bool MainComponent::apiSetClipProbability (int trackId, int index, float prob)
{
    return callOnMessageThread ([&] () -> bool
    {
        Track* t = resolveTrack (trackId);
        if (t == nullptr) return false;
        pushUndoSnapshot();
        {
            const juce::ScopedLock sl (engineLock);
            if (! juce::isPositiveAndBelow (index, (int) t->clips.size())) return false;
            auto& c = t->clips[(size_t) index];
            if (c.isAudio()) return false;                       // MIDI clips only
            const float p = juce::jlimit (0.0f, 1.0f, prob);
            for (auto& n : c.notes) n.probability = p;
        }
        emitChange ("clip_changed", trackId);
        return true;
    });
}

// A clip's notes as a JSON array (see NotesJson.h) — for the ExportNotesJSON RPC and the
// desktop "Copy notes" gesture. Empty string if the clip is missing / not a note clip.
juce::String MainComponent::apiExportClipNotesJson (int trackId, int index)
{
    return notesToJson (apiGetClipNotes (trackId, index));
}

// Build a new clip on trackId at startBeat from a JSON note array (see NotesJson.h). The
// clip length is the furthest note end (>= 1 beat). Returns the new clip index, or -1 if
// the JSON has no usable notes / the track is missing. Backs ImportNotesJSON + "Paste notes".
int MainComponent::apiImportClipNotesJson (int trackId, double startBeat, const juce::String& json)
{
    const auto notes = notesFromJson (json);
    if (notes.empty()) return -1;
    double len = 0.0;
    for (const auto& n : notes) len = juce::jmax (len, n.startBeat + n.lengthBeats);
    if (len <= 0.0) len = 1.0;
    return apiAddClip (trackId, startBeat, len, len, false, notes, "pasted");
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

bool MainComponent::apiLegatoClip (int trackId, int index, float amount)
{ GLOOPY_EDIT_CLIP_NOTES (legatoNotes (notes, amount)) }

bool MainComponent::apiRampClipVelocity (int trackId, int index, float fromVel, float toVel)
{ GLOOPY_EDIT_CLIP_NOTES (rampVelocities (notes, fromVel, toVel)) }

bool MainComponent::apiEchoClip (int trackId, int index, double delayBeats, int repeats, float feedback)
{ GLOOPY_EDIT_CLIP_NOTES (echoNotes (notes, delayBeats, repeats, feedback)) }

bool MainComponent::apiInvertClip (int trackId, int index)
{ GLOOPY_EDIT_CLIP_NOTES (invertNotes (notes)) }

bool MainComponent::apiRatchetClip (int trackId, int index, int subdivisions)
{ GLOOPY_EDIT_CLIP_NOTES (ratchetNotes (notes, subdivisions)) }

bool MainComponent::apiHarmonizeClip (int trackId, int index, int semitones)
{ GLOOPY_EDIT_CLIP_NOTES (harmonizeNotes (notes, semitones)) }

bool MainComponent::apiSplitNotesAtBeat (int trackId, int index, double beat)
{ GLOOPY_EDIT_CLIP_NOTES (splitNotesAtBeat (notes, beat)) }

// Recompute a track's live-arp expansion for all its MIDI clips. Caller holds engineLock.
void MainComponent::applyArpToTrack (Track& t)
{
    for (auto& c : t.clips)
    {
        if (t.arp.enabled && c.type == ClipType::Midi)
        {
            const double len = c.contentLenBeats > 0.0 ? c.contentLenBeats : c.lengthBeats;
            c.arpNotes = expandArp (c.notes, t.arp.rate, t.arp.octaves, t.arp.gate, t.arp.mode,
                                    t.arp.swing, t.arp.hold, len, t.arp.probability);
        }
        else
            c.arpNotes.clear();
    }
}

bool MainComponent::apiSetTrackArp (int trackId, bool enabled, double rate, int octaves, float gate, int mode,
                                   float swing, bool hold, float probability)
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
            t->arp.probability = juce::jlimit (0.0f, 1.0f, probability);
            applyArpToTrack (*t);
        }
        emitChange ("track_arp", trackId);
        return true;
    });
}

bool MainComponent::apiGetTrackArp (int trackId, bool& enabled, double& rate, int& octaves, float& gate, int& mode,
                                   float& swing, bool& hold, float& probability)
{
    return callOnMessageThread ([&] () -> bool
    {
        const juce::ScopedLock sl (engineLock);
        Track* t = resolveTrack (trackId);
        if (t == nullptr) return false;
        enabled = t->arp.enabled; rate = t->arp.rate; octaves = t->arp.octaves;
        gate = t->arp.gate; mode = t->arp.mode; swing = t->arp.swing; hold = t->arp.hold;
        probability = t->arp.probability;
        return true;
    });
}

bool MainComponent::apiAddChord (int trackId, int index, int root, const juce::String& type,
                                 double startBeat, double lengthBeats, float velocity, int inversion)
{ GLOOPY_EDIT_CLIP_NOTES ( { auto ch = makeChord (root, type, inversion, startBeat, lengthBeats, velocity);
                             for (auto& n : ch) notes.push_back (n); } ) }

#undef GLOOPY_EDIT_CLIP_NOTES
