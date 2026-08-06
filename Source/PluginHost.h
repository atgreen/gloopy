// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include "Paths.h"
#include <functional>
#include <set>
#include <cstdlib>   // setenv / _putenv_s

/** Owns the plugin format manager (VST3 + LV2) and the scanned plugin list.
    Scanning is done in-process for simplicity. */
class PluginHost
{
public:
    PluginHost()
    {
        addBundledDirsToLV2Path();                        // must precede format construction
        setBundledSurgeDataHome();                        // so the hosted Surge XT finds its patches
        juce::addDefaultFormatsToManager (formatManager); // VST3 + LV2
        loadBlacklist();                                  // plugins that crashed us last time
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
        auto exeDir = gloopy::executableDir();
        dirs.add (exeDir.getChildFile ("plugins"));   // portable: <exeDir>/plugins (e.g. the Windows zip)
        // Installed FHS layout: <prefix>/lib(64)/gloopy/plugins (exe at <prefix>/bin). Check both
        // because Fedora uses lib64 and Debian uses lib for the arch-specific plugin bundle.
        auto prefix = exeDir.getParentDirectory();
        for (auto* libd : { "lib", "lib64" })
            dirs.add (prefix.getChildFile (libd).getChildFile ("gloopy").getChildFile ("plugins"));
        return dirs;
    }

