#pragma once

#include <JuceHeader.h>
#include <functional>

/** Owns the plugin format manager (VST3 + LV2) and the scanned plugin list.
    Scanning is done in-process for simplicity. */
class PluginHost
{
public:
    PluginHost() { juce::addDefaultFormatsToManager (formatManager); }   // VST3 + LV2

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList          knownList;

    juce::StringArray formatNames() const
    {
        juce::StringArray names;
        for (auto* f : formatManager.getFormats())
            names.add (f->getName());
        return names;
    }

    /** Scan the default locations for every enabled format, then cache the list. */
    void scanAll (std::function<void (const juce::String&)> onProgress = {})
    {
        for (auto* fmt : formatManager.getFormats())
        {
            auto paths = fmt->getDefaultLocationsToSearch();
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
