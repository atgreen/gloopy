// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <cstring>

/** Parser for Hydrogen drumkits (drumkit.xml, format version 2).

    A Hydrogen kit is a folder of samples described by a drumkit.xml: an
    <instrumentList> of <instrument>s, each with a <name>, a <midiOutNote>, and one
    or more velocity <layer>s (a <filename> + a [min,max] velocity range). Gloopy's
    DrumKit plays one sample per pad, so we take each instrument's loudest layer.

    This header is pure parsing — XML + file paths, no audio — so it unit-tests
    without the audio modules. The actual sample loading (Sampler::loadFile) lives in
    the DrumKit loader on the app side. */
namespace HydrogenKit
{
    struct Piece
    {
        juce::String name;
        int          note { 36 };     // midiOutNote — the pad's MIDI note
        juce::Colour colour { 0xff9aa0a6 };
        juce::File   sample;          // resolved absolute path to the (loudest-layer) sample
    };

    struct Kit
    {
        juce::String       name, author, license;
        juce::File         dir;       // the kit folder
        std::vector<Piece> pieces;
        bool valid() const { return ! pieces.empty(); }
    };

    /** Colour a pad from its instrument name, so kicks/snares/hats read consistently
        across kits; anything unrecognised cycles a small palette by index. */
    inline juce::Colour colourFor (const juce::String& rawName, int index)
    {
        const auto n = rawName.toLowerCase();
        if (n.contains ("kick") || n.contains ("bass drum")) return juce::Colours::orangered;
        if (n.contains ("snare"))                            return juce::Colours::gold;
        if (n.contains ("clap") || n.contains ("hand"))      return juce::Colours::violet;
        if (n.contains ("hat") || n.contains ("hh"))         return juce::Colours::aquamarine;
        if (n.contains ("ride"))                             return juce::Colours::khaki;
        if (n.contains ("crash") || n.contains ("cymbal"))   return juce::Colours::lightsteelblue;
        if (n.contains ("tom"))                              return juce::Colours::skyblue;
        if (n.contains ("cowbell") || n.contains ("bell"))   return juce::Colours::yellowgreen;
        if (n.contains ("perc") || n.contains ("conga")
            || n.contains ("bongo") || n.contains ("clave")) return juce::Colours::mediumpurple;
        static const juce::Colour cycle[] = {
            juce::Colours::coral, juce::Colours::mediumturquoise, juce::Colours::plum,
            juce::Colours::sandybrown, juce::Colours::palegreen, juce::Colours::salmon };
        return cycle[index % (int) (sizeof (cycle) / sizeof (cycle[0]))];
    }

    /** The sample filename of an instrument's loudest layer (the layer whose velocity
        range reaches highest). Falls back to a direct <filename> on the instrument (an
        older, layerless kit). Empty if the instrument has no sample. */
    inline juce::String loudestLayerFilename (const juce::XmlElement& inst)
    {
        juce::String best;
        double bestMax = -1.0;
        // Layers live under <instrumentComponent>; older kits nest them directly.
        for (auto* comp : inst.getChildWithTagNameIterator ("instrumentComponent"))
        {
            for (auto* layer : comp->getChildWithTagNameIterator ("layer"))
            {
                const auto fn = layer->getChildElementAllSubText ("filename", {});
                if (fn.isEmpty()) continue;
                const double mx = layer->getDoubleAttribute ("max",
                                     layer->getChildElementAllSubText ("max", "1.0").getDoubleValue());
                if (mx > bestMax) { bestMax = mx; best = fn; }
            }
        }
        if (best.isEmpty())                                   // layerless fallback
            best = inst.getChildElementAllSubText ("filename", {});
        return best;
    }

