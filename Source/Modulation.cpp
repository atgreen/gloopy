// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

// Modulation matrix — LFO sources that drive any universal-parameter-model target
// (see Parameters.cpp) each audio block. This rides on two existing pieces: the
// string-id parameter model (the target grammar) and the per-block, engineLock-held
// evaluation shape of MainComponent::evaluateAutomation (called right next to it in
// renderBlock). value = center + depth * osc(rate * t). Kept minimal: one LFO per
// target, four shapes.

#include "MainComponent.h"
#include "EngineLock.h"
#include "SynthGenerator.h"
#include "Lfo.h"
#include "ParamId.h"
#include <cmath>
#include <cstring>
#include <cstdlib>

// Canonical single set: replace ANY modulation(s) already on this target with one source.
bool MainComponent::apiSetModulation (const juce::String& target, float rate, float depth, int shape, float center,
                                      float syncBeats, float phase, bool unipolar, float slewMs)
{
    if (target.trim().isEmpty()) return false;
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        GLOOPY_ELOCK(sl);
        modulations.erase (std::remove_if (modulations.begin(), modulations.end(),
                               [&] (const Mod& m) { return m.target == target; }),
                           modulations.end());
        const float ph = phase - std::floor (phase);
        modulations.push_back ({ target, juce::jmax (0.0f, rate), depth, center, juce::jlimit (0, 4, shape),
                                 juce::jmax (0.0f, syncBeats), ph, unipolar, juce::jmax (0.0f, slewMs) });
        std::cout << "[mod set] " << target << " rate=" << rate << " depth=" << depth << " center=" << center
                  << " shape=" << shape << " sync=" << syncBeats << " -> " << modulations.size() << " total" << std::endl;
        return true;
    });
}

// Append an ADDITIONAL modulation source on this target — multiple sources on the same
// param sum in evaluateModulation (center + sum of depth*osc), so two LFOs stack.
bool MainComponent::apiAddModulation (const juce::String& target, float rate, float depth, int shape, float center,
                                      float syncBeats, float phase, bool unipolar, float slewMs)
{
    if (target.trim().isEmpty()) return false;
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        GLOOPY_ELOCK(sl);
        const float ph = phase - std::floor (phase);
        modulations.push_back ({ target, juce::jmax (0.0f, rate), depth, center, juce::jlimit (0, 4, shape),
                                 juce::jmax (0.0f, syncBeats), ph, unipolar, juce::jmax (0.0f, slewMs) });
        std::cout << "[mod add] " << target << " rate=" << rate << " depth=" << depth << " center=" << center
                  << " shape=" << shape << " sync=" << syncBeats << " -> " << modulations.size() << " total" << std::endl;
        return true;
    });
}

// Clear the transient one-pole slew state so the next block seeds afresh — makes an
// offline render reproducible regardless of prior live state (called from apiRenderToFile).
void MainComponent::resetModulationSmoothing()
{
    GLOOPY_ELOCK(sl);
    for (auto& m : modulations) m.smoothInit = false;
}

bool MainComponent::apiRemoveModulation (const juce::String& target)
{
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        GLOOPY_ELOCK(sl);
        const auto before = modulations.size();
        modulations.erase (std::remove_if (modulations.begin(), modulations.end(),
                               [&] (const Mod& m) { return m.target == target; }),
                           modulations.end());
        return modulations.size() != before;
    });
}

std::vector<MainComponent::ModSnap> MainComponent::apiListModulations()
{
    return callOnMessageThread ([&]
    {
        GLOOPY_ELOCK(sl);
        std::vector<ModSnap> out;
        for (auto& m : modulations) out.push_back ({ m.target, m.rate, m.depth, m.center, m.shape, m.syncBeats, m.phase, m.unipolar, m.slewMs });
        return out;
    });
}

