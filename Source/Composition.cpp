// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

// Phase 1 of the "composition as repo" format (docs: gloop-compositions.md).
// A composition directory is an alternative *serialisation* of the same ValueTree
// that toValueTree()/loadFromTree() already use, so this file only implements the
// ValueTree <-> directory mapping and leaves the runtime<->tree mapping untouched:
//   saveComposition: toValueTree()  -> write TOML/notes/points/asset files
//   loadComposition: read those files -> ValueTree -> loadFromTree()
// The directory is the canonical project format (no XML). A single-file `.gloopy` is a
// zip of this folder (saveCompositionZip writes it; loadComposition unzips + reads it).

#include "MainComponent.h"
#include "Toml.h"
#include <iostream>
#include <set>
#include <map>
#include <vector>

using namespace gloopy;

namespace
{
// ── slugs ───────────────────────────────────────────────────────────────────
juce::String slugify (const juce::String& name, const juce::String& fallback)
{
    juce::String s;
    for (auto c : name.toLowerCase())
        s << (juce::CharacterFunctions::isLetterOrDigit (c) ? juce::String::charToString (c)
                                                            : juce::String ("-"));
    while (s.contains ("--")) s = s.replace ("--", "-");
    s = s.trimCharactersAtStart ("-").trimCharactersAtEnd ("-");
    return s.isEmpty() ? fallback : s;
}

juce::String uniqueSlug (const juce::String& base, std::map<juce::String, int>& seen)
{
    juce::String out = base;
    for (int n = 2; seen.count (out); ++n) out = base + "-" + juce::String (n);
    seen[out] = 1;
    return out;
}

// ── dirty-aware writer ──────────────────────────────────────────────────────
// Writes files only when their content actually differs from what's on disk, and
// prunes managed files that are no longer produced. So a no-op save touches
// nothing, and moving one fader rewrites exactly one file.
struct SaveCtx
{
    juce::File dir;
    std::set<juce::String> kept;
    int written = 0, pruned = 0;

    void keep (const juce::String& rel) { kept.insert (dir.getChildFile (rel).getRelativePathFrom (dir)); }

    void writeText (const juce::String& rel, const juce::String& text)
    {
        keep (rel);
        auto f = dir.getChildFile (rel);
        if (f.existsAsFile() && f.loadFileAsString() == text) return;
        f.getParentDirectory().createDirectory();
        f.replaceWithText (text, false, false, "\n");   // Unix newlines; keeps content == text
        ++written;
    }
    void writeBytes (const juce::String& rel, const juce::MemoryBlock& data)
    {
        keep (rel);
        auto f = dir.getChildFile (rel);
        if (f.existsAsFile())
        {
            juce::MemoryBlock cur; f.loadFileAsData (cur);
            if (cur == data) return;
        }
        f.getParentDirectory().createDirectory();
        f.replaceWithData (data.getData(), data.getSize());
        ++written;
    }
    void prune()
    {
        for (auto* sub : { "tracks", "clips", "mixer", "automation", "assets", "plugins" })
        {
            auto d = dir.getChildFile (sub);
            if (! d.isDirectory()) continue;
            for (auto& f : d.findChildFiles (juce::File::findFiles, true))
                if (! kept.count (f.getRelativePathFrom (dir))) { f.deleteFile(); ++pruned; }
            for (auto& sd : d.findChildFiles (juce::File::findDirectories, true))
                if (sd.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0) sd.deleteRecursively();
        }
    }
};

juce::MemoryBlock decodeBase64 (const juce::String& b64)
{
    juce::MemoryOutputStream os; juce::Base64::convertFromBase64 (os, b64); return os.getMemoryBlock();
}

// planar float base64 (the layout toValueTree writes) -> deterministic WAV bytes
juce::MemoryBlock buildWav (const juce::String& b64, int channels, int frames, double rate)
{
    const auto mb = decodeBase64 (b64);
    const auto* src = (const float*) mb.getData();
    const size_t count = mb.getSize() / sizeof (float);
    juce::AudioBuffer<float> buf (juce::jmax (1, channels), juce::jmax (0, frames));
    buf.clear();
    size_t idx = 0;
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < frames; ++i)
            if (idx < count) buf.setSample (ch, i, src[idx++]);

    juce::MemoryBlock out;
    juce::WavAudioFormat fmt;
    if (auto* w = fmt.createWriterFor (new juce::MemoryOutputStream (out, false),
                                       rate, (unsigned) buf.getNumChannels(), 32, {}, 0))
    {
        std::unique_ptr<juce::AudioFormatWriter> writer (w);
        writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    }
    return out;
}

juce::String wavToBase64 (const juce::File& wav, juce::AudioFormatManager& fm,
                          int& channels, int& frames, double& rate)
{
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (wav));
    if (r == nullptr) { channels = 1; frames = 0; rate = 44100.0; return {}; }
    channels = (int) r->numChannels; frames = (int) r->lengthInSamples; rate = r->sampleRate;
    juce::AudioBuffer<float> buf (channels, frames);
    r->read (&buf, 0, frames, 0, true, true);
    juce::MemoryBlock mb ((size_t) channels * (size_t) frames * sizeof (float));
    auto* dst = (float*) mb.getData();
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < frames; ++i) *dst++ = buf.getSample (ch, i);
    return juce::Base64::toBase64 (mb.getData(), mb.getSize());
}

juce::String readBinaryAsBase64 (const juce::File& f)
{
    juce::MemoryBlock mb; f.loadFileAsData (mb); return mb.toBase64Encoding();
}

// ── notes / points line files ───────────────────────────────────────────────
juce::String buildNotes (const juce::ValueTree& clip)
{
    std::vector<juce::ValueTree> notes;
    for (int i = 0; i < clip.getNumChildren(); ++i)
        if (clip.getChild (i).hasType ("NOTE")) notes.push_back (clip.getChild (i));
    std::sort (notes.begin(), notes.end(), [] (const juce::ValueTree& a, const juce::ValueTree& b)
    {
        const double sa = a.getProperty ("start"), sb = b.getProperty ("start");
        if (sa != sb) return sa < sb;
        const int pa = a.getProperty ("pitch"), pb = b.getProperty ("pitch");
        if (pa != pb) return pa < pb;
        return (double) a.getProperty ("nlen") < (double) b.getProperty ("nlen");
    });
    juce::String out ("# pitch\tstart\tlength\tvelocity\tprobability\n");
    for (auto& n : notes)
        out << (int) n.getProperty ("pitch") << "\t"
            << toml::Writer::num (n.getProperty ("start")) << "\t"
            << toml::Writer::num (n.getProperty ("nlen"))  << "\t"
            << toml::Writer::num (n.getProperty ("vel"))   << "\t"
            << toml::Writer::num (n.getProperty ("prob", 1.0)) << "\n";
    return out;
}

void readNotes (const juce::File& f, juce::ValueTree& clip)
{
    for (auto& line : juce::StringArray::fromLines (f.loadFileAsString()))
    {
        const auto t = line.trim();
        if (t.isEmpty() || t.startsWithChar ('#')) continue;
        auto cols = juce::StringArray::fromTokens (t, " \t", "");
        cols.removeEmptyStrings();
        if (cols.size() < 4) continue;
        juce::ValueTree n ("NOTE");
        n.setProperty ("pitch", cols[0].getIntValue(), nullptr);
        n.setProperty ("start", cols[1].getDoubleValue(), nullptr);
        n.setProperty ("nlen",  cols[2].getDoubleValue(), nullptr);
        n.setProperty ("vel",   (float) cols[3].getDoubleValue(), nullptr);
        if (cols.size() >= 5) n.setProperty ("prob", (float) cols[4].getDoubleValue(), nullptr);   // optional 5th col
        clip.addChild (n, -1, nullptr);
    }
}

juce::String buildPoints (const juce::ValueTree& lane)
{
    juce::String out ("# beat\tvalue\n");
    for (int i = 0; i < lane.getNumChildren(); ++i)
    {
        auto pt = lane.getChild (i);
        if (! pt.hasType ("PT")) continue;
        out << toml::Writer::num (pt.getProperty ("beat")) << "\t"
            << toml::Writer::num (pt.getProperty ("value")) << "\n";
    }
    return out;
}

void readPoints (const juce::File& f, juce::ValueTree& lane)
{
    for (auto& line : juce::StringArray::fromLines (f.loadFileAsString()))
    {
        const auto t = line.trim();
        if (t.isEmpty() || t.startsWithChar ('#')) continue;
        auto cols = juce::StringArray::fromTokens (t, " \t", "");
        cols.removeEmptyStrings();
        if (cols.size() < 2) continue;
        juce::ValueTree pt ("PT");
        pt.setProperty ("beat",  cols[0].getDoubleValue(), nullptr);
        pt.setProperty ("value", (float) cols[1].getDoubleValue(), nullptr);
        lane.addChild (pt, -1, nullptr);
    }
}

