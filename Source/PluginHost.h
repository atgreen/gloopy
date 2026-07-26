// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <functional>

/** Owns the plugin format manager (VST3 + LV2) and the scanned plugin list.
    Scanning is done in-process for simplicity. */
class PluginHost
{
public:
    PluginHost()
    {
        addBundledDirsToLV2Path();                        // must precede format construction
        juce::addDefaultFormatsToManager (formatManager); // VST3 + LV2
    }

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList          knownList;

    juce::StringArray formatNames() const
    {
        juce::StringArray names;
        for (auto* f : formatManager.getFormats())
            names.add (f->getName());
        return names;
    }

    /** Directories Gloopy bundles plugins in (the built-in Surge XT LV2, etc.). Added to
        every format's scan path so a bundled plugin is found with no external install:
        $GLOOPY_PLUGIN_PATH, the dev tree (GLOOPY_ASSETS_DIR/surge-plugin) and the installed
        layout (<exeDir>/plugins). */
    static juce::Array<juce::File> bundledPluginDirs()
    {
        juce::Array<juce::File> dirs;
        auto env = juce::SystemStats::getEnvironmentVariable ("GLOOPY_PLUGIN_PATH", {});
        if (env.isNotEmpty()) dirs.add (juce::File (env));
       #ifdef GLOOPY_ASSETS_DIR
        dirs.add (juce::File (GLOOPY_ASSETS_DIR).getChildFile ("surge-plugin"));
       #endif
        dirs.add (juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                      .getParentDirectory().getChildFile ("plugins"));
        return dirs;
    }

    /** Make the bundled plugin dirs visible to JUCE's LV2 host at *instantiation* time, not
        just when scanning. JUCE builds its lilv world once, from getDefaultLocationsToSearch()
        (= $LV2_PATH + the standard dirs), and instantiates cached plugins from that world — so
        a bundle added only to the scan path is discoverable yet fails to create from cache with
        "Unable to locate plugin with the requested URI". Prepending our dirs to $LV2_PATH before
        the LV2 format is constructed puts the bundled Surge XT in the world from the start. */
    static void addBundledDirsToLV2Path()
    {
       #if JUCE_LINUX || JUCE_BSD
        auto existing = juce::SystemStats::getEnvironmentVariable ("LV2_PATH", {});
        juce::StringArray entries;
        entries.addTokens (existing, ":", {});
        entries.removeEmptyStrings();
        bool changed = false;
        for (auto& d : bundledPluginDirs())
        {
            if (! d.isDirectory()) continue;
            auto p = d.getFullPathName();
            if (! entries.contains (p)) { entries.insert (0, p); changed = true; }
        }
        if (changed)
            ::setenv ("LV2_PATH", entries.joinIntoString (":").toRawUTF8(), 1);
       #endif
    }

    /** Scan the default locations for every enabled format, then cache the list. */
    void scanAll (std::function<void (const juce::String&)> onProgress = {})
    {
        for (auto* fmt : formatManager.getFormats())
        {
            auto paths = fmt->getDefaultLocationsToSearch();
            for (auto& d : bundledPluginDirs())
                if (d.isDirectory()) paths.addIfNotAlreadyThere (d);   // bundled Surge XT.lv2 etc.
            juce::PluginDirectoryScanner scanner (knownList, *fmt, paths, true, juce::File());
            juce::String name;
            while (scanner.scanNextFile (true, name))
                if (onProgress) onProgress (name);
        }
        saveCache();
    }

    juce::File cacheFile() const
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                    .getChildFile ("Gloopy").getChildFile ("plugins.xml");
    }

    /** Restore a previously scanned list (instant); returns true if it had entries. */
    bool loadCache()
    {
        if (auto xml = juce::parseXML (cacheFile()))
        {
            knownList.recreateFromXml (*xml);
            return knownList.getNumTypes() > 0;
        }
        return false;
    }

    void saveCache()
    {
        auto f = cacheFile();
        f.getParentDirectory().createDirectory();
        if (auto xml = knownList.createXml())
            xml->writeTo (f);
    }

    std::unique_ptr<juce::AudioPluginInstance> create (const juce::PluginDescription& desc,
                                                       double sampleRate, int blockSize,
                                                       juce::String& error)
    {
        return formatManager.createPluginInstance (desc, sampleRate, blockSize, error);
    }

    juce::Array<juce::PluginDescription> plugins (bool instrumentsOnly) const
    {
        juce::Array<juce::PluginDescription> out;
        for (const auto& d : knownList.getTypes())
            if (d.isInstrument == instrumentsOnly)
                out.add (d);
        return out;
    }
};