    /** Parse a drumkit.xml into a Kit. Sample paths are resolved relative to the xml's
        folder. Instruments with no usable sample are skipped; duplicate/invalid MIDI
        notes are bumped to a free slot so every pad stays addressable. */
    inline Kit parse (const juce::File& drumkitXml)
    {
        Kit kit;
        kit.dir = drumkitXml.getParentDirectory();

        juce::XmlDocument doc (drumkitXml);
        std::unique_ptr<juce::XmlElement> root (doc.getDocumentElement());
        if (root == nullptr || ! root->hasTagName ("drumkit_info"))
            return kit;

        kit.name    = root->getChildElementAllSubText ("name", drumkitXml.getParentDirectory().getFileName());
        kit.author  = root->getChildElementAllSubText ("author", {});
        kit.license = root->getChildElementAllSubText ("license", {});

        auto* list = root->getChildByName ("instrumentList");
        if (list == nullptr)
            return kit;

        std::vector<bool> taken (128, false);
        auto claimNote = [&] (int wanted) -> int
        {
            int n = juce::isPositiveAndBelow (wanted, 128) ? wanted : 36;
            while (n < 128 && taken[(size_t) n]) ++n;              // next free slot up
            if (n >= 128) { n = 0; while (n < 128 && taken[(size_t) n]) ++n; }
            if (n >= 128) n = 127;
            taken[(size_t) n] = true;
            return n;
        };

        int index = 0;
        for (auto* inst : list->getChildWithTagNameIterator ("instrument"))
        {
            const auto fn = loudestLayerFilename (*inst);
            if (fn.isEmpty()) continue;
            auto sample = kit.dir.getChildFile (fn);

            Piece p;
            p.name   = inst->getChildElementAllSubText ("name", "Pad " + juce::String (index + 1));
            p.note   = claimNote (inst->getChildElementAllSubText ("midiOutNote", "-1").getIntValue());
            p.colour = colourFor (p.name, index);
            p.sample = sample;
            kit.pieces.push_back (std::move (p));
            ++index;
        }
        return kit;
    }

    /** Extract a .h2drumkit (a gzip-compressed tar) into @p destDir and return the
        drumkit.xml found inside (or an invalid File). Hydrogen archives are a simple
        ustar tar of one top folder; we write regular files and skip the rest. */
    inline juce::File extractArchive (const juce::File& archive, const juce::File& destDir)
    {
        juce::FileInputStream fis (archive);
        if (! fis.openedOk()) return {};
        juce::GZIPDecompressorInputStream gz (&fis, false,
                                              juce::GZIPDecompressorInputStream::gzipFormat);
        juce::MemoryOutputStream mem;
        mem.writeFromInputStream (gz, -1);
        const auto* data  = static_cast<const char*> (mem.getData());
        const size_t total = mem.getDataSize();
        destDir.createDirectory();

        auto octal = [] (const char* p, int n)
        {
            long v = 0;
            for (int i = 0; i < n && p[i] != '\0' && p[i] != ' '; ++i)
                if (p[i] >= '0' && p[i] <= '7') v = v * 8 + (p[i] - '0');
            return v;
        };

        size_t off = 0;
        while (off + 512 <= total)
        {
            const char* h = data + off;
            if (h[0] == '\0') break;                              // zero block => end of archive

            char nameBuf[101]; std::memcpy (nameBuf, h, 100); nameBuf[100] = '\0';
            juce::String name { juce::CharPointer_UTF8 (nameBuf) };
            char prefBuf[156]; std::memcpy (prefBuf, h + 345, 155); prefBuf[155] = '\0';
            if (const juce::String prefix { juce::CharPointer_UTF8 (prefBuf) }; prefix.isNotEmpty())
                name = prefix + "/" + name;

            const long size = octal (h + 124, 12);
            const char type = h[156];
            off += 512;

            // Regular file, no path traversal, data in bounds.
            if (name.isNotEmpty() && ! name.contains ("..") && (type == '0' || type == '\0')
                && off + (size_t) size <= total)
            {
                auto out = destDir.getChildFile (name);
                out.getParentDirectory().createDirectory();
                out.deleteFile();
                if (auto os = out.createOutputStream())
                    os->write (data + off, (size_t) size);
            }
            off += (size_t) ((size + 511) / 512) * 512;           // advance to the next 512-aligned block
        }

        auto hits = destDir.findChildFiles (juce::File::findFiles, true, "drumkit.xml");
        return hits.isEmpty() ? juce::File() : hits[0];
    }
}
