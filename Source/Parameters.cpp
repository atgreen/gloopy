// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

// Universal parameter model — a single, flat, string-addressed view of every
// automatable value in the project. UI knobs, automation, MIDI/OSC mapping, gRPC
// state, and (later) the modulation matrix all address the same stable ids instead
// of each re-deriving the track/insert/effect tuple. This is the keystone the rest
// of the roadmap rides on; it deliberately *reuses* the existing thread-safe setters
// (apiSetTrackParams / apiSetInsertParams / apiSetSynthParam / apiSetEffectParam) so
// the audio-thread discipline is inherited rather than duplicated.
//
// Canonical id grammar:
//   track/<id>/{volume|pan|mute|solo}        insert/<index>/{volume|pan|mute|solo}
//   track/<id>/synth/<name>                  effect/<insert>/<slot>/<paramName>

#include "MainComponent.h"
#include "SynthGenerator.h"
#include "SynthVoice.h"
#include "Effect.h"
#include "ParamScale.h"

namespace
{
// Best-effort descriptor for a built-in synth engine parameter: {min,max,def,unit,scaling}.
struct SynthRange { float min, max, def; const char* unit; const char* scaling; };

// Keyed by the SynthParams names used in writeSynthParams (SynthVoice.h).
SynthRange synthRange (const juce::String& n)
{
    if (n == "wave" || n == "osc2wave")   return { 0.f, 4.f, 1.f, "", "enum" };
    if (n == "osc2detune")                return { -24.f, 24.f, 0.f, "st", "linear" };
    if (n == "oscmix")                    return { 0.f, 1.f, 0.5f, "", "linear" };
    if (n == "sub")                       return { 0.f, 1.f, 0.f, "", "linear" };
    if (n == "gain" || n == "sustain")    return { 0.f, 1.f, 0.8f, "", "linear" };
    if (n == "attack" || n == "decay" || n == "release"
     || n == "fattack" || n == "fdecay" || n == "frelease")
                                          return { 0.f, 5.f, 0.1f, "s", "linear" };
    if (n == "ftype")                     return { 0.f, 3.f, 0.f, "", "enum" };
    if (n == "cutoff")                    return { 20.f, 20000.f, 20000.f, "Hz", "log" };
    if (n == "reso")                      return { 0.f, 1.f, 0.1f, "", "linear" };
    if (n == "fenvamt")                   return { -1.f, 1.f, 0.f, "", "linear" };
    if (n == "fsustain")                  return { 0.f, 1.f, 1.f, "", "linear" };
    if (n == "lfotarget")                 return { 0.f, 3.f, 0.f, "", "enum" };
    if (n == "lforate")                   return { 0.f, 20.f, 1.f, "Hz", "linear" };
    if (n == "lfodepth")                  return { 0.f, 1.f, 0.f, "", "linear" };
    if (n == "detune")                    return { -2400.f, 2400.f, 0.f, "cents", "linear" };
    return { 0.f, 1.f, 0.f, "", "linear" };
}

MainComponent::ParamDesc mk (juce::String id, juce::String name, float value,
                             float mn, float mx, float def,
                             const char* unit, const char* scaling)
{
    return { std::move (id), std::move (name), unit, scaling, value, mn, mx, def };
}

// Set one hosted-plugin parameter by index (value normalised 0..1). notifyHost=true from
// the message thread (updates the plugin's generic editor); false from the audio thread.
bool setPluginParam (juce::AudioProcessor* proc, int idx, float value, bool notifyHost)
{
    if (proc == nullptr) return false;
    const auto& ps = proc->getParameters();
    if (! juce::isPositiveAndBelow (idx, ps.size())) return false;
    const float v = juce::jlimit (0.0f, 1.0f, value);
    if (notifyHost) ps[idx]->setValueNotifyingHost (v);
    else            ps[idx]->setValue (v);
    return true;
}
} // namespace

