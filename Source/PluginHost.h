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

    /** Scan the default locations for every enabled format. */
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
