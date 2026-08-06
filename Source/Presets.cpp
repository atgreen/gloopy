// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

// Reusable presets — an instrument's sound or a mixer insert's effect chain,
// saved as composition-friendly TOML so they diff and travel like the rest of a
// Gloopy project. Presets live under <userAppData>/Gloopy/presets/<category>/.

#include "MainComponent.h"
#include "EngineLock.h"
#include "SynthGenerator.h"
#include "SfizzGenerator.h"
#include "Effects.h"
#include "Toml.h"
#include <iostream>

using namespace gloopy;

namespace
{
juce::String presetSlug (const juce::String& name)
{
    juce::String s;
    for (auto c : name.toLowerCase())
        s << (juce::CharacterFunctions::isLetterOrDigit (c) ? juce::String::charToString (c) : juce::String ("-"));
    while (s.contains ("--")) s = s.replace ("--", "-");
    s = s.trimCharactersAtStart ("-").trimCharactersAtEnd ("-");
    return s.isEmpty() ? "preset" : s;
}
}

juce::File MainComponent::presetsDir (const juce::String& category) const
{
    auto base = juce::SystemStats::getEnvironmentVariable ("GLOOPY_PRESET_PATH", {});
    juce::File root = base.isNotEmpty()
        ? juce::File (base)
        : juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Gloopy").getChildFile ("presets");
    return root.getChildFile (category);
}

std::vector<juce::String> MainComponent::apiListPresets (const juce::String& category)
{
    std::vector<juce::String> out;
    for (auto& f : presetsDir (category).findChildFiles (juce::File::findFiles, false, "*.toml"))
        out.push_back (f.getFileNameWithoutExtension());
    return out;
}

// ── instrument (synth) presets ───────────────────────────────────────────────
bool MainComponent::apiSaveSynthPreset (int trackId, const juce::String& name)
{
    return callOnMessageThread ([&] () -> bool
    {
        Track* t = resolveTrack (trackId);
        auto* sg = t ? dynamic_cast<SynthGenerator*> (t->generator.get()) : nullptr;
        if (sg == nullptr) return false;

        juce::ValueTree s ("SYNTH");
        writeSynthParams (s, sg->engine.params);

        toml::Writer w;
        w.str ("format", "gloopy-preset").str ("type", "synth").str ("name", name).blank();
        w.table ("params");
        for (int p = 0; p < s.getNumProperties(); ++p)
        {
            const auto k = s.getPropertyName (p);
            w.value (k.toString(), s.getProperty (k));
        }
        auto dir = presetsDir ("synth"); dir.createDirectory();
        dir.getChildFile (presetSlug (name) + ".toml").replaceWithText (w.str(), false, false, "\n");
        std::cout << "[preset] saved synth preset '" << name << "'" << std::endl;
        return true;
    });
}

bool MainComponent::apiLoadSynthPreset (int trackId, const juce::String& name)
{
    return callOnMessageThread ([&] () -> bool
    {
        Track* t = resolveTrack (trackId);
        auto* sg = t ? dynamic_cast<SynthGenerator*> (t->generator.get()) : nullptr;
        if (sg == nullptr) return false;

        auto f = presetsDir ("synth").getChildFile (presetSlug (name) + ".toml");
        if (! f.existsAsFile()) return false;
        const auto doc = toml::parse (f.loadFileAsString());
        auto* params = doc.table ("params");
        if (params == nullptr) return false;

        juce::ValueTree s ("SYNTH");
        for (auto& kv : params->raw)
            s.setProperty (kv.first, params->getDouble (kv.first.toRawUTF8()), nullptr);
        readSynthParams (s, sg->engine.params);   // atomic stores — safe live
        std::cout << "[preset] loaded synth preset '" << name << "' onto track " << trackId << std::endl;
        return true;
    });
}

// ── general instrument presets (synth or SFZ) ────────────────────────────────
bool MainComponent::apiSaveInstrumentPreset (int trackId, const juce::String& name)
{
    return callOnMessageThread ([&] () -> bool
    {
        Track* t = resolveTrack (trackId);
        if (t == nullptr || t->generator == nullptr) return false;

        toml::Writer w;
        w.str ("format", "gloopy-preset").str ("type", "instrument").str ("name", name).blank();

        if (auto* sg = dynamic_cast<SynthGenerator*> (t->generator.get()))
        {
            w.table ("generator").str ("gen", "synth");
            juce::ValueTree s ("SYNTH"); writeSynthParams (s, sg->engine.params);
            for (int p = 0; p < s.getNumProperties(); ++p)
            { const auto k = s.getPropertyName (p); w.value (k.toString(), s.getProperty (k)); }
        }
        else if (auto* sf = dynamic_cast<SfizzGenerator*> (t->generator.get()))
        {
            w.table ("generator").str ("gen", "sfz").str ("path", portableSamplePath (sf->getSfzPath()));
        }
        else
        {
            std::cout << "[preset] instrument preset: only synth/SFZ tracks supported" << std::endl;
            return false;   // sampler/plugin instrument presets are a follow-up
        }

        auto dir = presetsDir ("instrument"); dir.createDirectory();
        dir.getChildFile (presetSlug (name) + ".toml").replaceWithText (w.str(), false, false, "\n");
        std::cout << "[preset] saved instrument preset '" << name << "'" << std::endl;
        return true;
    });
}