std::vector<MainComponent::ParamDesc> MainComponent::apiListParameters()
{
    return callOnMessageThread ([&]
    {
        std::vector<ParamDesc> out;

        // ── tracks (mixer strip on the track itself + built-in synth engine) ──
        for (auto& t : tracks)
        {
            const juce::String base = "track/" + juce::String (t->id) + "/";
            out.push_back (mk (base + "volume", t->name + " Volume", t->volume.load(), 0.f, 1.f, 0.8f, "", "linear"));
            out.push_back (mk (base + "pan",    t->name + " Pan",    t->pan.load(),   -1.f, 1.f, 0.f,  "", "linear"));
            out.push_back (mk (base + "mute",   t->name + " Mute",   t->mute.load() ? 1.f : 0.f, 0.f, 1.f, 0.f, "", "bool"));
            out.push_back (mk (base + "solo",   t->name + " Solo",   t->solo.load() ? 1.f : 0.f, 0.f, 1.f, 0.f, "", "bool"));

            if (auto* sg = dynamic_cast<SynthGenerator*> (t->generator.get()))
            {
                juce::ValueTree s ("SYNTH");
                writeSynthParams (s, sg->engine.params);
                for (int p = 0; p < s.getNumProperties(); ++p)
                {
                    const auto key = s.getPropertyName (p).toString();
                    const auto r   = synthRange (key);
                    out.push_back (mk (base + "synth/" + key, t->name + " " + key,
                                       (float) (double) s.getProperty (key),
                                       r.min, r.max, r.def, r.unit, r.scaling));
                }
            }
            else if (auto* proc = t->generator ? t->generator->getPluginInstance() : nullptr)
            {
                // Hosted instrument plugin (VST3/LV2): each param addressed by index,
                // value normalised 0..1 (the plugin's own scaling is internal).
                const juce::ScopedLock sl (engineLock);
                const auto& ps = proc->getParameters();
                for (int pi = 0; pi < ps.size(); ++pi)
                    out.push_back (mk (base + "plugin/" + juce::String (pi),
                                       t->name + " " + ps[pi]->getName (48),
                                       ps[pi]->getValue(), 0.f, 1.f, ps[pi]->getDefaultValue(), "", "linear"));
            }
        }

        // ── mixer inserts (strip + each effect's generic parameters) ──
        for (size_t i = 0; i < mixerTracks.size(); ++i)
        {
            auto& mt = *mixerTracks[i];
            const juce::String base = "insert/" + juce::String ((int) i) + "/";
            out.push_back (mk (base + "volume", mt.name + " Volume", mt.volume.load(), 0.f, 1.f, 0.8f, "", "linear"));
            out.push_back (mk (base + "pan",    mt.name + " Pan",    mt.pan.load(),   -1.f, 1.f, 0.f,  "", "linear"));
            out.push_back (mk (base + "mute",   mt.name + " Mute",   mt.mute.load() ? 1.f : 0.f, 0.f, 1.f, 0.f, "", "bool"));
            out.push_back (mk (base + "solo",   mt.name + " Solo",   mt.solo.load() ? 1.f : 0.f, 0.f, 1.f, 0.f, "", "bool"));

            const juce::ScopedLock sl (engineLock);
            for (size_t slot = 0; slot < mt.effects.size(); ++slot)
            {
                auto& fx = *mt.effects[slot];
                const juce::String fxBase = "effect/" + juce::String ((int) i) + "/" + juce::String ((int) slot) + "/";
                for (auto& pr : fx.parameters())
                    out.push_back (mk (fxBase + pr.name, fx.name() + " " + pr.name,
                                       pr.get ? pr.get() : 0.f,
                                       pr.minValue, pr.maxValue, pr.defaultValue, "", "linear"));
                if (auto* proc = fx.getPluginInstance())   // plugin effect: params by index, 0..1
                {
                    const auto& ps = proc->getParameters();
                    for (int pi = 0; pi < ps.size(); ++pi)
                        out.push_back (mk (fxBase + "plugin/" + juce::String (pi),
                                           fx.name() + " " + ps[pi]->getName (48),
                                           ps[pi]->getValue(), 0.f, 1.f, ps[pi]->getDefaultValue(), "", "linear"));
                }
            }
        }

        return out;
    });
}

