// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

// Modulation matrix — LFO sources that drive any universal-parameter-model target
// (see Parameters.cpp) each audio block. This rides on two existing pieces: the
// string-id parameter model (the target grammar) and the per-block, engineLock-held
// evaluation shape of MainComponent::evaluateAutomation (called right next to it in
// renderBlock). value = center + depth * osc(rate * t). Kept minimal: one LFO per
// target, four shapes.

#include "MainComponent.h"
#include "SynthGenerator.h"
#include "Lfo.h"
#include <cmath>

bool MainComponent::apiSetModulation (const juce::String& target, float rate, float depth, int shape, float center,
                                      float syncBeats, float phase, bool unipolar, float slewMs)
{
    if (target.trim().isEmpty()) return false;
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        const juce::ScopedLock sl (engineLock);
        auto it = std::find_if (modulations.begin(), modulations.end(),
                                [&] (const Mod& m) { return m.target == target; });
        // phase wraps to [0,1): only the fractional cycle offset matters.
        const float ph = phase - std::floor (phase);
        Mod m { target, juce::jmax (0.0f, rate), depth, center, juce::jlimit (0, 4, shape),
                juce::jmax (0.0f, syncBeats), ph, unipolar, juce::jmax (0.0f, slewMs) };
        if (it != modulations.end()) *it = m;      // upsert (also clears transient slew state)
        else                          modulations.push_back (m);
        std::cout << "[mod] " << target << " rate=" << rate << " depth=" << depth
                  << " center=" << center << " shape=" << shape << " sync=" << syncBeats
                  << " phase=" << ph << " unipolar=" << (int) unipolar << " slew=" << slewMs << std::endl;
        return true;
    });
}

// Clear the transient one-pole slew state so the next block seeds afresh — makes an
// offline render reproducible regardless of prior live state (called from apiRenderToFile).
void MainComponent::resetModulationSmoothing()
{
    const juce::ScopedLock sl (engineLock);
    for (auto& m : modulations) m.smoothInit = false;
}

bool MainComponent::apiRemoveModulation (const juce::String& target)
{
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        const juce::ScopedLock sl (engineLock);
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
        const juce::ScopedLock sl (engineLock);
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
    juce::StringArray tok; tok.addTokens (id, "/", ""); tok.removeEmptyStrings();
    if (tok.size() < 3) return;

    if (tok[0] == "track")
    {
        const int tid = tok[1].getIntValue();
        if (tok.size() == 3)
        {
            for (auto& t : tracks) if (t->id == tid)
            {
                if      (tok[2] == "volume") t->volume.store (juce::jlimit (0.0f, 1.0f, v));
                else if (tok[2] == "pan")    t->pan.store (juce::jlimit (-1.0f, 1.0f, v));
                else if (tok[2] == "mute")   t->mute.store (v >= 0.5f);
                else if (tok[2] == "solo")   t->solo.store (v >= 0.5f);
                break;
            }
        }
        else if (tok.size() == 4 && tok[2] == "synth")
        {
            for (auto& t : tracks) if (t->id == tid) { applySynthParam (t.get(), tok[3], v); break; }
        }
        else if (tok.size() == 4 && tok[2] == "plugin")   // track/<id>/plugin/<index>: normalised 0..1
        {
            for (auto& t : tracks)
                if (t->id == tid && t->generator)
                    if (auto* proc = t->generator->getPluginInstance())
                    {
                        const auto& ps = proc->getParameters();
                        const int pi = tok[3].getIntValue();
                        if (juce::isPositiveAndBelow (pi, ps.size())) ps[pi]->setValue (juce::jlimit (0.0f, 1.0f, v));
                        break;
                    }
        }
    }
    else if (tok[0] == "insert" && tok.size() == 3)
    {
        const int i = tok[1].getIntValue();
        if (juce::isPositiveAndBelow (i, (int) mixerTracks.size()))
        {
            auto& mt = *mixerTracks[(size_t) i];
            if      (tok[2] == "volume") mt.volume.store (juce::jlimit (0.0f, 1.0f, v));
            else if (tok[2] == "pan")    mt.pan.store (juce::jlimit (-1.0f, 1.0f, v));
            else if (tok[2] == "mute")   mt.mute.store (v >= 0.5f);
            else if (tok[2] == "solo")   mt.solo.store (v >= 0.5f);
        }
    }
    else if (tok[0] == "effect" && tok.size() == 4)
    {
        const int i = tok[1].getIntValue(), slot = tok[2].getIntValue();
        if (juce::isPositiveAndBelow (i, (int) mixerTracks.size()))
        {
            auto& fx = mixerTracks[(size_t) i]->effects;
            if (juce::isPositiveAndBelow (slot, (int) fx.size()))
                for (auto& pr : fx[(size_t) slot]->parameters())
                    if (pr.name.equalsIgnoreCase (tok[3])) { pr.set (v); break; }
        }
    }
    else if (tok[0] == "effect" && tok.size() == 5 && tok[3] == "plugin")   // effect/<i>/<slot>/plugin/<index>
    {
        const int i = tok[1].getIntValue(), slot = tok[2].getIntValue();
        if (juce::isPositiveAndBelow (i, (int) mixerTracks.size()))
        {
            auto& fx = mixerTracks[(size_t) i]->effects;
            if (juce::isPositiveAndBelow (slot, (int) fx.size()))
                if (auto* proc = fx[(size_t) slot]->getPluginInstance())
                {
                    const auto& ps = proc->getParameters();
                    const int pi = tok[4].getIntValue();
                    if (juce::isPositiveAndBelow (pi, ps.size())) ps[pi]->setValue (juce::jlimit (0.0f, 1.0f, v));
                }
        }
    }
}

void MainComponent::evaluateModulation (double timeSeconds, double beatPos)
{
    // Called from renderBlock while holding engineLock — iterate the locked vector.
    // A tempo-synced LFO (syncBeats>0) derives its phase from beatPos so its period
    // tracks the tempo; a free LFO uses rate (Hz) against transport seconds.
    const double blockDur = currentSampleRate > 0.0 ? (double) currentBlockSize / currentSampleRate : 0.0;
    for (auto& m : modulations)
    {
        if (m.syncBeats <= 0.0f && m.rate <= 0.0f && m.shape != 0) continue;   // constant, nothing to do
        const double phase = lfoPhaseCycles (m.syncBeats, beatPos, m.rate, timeSeconds);
        float value = m.center + m.depth * (float) lfoUnit (m.shape, phase, m.phase, m.unipolar);
        // One-pole slew (per block): soften abrupt shape edges / avoid zipper noise.
        if (m.slewMs > 0.0f && blockDur > 0.0)
        {
            const float coeff = (float) (1.0 - std::exp (-blockDur / ((double) m.slewMs * 1.0e-3)));
            if (! m.smoothInit) { m.smoothState = value; m.smoothInit = true; }   // seed, no jump on the first block
            else                  m.smoothState += (value - m.smoothState) * coeff;
            value = m.smoothState;
        }
        applyParamValue (m.target, value);
    }
}