// Ignore installed environments + caches, NOT authored sources: scripts/ and the lockfiles
// (requirements/uv.lock, ocicl systems.csv) are committed so a clone regenerates; the heavy,
// platform-specific installed trees (.venv/, ocicl systems/) are rebuilt from them. (RFC.)
const char* kGitignore =
    ".gloopy-cache/\nexports/\nassets/recordings/raw/\n*.wav.tmp\nplugins/scans/\n"
    ".venv/\nsystems/\n__pycache__/\n.ipynb_checkpoints/\n";
} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// SAVE : runtime -> ValueTree -> directory
// ═════════════════════════════════════════════════════════════════════════════
bool MainComponent::saveComposition (const juce::File& dir)
{
    const auto root = toValueTree();   // reuse the canonical serialisation

    dir.createDirectory();
    SaveCtx ctx { dir };               // writes only changed files; prunes stale ones

    // Title is the composition's own directory name, so it's stable across a
    // dir -> runtime -> dir round-trip (independent of currentProjectFile).
    const auto title = dir.getFileName();

    // --- manifest ---
    toml::Writer man;
    man.str ("format", "gloopy-composition").integer ("version", 1)
       .str ("title", title.isEmpty() ? "Untitled" : title)
       .number ("bpm", root.getProperty ("bpm"))
       .integer ("ts_num", (int) root.getProperty ("tsnum", 4))
       .integer ("ts_den", (int) root.getProperty ("tsden", 4))
       .number ("swing", root.getProperty ("swing", 0.5))
       .number ("launch_quantum", root.getProperty ("launchQuantum", 4.0));
    man.integer ("scale_root", (int) root.getProperty ("scaleRoot", 0))
       .str ("scale_name", root.getProperty ("scaleName", "chromatic").toString());
    { juce::StringArray iv;
      for (auto& s : juce::StringArray::fromTokens (root.getProperty ("scaleIntervals").toString(), ",", ""))
          if (s.trim().isNotEmpty()) iv.add (s.trim());
      man.strArray ("scale_intervals", iv); }
    if (root.hasProperty ("tuningCents"))              // microtuning: 12 cents offsets from ET
    {
        juce::StringArray tc;
        for (auto& s : juce::StringArray::fromTokens (root.getProperty ("tuningCents").toString(), ",", ""))
            if (s.trim().isNotEmpty()) tc.add (s.trim());
        if (! tc.isEmpty()) man.strArray ("tuning_cents", tc);
    }
    man.blank();

    // Slug the mixer inserts first so tracks can reference them by slug.
    auto mixer = root.getChildWithName ("MIXER");
    std::map<juce::String, int> insSeen;
    juce::StringArray insertSlug;                       // index -> slug
    for (int i = 0; i < mixer.getNumChildren(); ++i)
    {
        auto mt = mixer.getChild (i);
        insertSlug.add (uniqueSlug (slugify (mt.getProperty ("name").toString(), "ins-" + juce::String (i)), insSeen));
    }

    auto tracks = root.getChildWithName ("TRACKS");
    std::map<juce::String, int> trkSeen;
    juce::StringArray trackSlug;
    for (int i = 0; i < tracks.getNumChildren(); ++i)
        trackSlug.add (uniqueSlug (slugify (tracks.getChild (i).getProperty ("name").toString(),
                                            "track-" + juce::String (i)), trkSeen));

    for (int i = 0; i < tracks.getNumChildren(); ++i)
    {
        man.arrayItem ("tracks").str ("id", trackSlug[i])
           .str ("file", "tracks/" + trackSlug[i] + ".toml");
    }
    man.blank().table ("mixer").str ("file", "mixer/inserts.toml");
    man.blank().table ("automation").str ("file", "automation/lanes.toml");
    man.blank().table ("locations").str ("file", "locations.toml");
    man.blank().table ("exports").str ("file", "exports.toml");
    man.blank().table ("scenes").str ("file", "scenes.toml");
    man.blank().table ("session").str ("file", "session.toml");   // session-view scene rows
    man.blank().table ("groups").str ("file", "groups.toml");
    man.blank().table ("params").str ("file", "params.toml");
    man.blank().table ("mods").str ("file", "mods.toml");
    man.blank().table ("tempo").str ("file", "tempo.toml");
    man.blank().table ("timesig").str ("file", "timesig.toml");
    man.blank().table ("controllers").str ("file", "controllers.toml");
    man.blank().table ("notes").str ("file", "notes.md");
    ctx.writeText ("gloopy.toml", man.str());
    ctx.writeText ("notes.md", root.getProperty ("notes", "").toString());   // free-form markdown

    // --- per-track files ---
    for (int i = 0; i < tracks.getNumChildren(); ++i)
    {
        auto tr = tracks.getChild (i);
        const auto slug = trackSlug[i];
        toml::Writer w;
        w.str ("id", slug).integer ("tid", (int) tr.getProperty ("tid", -1))   // stable control-API id
         .str ("name", tr.getProperty ("name").toString())
         .integer ("type", (int) tr.getProperty ("type", 0))
         .integer ("colour", (juce::int64) (int) tr.getProperty ("colour", 0))
         .integer ("default_pitch", (int) tr.getProperty ("pitch", 60))
         .number ("volume", tr.getProperty ("vol", 0.8))
         .number ("pan", tr.getProperty ("pan", 0.0))
         .boolean ("mute", tr.getProperty ("mute", false))
         .boolean ("solo", tr.getProperty ("solo", false));
        const int mtIdx = (int) tr.getProperty ("mixerTrack", 0);
        w.str ("mixer_insert", juce::isPositiveAndBelow (mtIdx, insertSlug.size()) ? insertSlug[mtIdx] : juce::String (mtIdx));
        if ((bool) tr.getProperty ("polarity", false)) w.boolean ("polarity", true);   // phase invert
        // Live arpeggiator (per-track MIDI effect) — omit when off.
        if ((bool) tr.getProperty ("arpOn", false))
        {
            w.boolean ("arp", true)
             .number  ("arp_rate",    (double) tr.getProperty ("arpRate", 0.25))
             .integer ("arp_octaves", (int)    tr.getProperty ("arpOct", 1))
             .number  ("arp_gate",    (double) tr.getProperty ("arpGate", 0.5))
             .integer ("arp_mode",    (int)    tr.getProperty ("arpMode", 0))
             .number  ("arp_swing",   (double) tr.getProperty ("arpSwing", 0.5))
             .boolean ("arp_hold",    (bool)   tr.getProperty ("arpHold", false));
            if ((float) (double) tr.getProperty ("arpProb", 1.0) < 1.0f)
                w.number ("arp_probability", (double) tr.getProperty ("arpProb", 1.0));
        }
        w.blank();

        // generator subtable
        w.table ("generator");
        if (auto s = tr.getChildWithName ("SYNTH"); s.isValid())
        {
            w.str ("type", "synth");
            for (int p = 0; p < s.getNumProperties(); ++p)
            {
                const auto name = s.getPropertyName (p);
                w.value (name.toString(), s.getProperty (name));
            }
        }
        else if (auto sf = tr.getChildWithName ("SFZ"); sf.isValid())
        {
            w.str ("type", "sfz").str ("path", sf.getProperty ("path").toString());
        }
        else if (auto sm = tr.getChildWithName ("SAMPLE"); sm.isValid())
        {
            const auto rel = "assets/samples/" + slug + ".wav";
            ctx.writeBytes (rel, buildWav (sm.getProperty ("data").toString(),
                                           (int) sm.getProperty ("channels", 1), (int) sm.getProperty ("frames", 0),
                                           (double) sm.getProperty ("rate", 44100.0)));
            w.str ("type", "sampler").str ("sample_file", rel)
             .integer ("root", (int) sm.getProperty ("root", 60))
             .number ("start", (double) sm.getProperty ("sstart", 0.0))
             .number ("end", (double) sm.getProperty ("send", 1.0))
             .boolean ("reverse", (bool) sm.getProperty ("srev", false))
             .number ("fade_in", (double) sm.getProperty ("sfadein", 0.0))
             .number ("fade_out", (double) sm.getProperty ("sfadeout", 0.0))
             .boolean ("loop", (bool) sm.getProperty ("sloop", false))
             .boolean ("mono", (bool) sm.getProperty ("smono", false))
             .number ("loop_xfade", (double) sm.getProperty ("sloopxf", 0.0))
             .number ("interp", (double) (int) sm.getProperty ("sinterp", 0))
             .str ("sample_name", sm.getProperty ("sname").toString())
             .number ("rate", sm.getProperty ("rate", 44100.0));
        }
        else if (auto kit = tr.getChildWithName ("KIT"); kit.isValid())
        {
            // A multi-pad DrumKit: one [[generator.pad]] per voice, each pad's sample
            // written to assets/samples/ so the composition stays self-contained.
            w.str ("type", "drumkit");
            for (int pi = 0; pi < kit.getNumChildren(); ++pi)
            {
                auto pad = kit.getChild (pi);
                if (! pad.hasType ("PAD")) continue;
                const auto padslug = slugify (pad.getProperty ("padname").toString(), "pad-" + juce::String (pi));
                const auto rel = "assets/samples/" + slug + "-" + padslug + ".wav";
                ctx.writeBytes (rel, buildWav (pad.getProperty ("data").toString(),
                                               (int) pad.getProperty ("channels", 1), (int) pad.getProperty ("frames", 0),
                                               (double) pad.getProperty ("rate", 44100.0)));
                w.blank().arrayItem ("generator.pad")
                 .str ("name", pad.getProperty ("padname").toString())
                 .integer ("note", (int) pad.getProperty ("note", 36))
                 .integer ("colour", (int) pad.getProperty ("padcol", (int) juce::Colours::orangered.getARGB()))
                 .str ("sample_file", rel)
                 .integer ("root", (int) pad.getProperty ("root", (int) pad.getProperty ("note", 36)))
                 .number ("start", (double) pad.getProperty ("sstart", 0.0))
                 .number ("end", (double) pad.getProperty ("send", 1.0))
                 .boolean ("reverse", (bool) pad.getProperty ("srev", false))
                 .number ("fade_in", (double) pad.getProperty ("sfadein", 0.0))
                 .number ("fade_out", (double) pad.getProperty ("sfadeout", 0.0))
                 .boolean ("loop", (bool) pad.getProperty ("sloop", false))
                 .boolean ("mono", (bool) pad.getProperty ("smono", false))
                 .number ("loop_xfade", (double) pad.getProperty ("sloopxf", 0.0))
                 .integer ("interp", (int) pad.getProperty ("sinterp", 0))
                 .str ("sample_name", pad.getProperty ("sname").toString());
            }
        }
        else if (auto pl = tr.getChildWithName ("PLUGIN"); pl.isValid())
        {
            ctx.writeBytes ("plugins/state/" + slug + ".bin", decodeBase64 (pl.getProperty ("pstate").toString()));
            ctx.writeText ("plugins/state/" + slug + ".desc.xml", pl.getProperty ("pdesc").toString());
            w.str ("type", "plugin").str ("state_file", "plugins/state/" + slug + ".bin")
             .str ("desc_file", "plugins/state/" + slug + ".desc.xml");
        }
        else
        {
            w.str ("type", "none");
        }

        // clips
        std::map<juce::String, int> clipSeen;
        for (int c = 0; c < tr.getNumChildren(); ++c)
        {
            auto cl = tr.getChild (c);
            if (! cl.hasType ("CLIP")) continue;
            const auto cslug = uniqueSlug (slugify (cl.getProperty ("name").toString(), "clip-" + juce::String (c)), clipSeen);
            w.blank().arrayItem ("clips")
             .str ("id", cslug).str ("name", cl.getProperty ("name").toString())
             .integer ("ctype", (int) cl.getProperty ("ctype", 0))
             .number ("start", cl.getProperty ("start", 0.0))
             .number ("length", cl.getProperty ("len", 4.0))
             .number ("content_length", cl.getProperty ("content", 4.0))
             .boolean ("looped", cl.getProperty ("looped", false));
            if ((int) cl.getProperty ("transpose", 0) != 0) w.integer ("transpose", (int) cl.getProperty ("transpose", 0));
            if ((float) (double) cl.getProperty ("velscale", 1.0) != 1.0f) w.number ("velocity_scale", (double) cl.getProperty ("velscale", 1.0));
            if ((bool) cl.getProperty ("muted", false)) w.boolean ("muted", true);
            if ((double) cl.getProperty ("fadein", 0.0)  > 0.0) w.number ("fade_in",  cl.getProperty ("fadein", 0.0));
            if ((double) cl.getProperty ("fadeout", 0.0) > 0.0) w.number ("fade_out", cl.getProperty ("fadeout", 0.0));
            if ((int) cl.getProperty ("fadeshape", 0) != 0) w.number ("fade_shape", (double) (int) cl.getProperty ("fadeshape", 0));
            if ((int) cl.getProperty ("colour", 0) != 0) w.integer ("colour", (juce::int64) (int) cl.getProperty ("colour", 0));   // per-clip colour override
            if (cl.hasProperty ("script") || cl.hasProperty ("generator"))   // script clip (notes below are cached output)
            {
                if (cl.hasProperty ("script"))   // file-based generator
                {
                    // Make the composition self-contained: copy the generator source INTO the project
                    // (scripts/<name>) and store a project-relative path, so a clone regenerates.
                    // Resolves against the current project dir, so an untitled session's scratch script
                    // and a legacy absolute path both get pulled in. (RFC project-workflow.md, slices 1-3.)
                    const auto src     = cl.getProperty ("script").toString();
                    const auto srcFile = resolveScriptFile (src);
                    juce::String rel   = src;
                    if (srcFile.existsAsFile())
                    {
                        rel = "scripts/" + srcFile.getFileName();
                        const auto dest = dir.getChildFile (rel);
                        if (dest != srcFile) { dest.getParentDirectory().createDirectory(); srcFile.copyFileTo (dest); }
                    }
                    w.str ("script", rel);
                    ctx.keep (rel);          // don't prune the in-project script source file
                }
                if (cl.hasProperty ("generator"))    // named generator in the project's system/module
                {
                    w.str ("generator", cl.getProperty ("generator").toString());
                    if (cl.hasProperty ("scriptsystem")) w.str ("system", cl.getProperty ("scriptsystem").toString());
                }
                if (cl.hasProperty ("scriptlang")) w.str ("script_lang", cl.getProperty ("scriptlang").toString());
                if ((juce::int64) cl.getProperty ("scriptseed", (juce::int64) 0) != 0)
                    w.integer ("script_seed", (juce::int64) cl.getProperty ("scriptseed", (juce::int64) 0));
            }

            if (cl.hasProperty ("afile"))    // referenced audio (recorded take / import)
            {
                const auto ref = cl.getProperty ("afile").toString();
                w.str ("audio_file", ref).number ("audio_gain", cl.getProperty ("again", 1.0));
                if (cl.hasProperty ("take")) w.str ("take", cl.getProperty ("take").toString());
                ctx.keep (ref);              // don't prune the referenced take/asset
            }
            else if (cl.hasProperty ("adata"))   // embedded audio -> wav sidecar
            {
                const auto rel = "assets/audio/" + slug + "-" + cslug + ".wav";
                ctx.writeBytes (rel, buildWav (cl.getProperty ("adata").toString(),
                                               (int) cl.getProperty ("achannels", 1), (int) cl.getProperty ("aframes", 0),
                                               (double) cl.getProperty ("arate", 44100.0)));
                w.str ("audio_file", rel).number ("audio_rate", cl.getProperty ("arate", 44100.0))
                 .number ("audio_gain", cl.getProperty ("again", 1.0));
            }
            else                              // MIDI clip -> .notes sidecar
            {
                const auto rel = "clips/" + slug + "/" + cslug + ".notes";
                ctx.writeText (rel, buildNotes (cl));
                w.str ("notes", rel);
            }
        }

        // Session-view slots (SCLIP children), one [[session_clips]] item each, tagged with scene.
        std::map<juce::String, int> sclipSeen;
        for (int c = 0; c < tr.getNumChildren(); ++c)
        {
            auto cl = tr.getChild (c);
            if (! cl.hasType ("SCLIP")) continue;
            const int scene = (int) cl.getProperty ("scene", -1);
            const auto cslug = uniqueSlug (slugify (cl.getProperty ("name").toString(), "s" + juce::String (scene)), sclipSeen);
            w.blank().arrayItem ("session_clips")
             .integer ("scene", scene).str ("name", cl.getProperty ("name").toString())
             .integer ("ctype", (int) cl.getProperty ("ctype", 0))
             .number ("length", cl.getProperty ("len", 4.0))
             .number ("content_length", cl.getProperty ("content", 4.0))
             .boolean ("looped", cl.getProperty ("looped", true));
            if ((int) cl.getProperty ("transpose", 0) != 0) w.integer ("transpose", (int) cl.getProperty ("transpose", 0));
            if ((float) (double) cl.getProperty ("velscale", 1.0) != 1.0f) w.number ("velocity_scale", (double) cl.getProperty ("velscale", 1.0));
            if ((bool) cl.getProperty ("muted", false)) w.boolean ("muted", true);
            if ((double) cl.getProperty ("fadein", 0.0)  > 0.0) w.number ("fade_in",  cl.getProperty ("fadein", 0.0));
            if ((double) cl.getProperty ("fadeout", 0.0) > 0.0) w.number ("fade_out", cl.getProperty ("fadeout", 0.0));
            if ((int) cl.getProperty ("fadeshape", 0) != 0) w.number ("fade_shape", (double) (int) cl.getProperty ("fadeshape", 0));
            if ((int) cl.getProperty ("colour", 0) != 0) w.integer ("colour", (juce::int64) (int) cl.getProperty ("colour", 0));
            if (cl.hasProperty ("afile"))
            {
                const auto ref = cl.getProperty ("afile").toString();
                w.str ("audio_file", ref).number ("audio_gain", cl.getProperty ("again", 1.0));
                if (cl.hasProperty ("take")) w.str ("take", cl.getProperty ("take").toString());
                ctx.keep (ref);
            }
            else if (cl.hasProperty ("adata"))
            {
                const auto rel = "assets/audio/" + slug + "-session-" + cslug + ".wav";
                ctx.writeBytes (rel, buildWav (cl.getProperty ("adata").toString(),
                                               (int) cl.getProperty ("achannels", 1), (int) cl.getProperty ("aframes", 0),
                                               (double) cl.getProperty ("arate", 44100.0)));
                w.str ("audio_file", rel).number ("audio_rate", cl.getProperty ("arate", 44100.0))
                 .number ("audio_gain", cl.getProperty ("again", 1.0));
            }
            else
            {
                const auto rel = "clips/" + slug + "/session-" + cslug + ".notes";
                ctx.writeText (rel, buildNotes (cl));
                w.str ("notes", rel);
            }
        }
        ctx.writeText ("tracks/" + slug + ".toml", w.str());
    }

    // --- mixer ---
    toml::Writer mw;
    toml::Writer ew;   // effects.toml (flat, keyed by insert index + slot)
    for (int i = 0; i < mixer.getNumChildren(); ++i)
    {
        auto mt = mixer.getChild (i);
        mw.arrayItem ("inserts").str ("id", insertSlug[i]).str ("name", mt.getProperty ("name").toString())
          .number ("volume", mt.getProperty ("vol", 0.8)).number ("pan", mt.getProperty ("pan", 0.0))
          .boolean ("mute", mt.getProperty ("mute", false)).boolean ("solo", mt.getProperty ("solo", false));
        if ((bool) mt.getProperty ("bus", false)) mw.boolean ("bus", true);
        if ((int) mt.getProperty ("out", 0) != 0) mw.integer ("output", (int) mt.getProperty ("out", 0));   // group/bus routing (mixerTracks index)
        if ((bool) mt.getProperty ("fold", false)) mw.boolean ("folded", true);                              // session group collapsed
        if ((int) mt.getProperty ("col", 0) != 0) mw.integer ("colour", (juce::int64) (int) mt.getProperty ("col", 0));   // group colour
        if (mt.getProperty ("group").toString().isNotEmpty()) mw.str ("group", mt.getProperty ("group").toString());
        {
            juce::StringArray sendEnc;   // "busIndex,level[,post]"
            for (int e = 0; e < mt.getNumChildren(); ++e)
            {
                auto sd = mt.getChild (e);
                if (sd.hasType ("SEND"))
                    sendEnc.add (sd.getProperty ("to").toString() + "," + toml::Writer::num ((double) sd.getProperty ("level", 0.0))
                                 + ((bool) sd.getProperty ("post", false) ? ",post" : ""));
            }
            if (! sendEnc.isEmpty()) mw.strArray ("sends", sendEnc);
        }
        mw.blank();
        int slot = 0;
        for (int e = 0; e < mt.getNumChildren(); ++e)
        {
            auto fx = mt.getChild (e);
            if (! fx.hasType ("FX")) continue;
            ew.arrayItem ("effects").integer ("insert", i).integer ("slot", slot++)
              .str ("type", fx.getProperty ("type").toString())
              .boolean ("bypassed", fx.getProperty ("bypass", false));
            if (fx.getProperty ("type").toString() == "Plugin")
            {
                const auto stem = "plugins/state/ins" + juce::String (i) + "-fx" + juce::String (slot - 1);
                ctx.writeBytes (stem + ".bin", decodeBase64 (fx.getProperty ("pstate").toString()));
                ctx.writeText (stem + ".desc.xml", fx.getProperty ("pdesc").toString());
                ew.str ("state_file", stem + ".bin").str ("desc_file", stem + ".desc.xml");
            }
            else
            {
                for (int p = 0; p < fx.getNumChildren(); ++p)
                {
                    auto pr = fx.getChild (p);
                    if (pr.hasType ("PARAM"))
                        ew.number (pr.getProperty ("name").toString(), pr.getProperty ("value", 0.0));
                }
            }
            ew.blank();
        }
    }
    ctx.writeText ("mixer/inserts.toml", mw.str());
    ctx.writeText ("mixer/effects.toml", ew.str());

    // --- automation ---
    toml::Writer aw;
    auto autom = root.getChildWithName ("AUTOMATION");
    std::map<juce::String, int> laneSeen;
    for (int i = 0; i < autom.getNumChildren(); ++i)
    {
        auto lane = autom.getChild (i);
        if (! lane.hasType ("LANE")) continue;
        const juce::String tgt = lane.getProperty ("target").toString();
        auto base = tgt.isNotEmpty() ? slugify (tgt, "lane")
                                     : slugify (lane.getProperty ("param").toString(), "lane") + "-"
                                       + juce::String ((int) lane.getProperty ("id", 0));
        const auto lslug = uniqueSlug (base, laneSeen);
        const auto rel = "automation/" + lslug + ".points";
        ctx.writeText (rel, buildPoints (lane));
        auto& row = aw.arrayItem ("lanes").str ("id", lslug)
          .integer ("type", (int) lane.getProperty ("type", 0))
          .integer ("target_id", (int) lane.getProperty ("id", 0))
          .integer ("slot", (int) lane.getProperty ("slot", 0))
          .str ("param", lane.getProperty ("param").toString());
        if (tgt.isNotEmpty()) row.str ("target", tgt);
        if ((bool) lane.getProperty ("step", false)) row.boolean ("step", true);
        if ((float) (double) lane.getProperty ("curve", 0.0) != 0.0f) row.number ("curve", (double) lane.getProperty ("curve", 0.0));
        row.str ("points", rel).blank();
    }
    ctx.writeText ("automation/lanes.toml", aw.str());

    // --- timeline locations (markers, ranges, sections) ---
    toml::Writer lw;
    auto locs = root.getChildWithName ("LOCATIONS");
    for (int i = 0; i < locs.getNumChildren(); ++i)
    {
        auto l = locs.getChild (i);
        if (! l.hasType ("LOC")) continue;
        lw.arrayItem ("locations").str ("name", l.getProperty ("name").toString())
          .str ("kind", l.getProperty ("kind").toString())
          .number ("start", l.getProperty ("start"))
          .number ("end", l.getProperty ("end")).blank();
    }
    ctx.writeText ("locations.toml", lw.str());

    // --- export profiles (named render targets) ---
    toml::Writer ew2;
    auto exps = root.getChildWithName ("EXPORTS");
    for (int i = 0; i < exps.getNumChildren(); ++i)
    {
        auto e = exps.getChild (i);
        if (! e.hasType ("EXPORT")) continue;
        ew2.arrayItem ("exports").str ("name", e.getProperty ("name").toString())
           .str ("target", e.getProperty ("target").toString())
           .str ("range", e.getProperty ("range").toString())
           .str ("format", e.getProperty ("format").toString())
           .integer ("track", (int) e.getProperty ("track", 0))
           .number ("tail", e.getProperty ("tail")).blank();
    }
    ctx.writeText ("exports.toml", ew2.str());

    // --- mixer scenes (named snapshots) ---
    // Each insert encoded as a "vol,pan,mute,solo,bypassbits" string so the flat
    // TOML writer can hold a variable-length list.
    toml::Writer sw;
    auto scenes = root.getChildWithName ("SCENES");
    for (int i = 0; i < scenes.getNumChildren(); ++i)
    {
        auto sv = scenes.getChild (i);
        if (! sv.hasType ("SCENE")) continue;
        juce::StringArray insEnc;
        for (int j = 0; j < sv.getNumChildren(); ++j)
        {
            auto iv = sv.getChild (j);
            juce::String e;
            e << toml::Writer::num ((double) iv.getProperty ("vol", 0.8)) << ","
              << toml::Writer::num ((double) iv.getProperty ("pan", 0.0)) << ","
              << ((bool) iv.getProperty ("mute") ? "1" : "0") << ","
              << ((bool) iv.getProperty ("solo") ? "1" : "0") << ","
              << iv.getProperty ("bypass").toString() << ","
              << iv.getProperty ("sends").toString();   // "bus:level|bus:level" (may be empty)
            insEnc.add (e);
        }
        sw.arrayItem ("scenes").str ("name", sv.getProperty ("name").toString())
          .strArray ("inserts", insEnc).blank();
    }
    ctx.writeText ("scenes.toml", sw.str());

    // --- session-view scene rows ---
    toml::Writer ssw;
    if (auto sess = root.getChildWithName ("SESSIONSCENES"); sess.isValid())
        for (int i = 0; i < sess.getNumChildren(); ++i)
        {
            auto one = sess.getChild (i);
            if (! one.hasType ("SSCENE")) continue;
            ssw.arrayItem ("scene").str ("name", one.getProperty ("name").toString());
            if (one.hasProperty ("colour")) ssw.integer ("colour", (juce::int64) (int) one.getProperty ("colour", 0));
        }
    ctx.writeText ("session.toml", ssw.str());

    // --- control groups (VCA-lite) ---
    // The group defs; membership is the `group` field on each mixer insert above.
    toml::Writer gw;
    auto groupsT = root.getChildWithName ("GROUPS");
    for (int i = 0; i < groupsT.getNumChildren(); ++i)
    {
        auto gv = groupsT.getChild (i);
        if (! gv.hasType ("GROUP")) continue;
        gw.arrayItem ("groups").str ("name", gv.getProperty ("name").toString())
          .number ("gain", gv.getProperty ("gain", 1.0)).boolean ("mute", gv.getProperty ("mute", false))
          .boolean ("solo", gv.getProperty ("solo", false))
          .blank();
    }
    ctx.writeText ("groups.toml", gw.str());

    // --- universal parameter model snapshot (readable id->value manifest) ---
    // A discoverable record of every non-plugin ParamModel id and its value, so external
    // clients can read the param model from the repo without instantiating plugins. Purely
    // informational: on load, values come from each subsystem's own section, not this file.
    toml::Writer pw;
    for (auto& d : apiSnapshotParameters())
        pw.arrayItem ("params").str ("id", d.id).number ("value", d.value)
          .number ("min", d.min).number ("max", d.max)
          .str ("scaling", d.scaling).str ("unit", d.unit).blank();
    ctx.writeText ("params.toml", pw.str());

    // --- modulation matrix (LFO -> param) ---
    toml::Writer mdw;
    auto modsT = root.getChildWithName ("MODS");
    for (int i = 0; i < modsT.getNumChildren(); ++i)
    {
        auto mv = modsT.getChild (i);
        if (! mv.hasType ("MOD")) continue;
        mdw.arrayItem ("mods").str ("target", mv.getProperty ("target").toString())
           .number ("rate", mv.getProperty ("rate", 1.0)).number ("depth", mv.getProperty ("depth", 0.0))
           .number ("center", mv.getProperty ("center", 0.0)).integer ("shape", (int) mv.getProperty ("shape", 0))
           .number ("sync", mv.getProperty ("sync", 0.0))
           .number ("phase", mv.getProperty ("phase", 0.0)).boolean ("unipolar", mv.getProperty ("unipolar", false))
           .number ("slew", mv.getProperty ("slew", 0.0))
           .blank();
    }
    ctx.writeText ("mods.toml", mdw.str());

    // --- tempo map (tempo markers) ---
    toml::Writer tw;
    auto tmap = root.getChildWithName ("TEMPOMAP");
    for (int i = 0; i < tmap.getNumChildren(); ++i)
    {
        auto v = tmap.getChild (i);
        if (! v.hasType ("TM")) continue;
        tw.arrayItem ("markers").number ("beat", v.getProperty ("beat", 0.0))
          .number ("bpm", v.getProperty ("bpm", 120.0)).blank();
    }
    ctx.writeText ("tempo.toml", tw.str());

    // --- time-signature map (mid-song changes) ---
    toml::Writer tsw;
    auto tsmap = root.getChildWithName ("TIMESIGMAP");
    for (int i = 0; i < tsmap.getNumChildren(); ++i)
    {
        auto v = tsmap.getChild (i);
        if (! v.hasType ("TS")) continue;
        tsw.arrayItem ("markers").number ("beat", v.getProperty ("beat", 0.0))
           .number ("num", (int) v.getProperty ("num", 4))
           .number ("den", (int) v.getProperty ("den", 4)).blank();
    }
    ctx.writeText ("timesig.toml", tsw.str());

    // --- controller mappings (source -> ParamModel target) ---
    toml::Writer cw;
    auto ctls = root.getChildWithName ("CONTROLLERS");
    for (int i = 0; i < ctls.getNumChildren(); ++i)
    {
        auto v = ctls.getChild (i);
        if (! v.hasType ("CTRL")) continue;
        cw.arrayItem ("controllers").str ("source", v.getProperty ("source").toString())
          .str ("target", v.getProperty ("target").toString())
          .number ("lo", v.getProperty ("lo", 0.0)).number ("hi", v.getProperty ("hi", 1.0))
          .boolean ("bypass", (bool) v.getProperty ("bypass", false)).blank();
    }
    ctx.writeText ("controllers.toml", cw.str());

    ctx.writeText (".gitignore", kGitignore);
    // A starter README so the folder reads as a reproducible project on any git host — written
    // once, never clobbering a user's own (RFC: song-as-repo).
    if (! dir.getChildFile ("README.md").existsAsFile())
        ctx.writeText ("README.md",
            "# " + dir.getFileName() + "\n\n"
            "A [Gloopy](https://github.com/atgreen/gloopy) composition — a song as a repo.\n\n"
            "Open it:\n\n```sh\ngloopy .\n```\n\n"
            "Clips whose notes are generated by code keep their generators in `scripts/`, so a\n"
            "clone regenerates. Cached notes play without any runtime installed.\n");
    else
        ctx.keep ("README.md");
    ctx.prune();

    std::cout << "[composition] saved " << tracks.getNumChildren() << " tracks to "
              << dir.getFullPathName() << " (" << ctx.written << " written, "
              << ctx.pruned << " pruned)" << std::endl;

    // A composition folder IS a git repo: auto-init it on save, so every project is
    // version-controlled from the start (the composition-as-code north star — "store it in
    // git"). Skipped for throwaway temp dirs — the `.gloopy` zip archive, `gloopy pack`, and
    // the test harness all write under tempDirectory and must not carry a `.git`. `git init`
    // is idempotent, so re-saving an existing repo is a no-op.
    {
        const auto tmpRoot = juce::File::getSpecialLocation (juce::File::tempDirectory);
        juce::String gitVer;
        if (! dir.isAChildOf (tmpRoot) && apiGitAvailable (gitVer) && ! dir.getChildFile (".git").exists())
            apiGitInit (dir.getFullPathName());
    }

    // Opt-in auto-commit-on-save: a no-op unless this repo set gloopy.autocommit=true
    // (one `git config --get`), in which case stage-all + commit the just-saved state.
    apiGitAutoCommitOnSave (dir.getFullPathName());
    markSaved();                        // clear the unsaved flag + refresh the git summary
    return true;
}