bool MainComponent::apiGetParameter (const juce::String& id, ParamDesc& out)
{
    for (auto& d : apiListParameters())
        if (d.id == id) { out = d; return true; }
    return false;
}

// Set a parameter from a 0..1 knob/fader position, mapping through the param's declared
// scaling (log/dB/linear) so UI knobs and external controllers travel perceptually. For
// plugin params (min/max 0/1, linear) this is the identity, matching the plugin's own
// normalised range.
bool MainComponent::apiSetParameterNormalized (const juce::String& id, float pos01)
{
    ParamDesc d;
    if (! apiGetParameter (id, d)) return false;
    return apiSetParameter (id, paramDenormalize (pos01, d.min, d.max, d.scaling));
}

// The human-meaningful param model (everything except the thousands of opaque hosted-
// plugin params) as an id->value list — written to the composition as a readable,
// discoverable manifest so external clients can read the param model from the repo
// without instantiating plugins. It is informational: on load, values come from each
// subsystem's own serialised section, not this snapshot.
std::vector<MainComponent::ParamDesc> MainComponent::apiSnapshotParameters()
{
    std::vector<ParamDesc> out;
    for (auto& d : apiListParameters())
        if (! d.id.contains ("/plugin/")) out.push_back (d);
    return out;
}

bool MainComponent::apiSetParameter (const juce::String& id, float value)
{
    juce::StringArray tok;
    tok.addTokens (id, "/", "");
    tok.removeEmptyStrings();
    if (tok.size() < 3) return false;

    const auto& domain = tok[0];

    if (domain == "track")
    {
        const int tid = tok[1].getIntValue();
        if (tok.size() == 3)
        {
            const auto& field = tok[2];
            const bool on = value >= 0.5f;
            if (field == "volume") return apiSetTrackParams (tid, true, value, false, 0, false, false, false, false, false, {});
            if (field == "pan")    return apiSetTrackParams (tid, false, 0, true, value, false, false, false, false, false, {});
            if (field == "mute")   return apiSetTrackParams (tid, false, 0, false, 0, true, on, false, false, false, {});
            if (field == "solo")   return apiSetTrackParams (tid, false, 0, false, 0, false, false, true, on, false, {});
            return false;
        }
        if (tok.size() == 4 && tok[2] == "synth")
            return apiSetSynthParam (tid, tok[3], value);
        if (tok.size() == 4 && tok[2] == "plugin")   // track/<id>/plugin/<index>
        {
            const juce::ScopedLock sl (engineLock);
            for (auto& t : tracks)
                if (t->id == tid && t->generator)
                    return setPluginParam (t->generator->getPluginInstance(), tok[3].getIntValue(), value, true);
            return false;
        }
        return false;
    }

    if (domain == "insert" && tok.size() == 3)
    {
        const int idx = tok[1].getIntValue();
        const auto& field = tok[2];
        const bool on = value >= 0.5f;
        if (field == "volume") return apiSetInsertParams (idx, true, value, false, 0, false, false, false, false);
        if (field == "pan")    return apiSetInsertParams (idx, false, 0, true, value, false, false, false, false);
        if (field == "mute")   return apiSetInsertParams (idx, false, 0, false, 0, true, on, false, false);
        if (field == "solo")   return apiSetInsertParams (idx, false, 0, false, 0, false, false, true, on);
        return false;
    }

    if (domain == "effect" && tok.size() == 4)
        return apiSetEffectParam (tok[1].getIntValue(), tok[2].getIntValue(), tok[3], value);

    if (domain == "effect" && tok.size() == 5 && tok[3] == "plugin")   // effect/<i>/<slot>/plugin/<index>
    {
        const int i = tok[1].getIntValue(), slot = tok[2].getIntValue();
        const juce::ScopedLock sl (engineLock);
        if (! juce::isPositiveAndBelow (i, (int) mixerTracks.size())) return false;
        auto& fx = mixerTracks[(size_t) i]->effects;
        if (! juce::isPositiveAndBelow (slot, (int) fx.size())) return false;
        return setPluginParam (fx[(size_t) slot]->getPluginInstance(), tok[4].getIntValue(), value, true);
    }

    return false;
}