// Audio-thread-safe direct write of a ParamModel target. Caller holds engineLock.
// Mirrors the id grammar in Parameters.cpp but writes atomics directly (no message
// thread, no undo) — the same discipline evaluateAutomation uses.
void MainComponent::applyParamValue (const juce::String& id, float v)
{
    using gloopy::ieq;
    // Parse the ParamModel id WITHOUT allocating — this runs on the audio thread every block
    // for each automation/modulation lane (principle 4; verified by Diagnostics.audio_thread_allocs).
    // Copy into a stack buffer and split on '/' in place (skipping empties, like removeEmptyStrings).
    char buf[192];
    {
        const char* src = id.toRawUTF8();
        const size_t len = std::strlen (src);
        if (len >= sizeof buf) return;                 // our ids are short; anything longer isn't one
        std::memcpy (buf, src, len + 1);
    }
    const char* seg[6];
    int nseg = 0;
    for (char* p = buf; nseg < 6; )
    {
        while (*p == '/') ++p;                          // skip empty segments
        if (*p == 0) break;
        seg[nseg++] = p;
        while (*p != 0 && *p != '/') ++p;
        if (*p == '/') *p++ = 0;
    }
    if (nseg < 3) return;

    if (ieq (seg[0], "track"))
    {
        const int tid = std::atoi (seg[1]);
        if (nseg == 3)
        {
            for (auto& t : tracks) if (t->id == tid)
            {
                if      (ieq (seg[2], "volume")) t->volume.store (juce::jlimit (0.0f, 1.0f, v));
                else if (ieq (seg[2], "pan"))    t->pan.store (juce::jlimit (-1.0f, 1.0f, v));
                else if (ieq (seg[2], "mute"))   t->mute.store (v >= 0.5f);
                else if (ieq (seg[2], "solo"))   t->solo.store (v >= 0.5f);
                break;
            }
        }
        else if (nseg == 4 && ieq (seg[2], "synth"))
        {
            for (auto& t : tracks) if (t->id == tid) { applySynthParam (t.get(), seg[3], v); break; }
        }
        else if (nseg == 4 && ieq (seg[2], "macro"))    // track/<id>/macro/<index>: a rack macro (0..1)
        {
            const int mi = std::atoi (seg[3]);
            for (auto& t : tracks) if (t->id == tid)
            {
                if (juce::isPositiveAndBelow (mi, (int) t->macros.size()))
                {
                    auto& mac = t->macros[(size_t) mi];
                    mac.value = juce::jlimit (0.0f, 1.0f, v);
                    // Re-apply the macro's mappings using only the audio-safe writes used above
                    // (applySynthParam / effect param.set) — never the message-thread apiSetEffectParam.
                    for (auto& mp : mac.mappings)
                    {
                        const float pv = mp.lo + mac.value * (mp.hi - mp.lo);
                        if (mp.synthParam.isNotEmpty())
                            applySynthParam (t.get(), mp.synthParam.toRawUTF8(), pv);
                        else if (mp.insert >= 0 && mp.slot >= 0
                                 && juce::isPositiveAndBelow (mp.insert, (int) mixerTracks.size()))
                        {
                            auto& fx = mixerTracks[(size_t) mp.insert]->effects;
                            if (juce::isPositiveAndBelow (mp.slot, (int) fx.size()))
                                for (auto& pr : fx[(size_t) mp.slot]->parameters())
                                    if (pr.name.equalsIgnoreCase (mp.effectParam.toRawUTF8())) { pr.set (pv); break; }
                        }
                    }
                }
                break;
            }
        }
        else if (nseg == 4 && ieq (seg[2], "plugin"))   // track/<id>/plugin/<index>: normalised 0..1
        {
            for (auto& t : tracks)
                if (t->id == tid && t->generator)
                    if (auto* proc = t->generator->getPluginInstance())
                    {
                        const auto& ps = proc->getParameters();
                        const int pi = std::atoi (seg[3]);
                        if (juce::isPositiveAndBelow (pi, ps.size())) ps[pi]->setValue (juce::jlimit (0.0f, 1.0f, v));
                        break;
                    }
        }
    }
    else if (ieq (seg[0], "insert") && nseg == 3)
    {
        const int i = std::atoi (seg[1]);
        if (juce::isPositiveAndBelow (i, (int) mixerTracks.size()))
        {
            auto& mt = *mixerTracks[(size_t) i];
            if      (ieq (seg[2], "volume")) mt.volume.store (juce::jlimit (0.0f, 1.0f, v));
            else if (ieq (seg[2], "pan"))    mt.pan.store (juce::jlimit (-1.0f, 1.0f, v));
            else if (ieq (seg[2], "mute"))   mt.mute.store (v >= 0.5f);
            else if (ieq (seg[2], "solo"))   mt.solo.store (v >= 0.5f);
        }
    }
    else if (ieq (seg[0], "effect") && nseg == 4)
    {
        const int i = std::atoi (seg[1]), slot = std::atoi (seg[2]);
        if (juce::isPositiveAndBelow (i, (int) mixerTracks.size()))
        {
            auto& fx = mixerTracks[(size_t) i]->effects;
            if (juce::isPositiveAndBelow (slot, (int) fx.size()))
                for (auto& pr : fx[(size_t) slot]->parameters())
                    if (pr.name.equalsIgnoreCase (seg[3])) { pr.set (v); break; }   // equalsIgnoreCase(const char*) is alloc-free
        }
    }
    else if (ieq (seg[0], "effect") && nseg == 5 && ieq (seg[3], "plugin"))   // effect/<i>/<slot>/plugin/<index>
    {
        const int i = std::atoi (seg[1]), slot = std::atoi (seg[2]);
        if (juce::isPositiveAndBelow (i, (int) mixerTracks.size()))
        {
            auto& fx = mixerTracks[(size_t) i]->effects;
            if (juce::isPositiveAndBelow (slot, (int) fx.size()))
                if (auto* proc = fx[(size_t) slot]->getPluginInstance())
                {
                    const auto& ps = proc->getParameters();
                    const int pi = std::atoi (seg[4]);
                    if (juce::isPositiveAndBelow (pi, ps.size())) ps[pi]->setValue (juce::jlimit (0.0f, 1.0f, v));
                }
        }
    }
    else if (ieq (seg[0], "group") && nseg == 3 && ieq (seg[2], "gain"))   // group/<name>/gain: a VCA fader
    {
        // VCA offset: the group gain scales its members' volume in the mix, so members keep their
        // own automation untouched. equalsIgnoreCase(const char*) is alloc-free (audio-thread safe).
        for (auto& cg : controlGroups)
            if (cg->name.equalsIgnoreCase (seg[1])) { cg->gain.store (juce::jlimit (0.0f, 1.0f, v)); break; }
    }
}

