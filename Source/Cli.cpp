// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

// Headless composition utilities — the read-only half of the `gloopy <cmd>`
// command line (dispatch + pack live in Main.cpp). These reuse the same runtime
// model the GUI and gRPC surface use (loaded via openProjectFile), and emit stable
// JSON on stdout so scripts, CI, and agents can consume a composition without the
// control API. Run in headless-CLI mode (no OSC/gRPC/audio), so direct model
// access here is single-threaded and lock-free.

#include "MainComponent.h"
#include "SfizzGenerator.h"

namespace
{
juce::var obj (juce::DynamicObject::Ptr o) { return juce::var (o.get()); }
}

juce::String MainComponent::apiInspectJson()
{
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    // For a composition, currentProjectFile is the manifest (gloopy.toml) — the
    // title is the containing directory; for a .gloopy file it's the file's name.
    const auto title = currentProjectFile.getFileName() == "gloopy.toml"
                         ? currentProjectFile.getParentDirectory().getFileName()
                         : currentProjectFile.getFileNameWithoutExtension();
    root->setProperty ("title", title);
    root->setProperty ("bpm", transport.getBpm());

    juce::Array<juce::var> tarr;
    for (auto& t : tracks)
    {
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty ("id", t->id);
        o->setProperty ("name", t->name);
        o->setProperty ("type", t->type == TrackType::Instrument ? "instrument" : "audio");
        o->setProperty ("clips", (int) t->clips.size());
        if (auto* sf = dynamic_cast<SfizzGenerator*> (t->generator.get()))
            o->setProperty ("sfz", portableSamplePath (sf->getSfzPath()));
        tarr.add (obj (o));
    }
    root->setProperty ("tracks", tarr);

    juce::Array<juce::var> iarr;
    for (size_t i = 0; i < mixerTracks.size(); ++i)
    {
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty ("index", (int) i);
        o->setProperty ("name", mixerTracks[i]->name);
        o->setProperty ("effects", (int) mixerTracks[i]->effects.size());
        iarr.add (obj (o));
    }
    root->setProperty ("inserts", iarr);

    juce::Array<juce::var> larr;
    for (auto& l : locations)
    {
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty ("name", l.name);   o->setProperty ("kind", l.kind);
        o->setProperty ("start", l.startBeat); o->setProperty ("end", l.endBeat);
        larr.add (obj (o));
    }
    root->setProperty ("locations", larr);

    juce::Array<juce::var> earr;
    for (auto& e : exportProfiles)
    {
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty ("name", e.name); o->setProperty ("target", e.target);
        earr.add (obj (o));
    }
    root->setProperty ("exports", earr);

    return juce::JSON::toString (obj (root));
}

juce::String MainComponent::apiValidateJson (bool& ok)
{
    juce::Array<juce::var> errors, warnings;

    for (auto& t : tracks)
    {
        if (t->type == TrackType::Instrument && t->clips.empty())
            warnings.add ("track '" + t->name + "' has no clips");

        if (auto* sf = dynamic_cast<SfizzGenerator*> (t->generator.get()))
        {
            const auto raw = sf->getSfzPath();
            if (raw.isNotEmpty() && ! resolveSamplePath (raw).existsAsFile())
                errors.add ("track '" + t->name + "': SFZ not found: " + raw);
        }
    }

    // Export profiles that target a named range must resolve to a real range.
    for (auto& e : exportProfiles)
        if (e.target == "range")
        {
            double s, en;
            if (! apiResolveRange (e.rangeName, s, en))
                errors.add ("export '" + e.name + "': range '" + e.rangeName + "' is not a valid range");
        }

    for (auto& l : locations)
        if ((l.kind == "range" || l.kind == "section") && l.endBeat <= l.startBeat)
            warnings.add ("location '" + l.name + "' (" + l.kind + ") is zero-length");

    ok = errors.isEmpty();
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty ("ok", ok);
    root->setProperty ("errors", errors);
    root->setProperty ("warnings", warnings);
    return juce::JSON::toString (obj (root));
}