// ═════════════════════════════════════════════════════════════════════════════
// LOAD : directory -> ValueTree -> runtime  (read-only)
// ═════════════════════════════════════════════════════════════════════════════
// Save the project as a single-file `.gloopy` archive: a zip of the composition folder.
// This is the shareable one-file form of the directory format (the directory is the default);
// there is no XML any more. loadComposition() reads it back (it unzips to a temp workspace).
bool MainComponent::saveCompositionZip (const juce::File& zipFile)
{
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                 .getChildFile ("gloopy-zip-" + juce::String (juce::Time::getMillisecondCounterHiRes()));
    tmp.deleteRecursively();
    // Wrap the composition in a folder named after the archive, so unzipping yields
    // `<name>/gloopy.toml` (loadComposition finds the manifest at the root or one level down).
    const auto compDir = tmp.getChildFile (zipFile.getFileNameWithoutExtension());
    if (! compDir.createDirectory()) { tmp.deleteRecursively(); return false; }

    const bool saved = saveComposition (compDir);
    if (saved)
    {
        juce::ZipFile::Builder b;
        for (auto& f : compDir.findChildFiles (juce::File::findFiles, true))
            b.addFile (f, 9, f.getRelativePathFrom (tmp));   // relative to tmp -> "<name>/..." inside the zip
        zipFile.deleteFile();
        if (auto os = zipFile.createOutputStream())
            b.writeToStream (*os, nullptr);
        else { tmp.deleteRecursively(); return false; }
    }
    tmp.deleteRecursively();
    return saved && zipFile.existsAsFile();
}