bool MainComponent::apiLoadInstrumentPreset (int trackId, const juce::String& name)
{
    return callOnMessageThread ([&] () -> bool
    {
        Track* t = resolveTrack (trackId);
        if (t == nullptr) return false;
        auto f = presetsDir ("instrument").getChildFile (presetSlug (name) + ".toml");
        if (! f.existsAsFile()) return false;
        const auto doc = toml::parse (f.loadFileAsString());
        auto* g = doc.table ("generator");
        if (g == nullptr) return false;

        // Build the new generator outside the lock, then swap it in.
        std::unique_ptr<Generator> gen;
        const auto kind = g->getString ("gen");
        if (kind == "synth")
        {
            auto sg = std::make_unique<SynthGenerator>();
            juce::ValueTree s ("SYNTH");
            for (auto& kv : g->raw)
                if (kv.first != "gen") s.setProperty (kv.first, g->getDouble (kv.first.toRawUTF8()), nullptr);
            readSynthParams (s, sg->engine.params);
            gen = std::move (sg);
        }
        else if (kind == "sfz")
        {
            auto sf = std::make_unique<SfizzGenerator>();
            sf->prepare (currentSampleRate, currentBlockSize);
            juce::String err;
            if (! sf->loadSfz (resolveSamplePath (g->getString ("path")), err))
            { std::cout << "[preset] SFZ load failed: " << err << std::endl; return false; }
            gen = std::move (sf);
        }
        else return false;

        gen->prepare (currentSampleRate, currentBlockSize);
        pushUndoSnapshot();
        { GLOOPY_ELOCK(sl); t->generator = std::move (gen); }
        emitChange ("track_changed", trackId);
        if (arrangeView) arrangeView->rebuild();
        std::cout << "[preset] loaded instrument preset '" << name << "' (" << kind << ") onto track " << trackId << std::endl;
        return true;
    });
}

// ── effect-chain presets (built-in effects) ──────────────────────────────────
bool MainComponent::apiSaveEffectPreset (int insert, const juce::String& name)
{
    return callOnMessageThread ([&] () -> bool
    {
        if (! juce::isPositiveAndBelow (insert, (int) mixerTracks.size())) return false;
        toml::Writer w;
        w.str ("format", "gloopy-preset").str ("type", "effect-chain").str ("name", name).blank();
        int builtins = 0;
        {
            GLOOPY_ELOCK(sl);
            for (auto& fx : mixerTracks[(size_t) insert]->effects)
            {
                if (fx->getPluginInstance() != nullptr) continue;   // plugin effects skipped in presets
                w.arrayItem ("effects").str ("type", fx->name()).boolean ("bypassed", fx->bypassed.load());
                for (auto& pr : fx->parameters())
                    w.number (pr.name, pr.get());
                w.blank();
                ++builtins;
            }
        }
        auto dir = presetsDir ("effects"); dir.createDirectory();
        dir.getChildFile (presetSlug (name) + ".toml").replaceWithText (w.str(), false, false, "\n");
        std::cout << "[preset] saved effect-chain preset '" << name << "' (" << builtins << " built-in fx)" << std::endl;
        return true;
    });
}

bool MainComponent::apiLoadEffectPreset (int insert, const juce::String& name)
{
    return callOnMessageThread ([&] () -> bool
    {
        if (! juce::isPositiveAndBelow (insert, (int) mixerTracks.size())) return false;
        auto f = presetsDir ("effects").getChildFile (presetSlug (name) + ".toml");
        if (! f.existsAsFile()) return false;
        const auto doc = toml::parse (f.loadFileAsString());
        auto* effects = doc.array ("effects");
        if (effects == nullptr) return false;

        GLOOPY_ELOCK(sl);
        auto& chain = mixerTracks[(size_t) insert]->effects;
        chain.clear();
        for (auto& e : *effects)
        {
            auto fx = makeEffect (e.getString ("type"));
            if (fx == nullptr) continue;
            fx->bypassed.store (e.getBool ("bypassed"));
            auto params = fx->parameters();
            for (auto& kv : e.raw)
                if (kv.first != "type" && kv.first != "bypassed")
                    for (auto& pr : params) if (pr.name == kv.first) { pr.set ((float) e.getDouble (kv.first.toRawUTF8())); break; }
            chain.push_back (std::move (fx));
        }
        std::cout << "[preset] loaded effect-chain preset '" << name << "' onto insert " << insert << std::endl;
        return true;
    });
}