void MainComponent::evaluateModulation (double timeSeconds, double beatPos)
{
    // Called from renderBlock while holding engineLock — iterate the locked vector.
    // Multiple modulation sources may target the SAME ParamModel id: they SUM. The
    // applied value is center(first source on the target) + Σ depth_i * osc_i, written
    // ONCE per target (so two LFOs on one cutoff stack instead of the last one winning).
    // A tempo-synced source (syncBeats>0) derives its phase from beatPos so its period
    // tracks the tempo; a free source uses rate (Hz). Grouping is an allocation-free
    // O(n^2) scan over the small mod vector (no heap on the audio thread — principle 4).
    const double blockDur = currentSampleRate > 0.0 ? (double) currentBlockSize / currentSampleRate : 0.0;
    const size_t n = modulations.size();
    for (size_t i = 0; i < n; ++i)
    {
        // Process each distinct target once, at its first occurrence.
        bool firstForTarget = true;
        for (size_t k = 0; k < i; ++k) if (modulations[k].target == modulations[i].target) { firstForTarget = false; break; }
        if (! firstForTarget) continue;

        float sumDelta = 0.0f;
        bool  anyActive = false;
        for (size_t j = i; j < n; ++j)
        {
            auto& m = modulations[j];
            if (m.target != modulations[i].target) continue;
            if (m.syncBeats <= 0.0f && m.rate <= 0.0f && m.shape != 0) continue;   // constant, contributes nothing
            const double phase = lfoPhaseCycles (m.syncBeats, beatPos, m.rate, timeSeconds);
            float delta = m.depth * (float) lfoUnit (m.shape, phase, m.phase, m.unipolar);
            // One-pole slew (per source, per block): soften abrupt shape edges / zipper noise.
            // Slewing the delta (center is constant) is identical to slewing center+delta.
            if (m.slewMs > 0.0f && blockDur > 0.0)
            {
                const float coeff = (float) (1.0 - std::exp (-blockDur / ((double) m.slewMs * 1.0e-3)));
                if (! m.smoothInit) { m.smoothState = delta; m.smoothInit = true; }   // seed, no jump on the first block
                else                  m.smoothState += (delta - m.smoothState) * coeff;
                delta = m.smoothState;
            }
            sumDelta += delta;
            anyActive = true;
        }
        if (anyActive) applyParamValue (modulations[i].target, modulations[i].center + sumDelta);
    }
}