    /** Make the bundled plugin dirs visible to JUCE's LV2 host at *instantiation* time, not
        just when scanning. JUCE builds its lilv world once, from getDefaultLocationsToSearch()
        (= $LV2_PATH + the standard dirs), and instantiates cached plugins from that world — so
        a bundle added only to the scan path is discoverable yet fails to create from cache with
        "Unable to locate plugin with the requested URI". Prepending our dirs to $LV2_PATH before
        the LV2 format is constructed puts the bundled Surge XT in the world from the start. */
    /** Point Surge XT at the factory data we bundle. When hosted, Surge can't reliably
        find its data relative to its own binary — its dladdr/GetModuleFileName lookup
        resolves to the *host* (Gloopy), not the plugin, so it falls through to empty
        system paths and shows no patches. Surge honours the SURGE_DATA_HOME env var above
        every other path, on all platforms — so set it to our bundled SurgeXTData. This
        is cross-platform (an env var), unlike a per-OS symlink, and we never override a
        value the user set themselves. */
    static void setBundledSurgeDataHome()
    {
        if (juce::SystemStats::getEnvironmentVariable ("SURGE_DATA_HOME", {}).isNotEmpty())
            return;                                       // respect an explicit user setting

        auto setIt = [] (const juce::File& data)
        {
           #if JUCE_WINDOWS
            _putenv_s ("SURGE_DATA_HOME", data.getFullPathName().toRawUTF8());
           #else
            ::setenv ("SURGE_DATA_HOME", data.getFullPathName().toRawUTF8(), 1);
           #endif
        };

        for (auto& d : bundledPluginDirs())
        {
            if (! d.isDirectory()) continue;
            // Known bundle layouts, checked directly (fast). The build stages the data as
            // SurgeXTData/ inside the plugin bundle; a valid data dir has patches_factory/.
            for (const char* rel : { "Surge XT.lv2/SurgeXTData",
                                     "Surge XT.vst3/Contents/Resources/SurgeXTData" })
                if (auto f = d.getChildFile (rel); f.getChildFile ("patches_factory").isDirectory())
                { setIt (f); return; }
            // Fallback: find a SurgeXTData/ anywhere under this plugin dir (one-time, startup).
            for (juce::DirectoryIterator it (d, true, "SurgeXTData", juce::File::findDirectories); it.next();)
                if (auto f = it.getFile(); f.getChildFile ("patches_factory").isDirectory())
                { setIt (f); return; }
        }
    }

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
        // The dead-man's-pedal file makes scanning crash-resilient: the scanner records the plugin
        // it's about to probe before loading it, and on the next launch blacklists anything left
        // behind by a crash — so one bad plugin can't wedge startup on every run.
        auto pedal = scanPedalFile();
        pedal.getParentDirectory().createDirectory();
        for (auto* fmt : formatManager.getFormats())
        {
            auto paths = fmt->getDefaultLocationsToSearch();
            for (auto& d : bundledPluginDirs())
                if (d.isDirectory()) paths.addIfNotAlreadyThere (d);   // bundled Surge XT.lv2 etc.
            juce::PluginDirectoryScanner scanner (knownList, *fmt, paths, true, pedal);
            juce::String name;
            while (scanner.scanNextFile (true, name))
                if (onProgress) onProgress (name);
        }
        saveCache();
    }

    /** Scan OUT OF PROCESS: run the full scan in a child (`gloopy --scan`) so a plugin that
        segfaults while being probed takes down the CHILD, not us. The child shares the
        dead-man's-pedal file, and JUCE's PluginDirectoryScanner reads it on construction and
        skips whatever a prior run crashed on — so each re-spawn RESUMES past the last crasher
        until the scan completes cleanly. The child writes the plugins.xml cache on a clean
        finish, which we then loadCache(). This means one bad plugin can no longer wedge or
        crash startup even on the FIRST scan (the in-process scanAll + pedal only recovered on
        the *next* launch). Falls back to in-process scanAll() if the exe can't be found/spawned. */
    bool scanOutOfProcess (std::function<void (const juce::String&)> onProgress = {})
    {
        const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        if (! exe.existsAsFile()) { scanAll (onProgress); return true; }   // no exe to re-invoke

        for (int attempt = 0; attempt < 256; ++attempt)   // bounded: ~one retry per bad plugin
        {
            if (onProgress) onProgress (attempt == 0 ? juce::String ("Scanning plugins (isolated)…")
                                                     : "A plugin crashed while scanning — skipping it, resuming…");
            juce::ChildProcess child;
            if (! child.start (juce::StringArray { exe.getFullPathName(), "--scan" },
                               juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
            {
                scanAll (onProgress);   // couldn't spawn the child — do it in-process
                return true;
            }
            child.readAllProcessOutput();        // blocks until the child exits (clean or crash)
            if (child.getExitCode() == 0)        // clean finish -> the child wrote the full cache
                return loadCache();
            // Non-zero exit == the child crashed mid-scan. The offender is now recorded in the
            // pedal, so the next child's scanner skips it and resumes. The attempt cap prevents
            // an infinite loop if the pedal ever fails to advance.
        }
        return loadCache();   // exhausted retries — use the most complete cache we managed to write
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

    // --- Crash resilience: blacklist-before-load ----------------------------------------
    // A plugin can segfault while being instantiated in-process, taking Gloopy down. There's
    // no watchdog; instead we persist the plugin's id to disk *before* the risky call and clear
    // it *after* a successful load. If the load crashes the app, the id survives the crash, so
    // the next launch skips that plugin (with a "crashed last time" note) instead of crashing on
    // every startup. (Radium's trick.) The scan phase is protected separately by the scanner's
    // dead-man's-pedal file below.
    juce::File appDataDir()     const { return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Gloopy"); }
    juce::File blacklistFile()  const { return appDataDir().getChildFile ("plugin-blacklist.txt"); }
    juce::File scanPedalFile()  const { return appDataDir().getChildFile ("plugin-scan-pedal.txt"); }

    void loadBlacklist()
    {
        blacklist.clear();
        for (auto& line : juce::StringArray::fromLines (blacklistFile().loadFileAsString()))
            if (line.trim().isNotEmpty()) blacklist.insert (line.trim());
    }
    void writeBlacklist (const std::set<juce::String>& ids) const
    {
        auto f = blacklistFile();
        f.getParentDirectory().createDirectory();
        juce::StringArray lines;
        for (auto& id : ids) lines.add (id);
        f.replaceWithText (lines.joinIntoString ("\n"));   // synchronous — flushed before we return
    }
    bool isBlacklisted (const juce::String& id) const { return blacklist.find (id) != blacklist.end(); }
    juce::StringArray blacklistedIds() const { juce::StringArray a; for (auto& id : blacklist) a.add (id); return a; }
    void clearBlacklist()                        { blacklist.clear();   writeBlacklist (blacklist); }
    void removeFromBlacklist (const juce::String& id) { blacklist.erase (id); writeBlacklist (blacklist); }

    std::unique_ptr<juce::AudioPluginInstance> create (const juce::PluginDescription& desc,
                                                       double sampleRate, int blockSize,
                                                       juce::String& error)
    {
        const auto id = desc.createIdentifierString();
        if (isBlacklisted (id))
        {
            error = desc.name + " crashed the last time it was opened, so it was skipped. "
                    "Reset the plugin blacklist to try it again.";
            return nullptr;
        }
        // Persist the intent (blacklist ∪ {id}) to disk before the load; a crash leaves it behind.
        auto pending = blacklist;
        pending.insert (id);
        writeBlacklist (pending);

        auto inst = formatManager.createPluginInstance (desc, sampleRate, blockSize, error);

        writeBlacklist (blacklist);   // reached only if the load didn't crash — clear the intent
        return inst;
    }

    juce::Array<juce::PluginDescription> plugins (bool instrumentsOnly) const
    {
        juce::Array<juce::PluginDescription> out;
        for (const auto& d : knownList.getTypes())
            if (d.isInstrument == instrumentsOnly)
                out.add (d);
        return out;
    }

private:
    std::set<juce::String> blacklist;   // plugin ids that crashed us on load; skipped until reset
};