bool MainComponent::loadComposition (const juce::File& pathIn)
{
    juce::File dir = pathIn;

    // A single-file archive (`.gloopy` / `.zip` — a zipped composition): unpack to a
    // read-only temp workspace and load that. The gloopy.toml may sit at the root or
    // inside a single top-level folder.
    if (pathIn.existsAsFile() && pathIn.hasFileExtension ("gloopy;zip"))
    {
        auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                     .getChildFile ("gloopy-comp-" + juce::String (juce::Time::getMillisecondCounter()));
        tmp.deleteRecursively(); tmp.createDirectory();
        juce::ZipFile zf (pathIn);
        if (zf.uncompressTo (tmp, true).failed()) return false;
        dir = tmp;
        if (! dir.getChildFile ("gloopy.toml").existsAsFile())
            for (auto& c : dir.findChildFiles (juce::File::findDirectories, false))
                if (c.getChildFile ("gloopy.toml").existsAsFile()) { dir = c; break; }
    }

    const auto manifest = dir.getChildFile ("gloopy.toml");
    if (! manifest.existsAsFile()) return false;
    const auto man = toml::parse (manifest.loadFileAsString());

    juce::ValueTree root ("GLOOPY");
    root.setProperty ("version", 2, nullptr);
    root.setProperty ("bpm", man.root.getDouble ("bpm", 120.0), nullptr);
    root.setProperty ("tsnum", man.root.getInt ("ts_num", 4), nullptr);
    root.setProperty ("tsden", man.root.getInt ("ts_den", 4), nullptr);
    root.setProperty ("swing", man.root.getDouble ("swing", 0.5), nullptr);
    root.setProperty ("launchQuantum", man.root.getDouble ("launch_quantum", 4.0), nullptr);
    root.setProperty ("notes", dir.getChildFile ("notes.md").loadFileAsString(), nullptr);
    root.setProperty ("scaleRoot", man.root.getInt ("scale_root", 0), nullptr);
    root.setProperty ("scaleName", man.root.getString ("scale_name", "chromatic"), nullptr);
    root.setProperty ("scaleIntervals", man.root.getStringArray ("scale_intervals").joinIntoString (","), nullptr);
    { auto tc = man.root.getStringArray ("tuning_cents");
      if (! tc.isEmpty()) root.setProperty ("tuningCents", tc.joinIntoString (","), nullptr); }

    // Inserts first (tracks reference them by slug) — establishes slug->index.
    juce::ValueTree mixerTree ("MIXER");
    std::map<juce::String, int> insertIndex;
    if (auto insDoc = toml::parse (dir.getChildFile ("mixer/inserts.toml").loadFileAsString());
        auto* inserts = insDoc.array ("inserts"))
    {
        int idx = 0;
        for (auto& in : *inserts)
        {
            insertIndex[in.getString ("id")] = idx++;
            juce::ValueTree mt ("MTRACK");
            mt.setProperty ("name", in.getString ("name"), nullptr);
            mt.setProperty ("vol", in.getDouble ("volume", 0.8), nullptr);
            mt.setProperty ("pan", in.getDouble ("pan", 0.0), nullptr);
            mt.setProperty ("mute", in.getBool ("mute"), nullptr);
            mt.setProperty ("solo", in.getBool ("solo"), nullptr);
            if (in.getBool ("bus")) mt.setProperty ("bus", true, nullptr);
            if (in.getInt ("output", 0) != 0) mt.setProperty ("out", in.getInt ("output", 0), nullptr);   // group/bus routing
            if (in.getBool ("folded")) mt.setProperty ("fold", true, nullptr);                              // session group collapsed
            if (in.getInt ("colour", 0) != 0) mt.setProperty ("col", in.getInt ("colour", 0), nullptr);    // group colour
            if (in.getString ("group").isNotEmpty()) mt.setProperty ("group", in.getString ("group"), nullptr);
            for (auto& enc : in.getStringArray ("sends"))
            {
                auto p = juce::StringArray::fromTokens (enc, ",", "");
                juce::ValueTree sv ("SEND");
                sv.setProperty ("to", p.size() > 0 ? p[0].getIntValue() : 0, nullptr);
                sv.setProperty ("level", p.size() > 1 ? p[1].getDoubleValue() : 0.0, nullptr);
                if (p.size() > 2 && p[2].trim() == "post") sv.setProperty ("post", true, nullptr);
                mt.addChild (sv, -1, nullptr);
            }
            mixerTree.addChild (mt, -1, nullptr);
        }
    }
    // Effects (flat, grouped by insert index + slot).
    if (auto fxDoc = toml::parse (dir.getChildFile ("mixer/effects.toml").loadFileAsString());
        auto* effects = fxDoc.array ("effects"))
    {
        // stable order by (insert, slot)
        std::vector<const toml::Table*> sorted;
        for (auto& e : *effects) sorted.push_back (&e);
        std::sort (sorted.begin(), sorted.end(), [] (auto* a, auto* b)
        {
            if (a->getInt ("insert") != b->getInt ("insert")) return a->getInt ("insert") < b->getInt ("insert");
            return a->getInt ("slot") < b->getInt ("slot");
        });
        for (auto* e : sorted)
        {
            const int ins = e->getInt ("insert");
            if (! juce::isPositiveAndBelow (ins, mixerTree.getNumChildren())) continue;
            juce::ValueTree fx ("FX");
            const auto type = e->getString ("type");
            fx.setProperty ("type", type, nullptr);
            fx.setProperty ("bypass", e->getBool ("bypassed"), nullptr);
            if (type == "Plugin")
            {
                fx.setProperty ("pstate", readBinaryAsBase64 (dir.getChildFile (e->getString ("state_file"))), nullptr);
                fx.setProperty ("pdesc",  dir.getChildFile (e->getString ("desc_file")).loadFileAsString(), nullptr);
            }
            else
            {
                for (auto& kv : e->raw)
                    if (kv.first != "insert" && kv.first != "slot" && kv.first != "type" && kv.first != "bypassed")
                    {
                        juce::ValueTree pv ("PARAM");
                        pv.setProperty ("name", kv.first, nullptr);
                        pv.setProperty ("value", (float) e->getDouble (kv.first.toRawUTF8()), nullptr);
                        fx.addChild (pv, -1, nullptr);
                    }
            }
            mixerTree.getChild (ins).addChild (fx, -1, nullptr);
        }
    }

    // Tracks (in manifest order).
    juce::ValueTree tracksTree ("TRACKS");
    if (auto* trackList = man.array ("tracks"))
        for (auto& tref : *trackList)
        {
            const auto tf = dir.getChildFile (tref.getString ("file"));
            if (! tf.existsAsFile()) continue;
            const auto td = toml::parse (tf.loadFileAsString());

            juce::ValueTree tr ("TRACK");
            tr.setProperty ("tid", td.root.getInt ("tid", -1), nullptr);   // preserve the stable control-API id
            tr.setProperty ("name", td.root.getString ("name"), nullptr);
            tr.setProperty ("colour", td.root.getInt ("colour", (int) 0xff4a90d9), nullptr);
            tr.setProperty ("pitch", td.root.getInt ("default_pitch", 60), nullptr);
            tr.setProperty ("vol", td.root.getDouble ("volume", 0.8), nullptr);
            tr.setProperty ("pan", td.root.getDouble ("pan", 0.0), nullptr);
            tr.setProperty ("mute", td.root.getBool ("mute"), nullptr);
            tr.setProperty ("solo", td.root.getBool ("solo"), nullptr);
            tr.setProperty ("type", td.root.getInt ("type", 0), nullptr);
            const auto insRef = td.root.getString ("mixer_insert");
            tr.setProperty ("mixerTrack", insertIndex.count (insRef) ? insertIndex[insRef] : insRef.getIntValue(), nullptr);
            if (td.root.getBool ("polarity")) tr.setProperty ("polarity", true, nullptr);
            if (td.root.getBool ("arp"))       // live arpeggiator
            {
                tr.setProperty ("arpOn", true, nullptr);
                tr.setProperty ("arpRate",  td.root.getDouble ("arp_rate", 0.25), nullptr);
                tr.setProperty ("arpOct",   td.root.getInt ("arp_octaves", 1), nullptr);
                tr.setProperty ("arpGate",  td.root.getDouble ("arp_gate", 0.5), nullptr);
                tr.setProperty ("arpMode",  td.root.getInt ("arp_mode", 0), nullptr);
                tr.setProperty ("arpSwing", td.root.getDouble ("arp_swing", 0.5), nullptr);
                tr.setProperty ("arpHold",  td.root.getBool ("arp_hold"), nullptr);
                if (td.root.getDouble ("arp_probability", 1.0) < 1.0)
                    tr.setProperty ("arpProb", td.root.getDouble ("arp_probability", 1.0), nullptr);
            }

            if (auto* g = td.table ("generator"))
            {
                const auto gtype = g->getString ("type");
                if (gtype == "synth")
                {
                    tr.setProperty ("gen", "Synth", nullptr);
                    juce::ValueTree s ("SYNTH");
                    for (auto& kv : g->raw)
                        if (kv.first != "type")
                            s.setProperty (kv.first, g->getDouble (kv.first.toRawUTF8()), nullptr);
                    tr.addChild (s, -1, nullptr);
                }
                else if (gtype == "sfz")
                {
                    tr.setProperty ("gen", "Sfz", nullptr);
                    juce::ValueTree s ("SFZ"); s.setProperty ("path", g->getString ("path"), nullptr);
                    tr.addChild (s, -1, nullptr);
                }
                else if (gtype == "sampler")
                {
                    tr.setProperty ("gen", "Sampler", nullptr);
                    juce::ValueTree s ("SAMPLE");
                    int ch = 1, fr = 0; double rate = 44100.0;
                    s.setProperty ("data", wavToBase64 (dir.getChildFile (g->getString ("sample_file")),
                                                        formatManager, ch, fr, rate), nullptr);
                    s.setProperty ("channels", ch, nullptr); s.setProperty ("frames", fr, nullptr);
                    s.setProperty ("rate", rate, nullptr);
                    s.setProperty ("root", g->getInt ("root", 60), nullptr);
                    s.setProperty ("sstart", g->getDouble ("start", 0.0), nullptr);
                    s.setProperty ("send", g->getDouble ("end", 1.0), nullptr);
                    s.setProperty ("srev", g->getBool ("reverse", false), nullptr);
                    s.setProperty ("sfadein", g->getDouble ("fade_in", 0.0), nullptr);
                    s.setProperty ("sfadeout", g->getDouble ("fade_out", 0.0), nullptr);
                    s.setProperty ("sloop", g->getBool ("loop", false), nullptr);
                    if (g->getBool ("mono", false)) s.setProperty ("smono", true, nullptr);
                    if (g->getDouble ("loop_xfade", 0.0) > 0.0) s.setProperty ("sloopxf", g->getDouble ("loop_xfade", 0.0), nullptr);
                    if ((int) g->getDouble ("interp", 0.0) != 0) s.setProperty ("sinterp", (int) g->getDouble ("interp", 0.0), nullptr);
                    s.setProperty ("sname", g->getString ("sample_name"), nullptr);
                    tr.addChild (s, -1, nullptr);
                }
                else if (gtype == "drumkit")
                {
                    // Rebuild a DrumKit: each [[generator.pad]] loads its sample_file into a
                    // PAD node shaped exactly like the native (.glp) KIT/PAD save path expects.
                    tr.setProperty ("gen", "DrumKit", nullptr);
                    juce::ValueTree k ("KIT");
                    if (auto* pads = td.array ("generator.pad"))
                        for (auto& pd : *pads)
                        {
                            juce::ValueTree p ("PAD");
                            int ch = 1, fr = 0; double rate = 44100.0;
                            p.setProperty ("data", wavToBase64 (dir.getChildFile (pd.getString ("sample_file")),
                                                                formatManager, ch, fr, rate), nullptr);
                            p.setProperty ("channels", ch, nullptr);
                            p.setProperty ("frames", fr, nullptr);
                            p.setProperty ("rate", rate, nullptr);
                            const int note = pd.getInt ("note", 36);
                            p.setProperty ("note", note, nullptr);
                            p.setProperty ("root", pd.getInt ("root", note), nullptr);
                            p.setProperty ("padname", pd.getString ("name", "Pad"), nullptr);
                            p.setProperty ("padcol", pd.getInt ("colour", (int) juce::Colours::orangered.getARGB()), nullptr);
                            p.setProperty ("sstart", pd.getDouble ("start", 0.0), nullptr);
                            p.setProperty ("send", pd.getDouble ("end", 1.0), nullptr);
                            p.setProperty ("srev", pd.getBool ("reverse", false), nullptr);
                            p.setProperty ("sfadein", pd.getDouble ("fade_in", 0.0), nullptr);
                            p.setProperty ("sfadeout", pd.getDouble ("fade_out", 0.0), nullptr);
                            p.setProperty ("sloop", pd.getBool ("loop", false), nullptr);
                            if (pd.getBool ("mono", false)) p.setProperty ("smono", true, nullptr);
                            if (pd.getDouble ("loop_xfade", 0.0) > 0.0) p.setProperty ("sloopxf", pd.getDouble ("loop_xfade", 0.0), nullptr);
                            if (pd.getInt ("interp", 0) != 0) p.setProperty ("sinterp", pd.getInt ("interp", 0), nullptr);
                            p.setProperty ("sname", pd.getString ("sample_name", pd.getString ("name")), nullptr);
                            k.addChild (p, -1, nullptr);
                        }
                    tr.addChild (k, -1, nullptr);
                }
                else if (gtype == "plugin")
                {
                    tr.setProperty ("gen", "Plugin", nullptr);
                    juce::ValueTree p ("PLUGIN");
                    p.setProperty ("pstate", readBinaryAsBase64 (dir.getChildFile (g->getString ("state_file"))), nullptr);
                    p.setProperty ("pdesc",  dir.getChildFile (g->getString ("desc_file")).loadFileAsString(), nullptr);
                    tr.addChild (p, -1, nullptr);
                }
            }

            if (auto* clips = td.array ("clips"))
                for (auto& cd : *clips)
                {
                    juce::ValueTree cl ("CLIP");
                    cl.setProperty ("ctype", cd.getInt ("ctype", 0), nullptr);
                    cl.setProperty ("name", cd.getString ("name"), nullptr);
                    cl.setProperty ("start", cd.getDouble ("start", 0.0), nullptr);
                    cl.setProperty ("len", cd.getDouble ("length", 4.0), nullptr);
                    cl.setProperty ("content", cd.getDouble ("content_length", 4.0), nullptr);
                    cl.setProperty ("looped", cd.getBool ("looped"), nullptr);
                    if (cd.getInt ("transpose", 0) != 0) cl.setProperty ("transpose", cd.getInt ("transpose", 0), nullptr);
                    if (cd.getDouble ("velocity_scale", 1.0) != 1.0) cl.setProperty ("velscale", cd.getDouble ("velocity_scale", 1.0), nullptr);
                    if (cd.getBool ("muted")) cl.setProperty ("muted", true, nullptr);
                    if (cd.getDouble ("fade_in", 0.0)  > 0.0) cl.setProperty ("fadein",  cd.getDouble ("fade_in", 0.0), nullptr);
                    if (cd.getDouble ("fade_out", 0.0) > 0.0) cl.setProperty ("fadeout", cd.getDouble ("fade_out", 0.0), nullptr);
                    if ((int) cd.getDouble ("fade_shape", 0.0) != 0) cl.setProperty ("fadeshape", (int) cd.getDouble ("fade_shape", 0.0), nullptr);
                    if (cd.getInt ("colour", 0) != 0) cl.setProperty ("colour", cd.getInt ("colour", 0), nullptr);   // per-clip colour override
                    if (cd.has ("script") || cd.has ("generator"))   // script clip: file OR named generator + seed
                    {
                        if (cd.has ("script"))    cl.setProperty ("script", cd.getString ("script"), nullptr);
                        if (cd.has ("generator")) cl.setProperty ("generator", cd.getString ("generator"), nullptr);
                        if (cd.has ("system"))    cl.setProperty ("scriptsystem", cd.getString ("system"), nullptr);
                        if (cd.has ("script_lang")) cl.setProperty ("scriptlang", cd.getString ("script_lang"), nullptr);
                        if (const auto sd = cd.getString ("script_seed").getLargeIntValue(); sd != 0)
                            cl.setProperty ("scriptseed", (juce::int64) sd, nullptr);
                    }
                    if (cd.has ("take"))          // referenced take/asset — keep the reference
                    {
                        cl.setProperty ("afile", cd.getString ("audio_file"), nullptr);
                        cl.setProperty ("take",  cd.getString ("take"), nullptr);
                        cl.setProperty ("again", cd.getDouble ("audio_gain", 1.0), nullptr);
                    }
                    else if (cd.has ("audio_file"))   // embedded audio sidecar
                    {
                        int ch = 1, fr = 0; double rate = 44100.0;
                        cl.setProperty ("adata", wavToBase64 (dir.getChildFile (cd.getString ("audio_file")),
                                                              formatManager, ch, fr, rate), nullptr);
                        cl.setProperty ("achannels", ch, nullptr); cl.setProperty ("aframes", fr, nullptr);
                        cl.setProperty ("arate", cd.getDouble ("audio_rate", rate), nullptr);
                        cl.setProperty ("again", cd.getDouble ("audio_gain", 1.0), nullptr);
                    }
                    else if (cd.has ("notes"))
                    {
                        readNotes (dir.getChildFile (cd.getString ("notes")), cl);
                    }
                    tr.addChild (cl, -1, nullptr);
                }
            // Session-view slots -> SCLIP children tagged with their scene index.
            if (auto* sclips = td.array ("session_clips"))
                for (auto& cd : *sclips)
                {
                    juce::ValueTree cl ("SCLIP");
                    cl.setProperty ("scene", cd.getInt ("scene", -1), nullptr);
                    cl.setProperty ("ctype", cd.getInt ("ctype", 0), nullptr);
                    cl.setProperty ("name", cd.getString ("name"), nullptr);
                    cl.setProperty ("len", cd.getDouble ("length", 4.0), nullptr);
                    cl.setProperty ("content", cd.getDouble ("content_length", 4.0), nullptr);
                    cl.setProperty ("looped", cd.getBool ("looped"), nullptr);
                    if (cd.getInt ("transpose", 0) != 0) cl.setProperty ("transpose", cd.getInt ("transpose", 0), nullptr);
                    if (cd.getDouble ("velocity_scale", 1.0) != 1.0) cl.setProperty ("velscale", cd.getDouble ("velocity_scale", 1.0), nullptr);
                    if (cd.getBool ("muted")) cl.setProperty ("muted", true, nullptr);
                    if (cd.getDouble ("fade_in", 0.0)  > 0.0) cl.setProperty ("fadein",  cd.getDouble ("fade_in", 0.0), nullptr);
                    if (cd.getDouble ("fade_out", 0.0) > 0.0) cl.setProperty ("fadeout", cd.getDouble ("fade_out", 0.0), nullptr);
                    if ((int) cd.getDouble ("fade_shape", 0.0) != 0) cl.setProperty ("fadeshape", (int) cd.getDouble ("fade_shape", 0.0), nullptr);
                    if (cd.getInt ("colour", 0) != 0) cl.setProperty ("colour", cd.getInt ("colour", 0), nullptr);
                    if (cd.has ("take"))
                    {
                        cl.setProperty ("afile", cd.getString ("audio_file"), nullptr);
                        cl.setProperty ("take",  cd.getString ("take"), nullptr);
                        cl.setProperty ("again", cd.getDouble ("audio_gain", 1.0), nullptr);
                    }
                    else if (cd.has ("audio_file"))
                    {
                        int ch = 1, fr = 0; double rate = 44100.0;
                        cl.setProperty ("adata", wavToBase64 (dir.getChildFile (cd.getString ("audio_file")),
                                                              formatManager, ch, fr, rate), nullptr);
                        cl.setProperty ("achannels", ch, nullptr); cl.setProperty ("aframes", fr, nullptr);
                        cl.setProperty ("arate", cd.getDouble ("audio_rate", rate), nullptr);
                        cl.setProperty ("again", cd.getDouble ("audio_gain", 1.0), nullptr);
                    }
                    else if (cd.has ("notes"))
                    {
                        readNotes (dir.getChildFile (cd.getString ("notes")), cl);
                    }
                    tr.addChild (cl, -1, nullptr);
                }
            tracksTree.addChild (tr, -1, nullptr);
        }

    // Automation.
    juce::ValueTree autoTree ("AUTOMATION");
    if (auto laneDoc = toml::parse (dir.getChildFile ("automation/lanes.toml").loadFileAsString());
        auto* lanes = laneDoc.array ("lanes"))
        for (auto& ld : *lanes)
        {
            juce::ValueTree lane ("LANE");
            lane.setProperty ("type", ld.getInt ("type", 0), nullptr);
            lane.setProperty ("id", ld.getInt ("target_id", 0), nullptr);
            lane.setProperty ("slot", ld.getInt ("slot", 0), nullptr);
            lane.setProperty ("param", ld.getString ("param"), nullptr);
            if (ld.getString ("target").isNotEmpty()) lane.setProperty ("target", ld.getString ("target"), nullptr);
            if (ld.getBool ("step")) lane.setProperty ("step", true, nullptr);
            if (ld.getDouble ("curve", 0.0) != 0.0) lane.setProperty ("curve", ld.getDouble ("curve", 0.0), nullptr);
            readPoints (dir.getChildFile (ld.getString ("points")), lane);
            autoTree.addChild (lane, -1, nullptr);
        }

    // Timeline locations.
    juce::ValueTree locTree ("LOCATIONS");
    if (auto locDoc = toml::parse (dir.getChildFile ("locations.toml").loadFileAsString());
        auto* ls = locDoc.array ("locations"))
        for (auto& ld : *ls)
        {
            juce::ValueTree l ("LOC");
            l.setProperty ("name", ld.getString ("name"), nullptr);
            l.setProperty ("kind", ld.getString ("kind"), nullptr);
            l.setProperty ("start", ld.getDouble ("start", 0.0), nullptr);
            l.setProperty ("end", ld.getDouble ("end", 0.0), nullptr);
            locTree.addChild (l, -1, nullptr);
        }

    // Export profiles.
    juce::ValueTree expTree ("EXPORTS");
    if (auto expDoc = toml::parse (dir.getChildFile ("exports.toml").loadFileAsString());
        auto* es = expDoc.array ("exports"))
        for (auto& ed : *es)
        {
            juce::ValueTree e ("EXPORT");
            e.setProperty ("name", ed.getString ("name"), nullptr);
            e.setProperty ("target", ed.getString ("target"), nullptr);
            e.setProperty ("range", ed.getString ("range"), nullptr);
            e.setProperty ("format", ed.getString ("format"), nullptr);
            e.setProperty ("track", ed.getInt ("track", 0), nullptr);
            e.setProperty ("tail", ed.getDouble ("tail", 0.0), nullptr);
            expTree.addChild (e, -1, nullptr);
        }

    // Mixer scenes.
    juce::ValueTree sceneTree ("SCENES");
    if (auto scDoc = toml::parse (dir.getChildFile ("scenes.toml").loadFileAsString());
        auto* ss = scDoc.array ("scenes"))
        for (auto& sd : *ss)
        {
            juce::ValueTree sv ("SCENE");
            sv.setProperty ("name", sd.getString ("name"), nullptr);
            for (auto& enc : sd.getStringArray ("inserts"))
            {
                auto p = juce::StringArray::fromTokens (enc, ",", "");
                juce::ValueTree iv ("INSERT");
                iv.setProperty ("vol",  p.size() > 0 ? p[0].getDoubleValue() : 0.8, nullptr);
                iv.setProperty ("pan",  p.size() > 1 ? p[1].getDoubleValue() : 0.0, nullptr);
                iv.setProperty ("mute", p.size() > 2 && p[2] == "1", nullptr);
                iv.setProperty ("solo", p.size() > 3 && p[3] == "1", nullptr);
                iv.setProperty ("bypass", p.size() > 4 ? p[4] : juce::String(), nullptr);
                if (p.size() > 5 && p[5].isNotEmpty()) iv.setProperty ("sends", p[5], nullptr);
                sv.addChild (iv, -1, nullptr);
            }
            sceneTree.addChild (sv, -1, nullptr);
        }

    // Session-view scene rows.
    juce::ValueTree sessionScenes ("SESSIONSCENES");
    if (auto ssDoc = toml::parse (dir.getChildFile ("session.toml").loadFileAsString());
        auto* rows = ssDoc.array ("scene"))
        for (auto& sd : *rows)
        {
            juce::ValueTree one ("SSCENE");
            one.setProperty ("name", sd.getString ("name"), nullptr);
            if (sd.has ("colour")) one.setProperty ("colour", sd.getInt ("colour", 0), nullptr);
            sessionScenes.addChild (one, -1, nullptr);
        }

    root.addChild (tracksTree, -1, nullptr);
    root.addChild (mixerTree, -1, nullptr);
    root.addChild (autoTree, -1, nullptr);
    // Modulation matrix.
    juce::ValueTree modTree ("MODS");
    if (auto mdDoc = toml::parse (dir.getChildFile ("mods.toml").loadFileAsString());
        auto* ms = mdDoc.array ("mods"))
        for (auto& md : *ms)
        {
            juce::ValueTree mv ("MOD");
            mv.setProperty ("target", md.getString ("target"), nullptr);
            mv.setProperty ("rate", md.getDouble ("rate", 1.0), nullptr);
            mv.setProperty ("depth", md.getDouble ("depth", 0.0), nullptr);
            mv.setProperty ("center", md.getDouble ("center", 0.0), nullptr);
            mv.setProperty ("shape", md.getInt ("shape", 0), nullptr);
            if (md.getDouble ("sync", 0.0) > 0.0) mv.setProperty ("sync", md.getDouble ("sync", 0.0), nullptr);
            if (md.getDouble ("phase", 0.0) > 0.0) mv.setProperty ("phase", md.getDouble ("phase", 0.0), nullptr);
            if (md.getBool ("unipolar")) mv.setProperty ("unipolar", true, nullptr);
            if (md.getDouble ("slew", 0.0) > 0.0) mv.setProperty ("slew", md.getDouble ("slew", 0.0), nullptr);
            modTree.addChild (mv, -1, nullptr);
        }

    root.addChild (locTree, -1, nullptr);
    root.addChild (expTree, -1, nullptr);
    // Tempo map.
    juce::ValueTree tempoTree ("TEMPOMAP");
    if (auto tDoc = toml::parse (dir.getChildFile ("tempo.toml").loadFileAsString());
        auto* ms = tDoc.array ("markers"))
        for (auto& md : *ms)
        {
            juce::ValueTree v ("TM");
            v.setProperty ("beat", md.getDouble ("beat", 0.0), nullptr);
            v.setProperty ("bpm", md.getDouble ("bpm", 120.0), nullptr);
            tempoTree.addChild (v, -1, nullptr);
        }

    // Time-signature map (mid-song changes).
    juce::ValueTree timeSigTree ("TIMESIGMAP");
    if (auto tsDoc = toml::parse (dir.getChildFile ("timesig.toml").loadFileAsString());
        auto* ms = tsDoc.array ("markers"))
        for (auto& md : *ms)
        {
            juce::ValueTree v ("TS");
            v.setProperty ("beat", md.getDouble ("beat", 0.0), nullptr);
            v.setProperty ("num", (int) md.getDouble ("num", 4.0), nullptr);
            v.setProperty ("den", (int) md.getDouble ("den", 4.0), nullptr);
            timeSigTree.addChild (v, -1, nullptr);
        }

    root.addChild (sceneTree, -1, nullptr);
    root.addChild (sessionScenes, -1, nullptr);
    // Control groups (VCA-lite).
    juce::ValueTree groupTree ("GROUPS");
    if (auto gDoc = toml::parse (dir.getChildFile ("groups.toml").loadFileAsString());
        auto* gs = gDoc.array ("groups"))
        for (auto& gd : *gs)
        {
            juce::ValueTree gv ("GROUP");
            gv.setProperty ("name", gd.getString ("name"), nullptr);
            gv.setProperty ("gain", gd.getDouble ("gain", 1.0), nullptr);
            gv.setProperty ("mute", gd.getBool ("mute"), nullptr);
            if (gd.getBool ("solo")) gv.setProperty ("solo", true, nullptr);
            groupTree.addChild (gv, -1, nullptr);
        }
    root.addChild (groupTree, -1, nullptr);
    // Controller mappings.
    juce::ValueTree ctlTree ("CONTROLLERS");
    if (auto cDoc = toml::parse (dir.getChildFile ("controllers.toml").loadFileAsString());
        auto* cs = cDoc.array ("controllers"))
        for (auto& cd : *cs)
        {
            juce::ValueTree v ("CTRL");
            v.setProperty ("source", cd.getString ("source"), nullptr);
            v.setProperty ("target", cd.getString ("target"), nullptr);
            v.setProperty ("lo", cd.getDouble ("lo", 0.0), nullptr);
            v.setProperty ("hi", cd.getDouble ("hi", 1.0), nullptr);
            if (cd.getBool ("bypass")) v.setProperty ("bypass", true, nullptr);
            ctlTree.addChild (v, -1, nullptr);
        }

    root.addChild (modTree, -1, nullptr);
    root.addChild (tempoTree, -1, nullptr);
    root.addChild (timeSigTree, -1, nullptr);
    root.addChild (ctlTree, -1, nullptr);

    currentProjectFile = manifest;   // so relative sample/SFZ paths resolve against the dir
    undoSuppressed = true;
    loadFromTree (root);
    undoSuppressed = false;
    undoStack.clear(); redoStack.clear();
    refreshUiAfterLoad();
    std::cout << "[composition] loaded " << tracksTree.getNumChildren() << " tracks from "
              << dir.getFullPathName() << std::endl;
    return true;
}
