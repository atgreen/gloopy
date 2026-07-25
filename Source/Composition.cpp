// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

// Phase 1 of the "composition as repo" format (docs: gloop-compositions.md).
// A composition directory is an alternative *serialisation* of the same ValueTree
// that toValueTree()/loadFromTree() already use, so this file only implements the
// ValueTree <-> directory mapping and leaves the runtime<->tree mapping untouched:
//   saveComposition: toValueTree()  -> write TOML/notes/points/asset files
//   loadComposition: read those files -> ValueTree -> loadFromTree()
// .gloopy XML stays the default; the directory loader is read-only for now.

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
    juce::String out ("# pitch\tstart\tlength\tvelocity\n");
    for (auto& n : notes)
        out << (int) n.getProperty ("pitch") << "\t"
            << toml::Writer::num (n.getProperty ("start")) << "\t"
            << toml::Writer::num (n.getProperty ("nlen"))  << "\t"
            << toml::Writer::num (n.getProperty ("vel"))   << "\n";
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

const char* kGitignore = ".gloopy-cache/\nexports/\nassets/recordings/raw/\n*.wav.tmp\nplugins/scans/\n";
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
       .number ("swing", root.getProperty ("swing", 0.5)).blank();

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
    ctx.writeText ("gloopy.toml", man.str());

    // --- per-track files ---
    for (int i = 0; i < tracks.getNumChildren(); ++i)
    {
        auto tr = tracks.getChild (i);
        const auto slug = trackSlug[i];
        toml::Writer w;
        w.str ("id", slug).str ("name", tr.getProperty ("name").toString())
         .integer ("type", (int) tr.getProperty ("type", 0))
         .integer ("colour", (juce::int64) (int) tr.getProperty ("colour", 0))
         .integer ("default_pitch", (int) tr.getProperty ("pitch", 60))
         .number ("volume", tr.getProperty ("vol", 0.8))
         .number ("pan", tr.getProperty ("pan", 0.0))
         .boolean ("mute", tr.getProperty ("mute", false))
         .boolean ("solo", tr.getProperty ("solo", false));
        const int mtIdx = (int) tr.getProperty ("mixerTrack", 0);
        w.str ("mixer_insert", juce::isPositiveAndBelow (mtIdx, insertSlug.size()) ? insertSlug[mtIdx] : juce::String (mtIdx));
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
             .str ("sample_name", sm.getProperty ("sname").toString())
             .number ("rate", sm.getProperty ("rate", 44100.0));
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
            if ((bool) cl.getProperty ("muted", false)) w.boolean ("muted", true);

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
          .boolean ("mute", mt.getProperty ("mute", false)).boolean ("solo", mt.getProperty ("solo", false)).blank();
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
        auto base = slugify (lane.getProperty ("param").toString(), "lane") + "-"
                    + juce::String ((int) lane.getProperty ("id", 0));
        const auto lslug = uniqueSlug (base, laneSeen);
        const auto rel = "automation/" + lslug + ".points";
        ctx.writeText (rel, buildPoints (lane));
        aw.arrayItem ("lanes").str ("id", lslug)
          .integer ("type", (int) lane.getProperty ("type", 0))
          .integer ("target_id", (int) lane.getProperty ("id", 0))
          .integer ("slot", (int) lane.getProperty ("slot", 0))
          .str ("param", lane.getProperty ("param").toString())
          .str ("points", rel).blank();
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

    ctx.writeText (".gitignore", kGitignore);
    ctx.prune();

    std::cout << "[composition] saved " << tracks.getNumChildren() << " tracks to "
              << dir.getFullPathName() << " (" << ctx.written << " written, "
              << ctx.pruned << " pruned)" << std::endl;
    return true;
}

// ═════════════════════════════════════════════════════════════════════════════
// LOAD : directory -> ValueTree -> runtime  (read-only)
// ═════════════════════════════════════════════════════════════════════════════
bool MainComponent::loadComposition (const juce::File& pathIn)
{
    juce::File dir = pathIn;

    // A .zip archive: unpack to a read-only temp workspace and load that. The
    // gloopy.toml may sit at the root or inside a single top-level folder.
    if (pathIn.existsAsFile() && pathIn.hasFileExtension ("zip"))
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
    root.setProperty ("swing", man.root.getDouble ("swing", 0.5), nullptr);

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
                    s.setProperty ("sname", g->getString ("sample_name"), nullptr);
                    tr.addChild (s, -1, nullptr);
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
                    if (cd.getBool ("muted")) cl.setProperty ("muted", true, nullptr);
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

    root.addChild (tracksTree, -1, nullptr);
    root.addChild (mixerTree, -1, nullptr);
    root.addChild (autoTree, -1, nullptr);
    root.addChild (locTree, -1, nullptr);
    root.addChild (expTree, -1, nullptr);

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
