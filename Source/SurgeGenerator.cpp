// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only
//
// Surge XT synth engine glue. This is the ONLY Gloopy TU that touches the Surge
// headers, and it is compiled as a C++20 unit with -fno-char8_t (Surge's
// requirements; see CMakeLists.txt + docs/surge-embed.md). Everything Surge stays
// behind the PIMPL so the rest of Gloopy remains C++17.

#include "SurgeGenerator.h"

#ifdef GLOOPY_WITH_SURGE

#include "SurgeSynthesizer.h"
#include <algorithm>

namespace
{
    // Minimal PluginLayer — Surge calls back into this for param/macro updates.
    struct GloopySurgeLayer : public SurgeSynthesizer::PluginLayer
    {
        void surgeParameterUpdated (const SurgeSynthesizer::ID&, float) override {}
        void surgeMacroUpdated (long, float) override {}
    };
}

struct SurgeGenerator::Impl
{
    GloopySurgeLayer                   layer;
    std::unique_ptr<SurgeSynthesizer>  surge;
    // Carry buffer: Surge emits exactly BLOCK_SIZE samples per process(); we hold
    // the not-yet-consumed tail across render() calls so no samples are dropped.
    float carry[2][BLOCK_SIZE] {};
    int   carryAvail { 0 };
    int   carryPos   { 0 };

    void ensureSurge()
    {
        if (surge) return;
        const auto dir = SurgeGenerator::dataDir().getFullPathName().toStdString();
        surge = std::make_unique<SurgeSynthesizer> (&layer, dir);
    }
};

SurgeGenerator::SurgeGenerator() : impl (std::make_unique<Impl>()) {}
SurgeGenerator::~SurgeGenerator() = default;

juce::File SurgeGenerator::dataDir()
{
    auto env = juce::SystemStats::getEnvironmentVariable ("GLOOPY_SURGE_DATA", {});
    if (env.isNotEmpty()) return juce::File (env);
   #ifdef GLOOPY_ASSETS_DIR
    auto bundled = juce::File (GLOOPY_ASSETS_DIR).getChildFile ("surge-data");   // vendored factory data (dev tree)
    if (bundled.isDirectory()) return bundled;
   #endif
    // Installed layout: <exeDir>/assets/surge-data (see the install rule + Salamander piano).
    auto exeAdj = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                      .getParentDirectory().getChildFile ("assets").getChildFile ("surge-data");
    if (exeAdj.isDirectory()) return exeAdj;
    return {};
}

void SurgeGenerator::prepare (double sampleRate, int /*maxBlockSize*/)
{
    impl->ensureSurge();
    if (impl->surge)
    {
        impl->surge->setSamplerate ((float) sampleRate);
        impl->surge->time_data.tempo  = 120;
        impl->surge->time_data.ppqPos = 0;
    }
}

bool SurgeGenerator::loadPatch (const juce::File& fxp, juce::String& error)
{
    impl->ensureSurge();
    if (! impl->surge) { error = "Surge engine unavailable (no data dir)"; return false; }
    if (! fxp.existsAsFile()) { error = "Patch not found: " + fxp.getFullPathName(); return false; }
    impl->surge->loadPatchByPath (fxp.getFullPathName().toRawUTF8(), -1,
                                  fxp.getFileNameWithoutExtension().toRawUTF8());
    displayName = fxp.getFileNameWithoutExtension();
    return true;
}

int SurgeGenerator::numPatches() const
{
    return (impl->surge != nullptr) ? (int) impl->surge->storage.patch_list.size() : 0;
}

void SurgeGenerator::render (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                             int startSample, int numSamples)
{
    auto* surge = impl->surge.get();
    if (surge == nullptr) return;

    const int outCh = buffer.getNumChannels();
    auto it = midi.cbegin();
    const auto end = midi.cend();

    int produced = 0;
    while (produced < numSamples)
    {
        if (impl->carryAvail == 0)
        {
            // Fresh Surge block covering [blockStart, blockStart + BLOCK_SIZE).
            // Dispatch this block's MIDI (block-granular timing; < 1 ms at 44.1k).
            const int blockStart = startSample + produced;
            while (it != end && (*it).samplePosition < blockStart + BLOCK_SIZE)
            {
                const auto m = (*it).getMessage();
                if      (m.isNoteOn())     surge->playNote (0, (char) m.getNoteNumber(), (char) m.getVelocity(), 0);
                else if (m.isNoteOff())    surge->releaseNote (0, (char) m.getNoteNumber(), 0);
                else if (m.isController()) surge->channelController (0, m.getControllerNumber(), m.getControllerValue());
                else if (m.isPitchWheel()) surge->pitchBend (0, m.getPitchWheelValue() - 8192);
                ++it;
            }
            surge->process();
            std::copy (surge->output[0], surge->output[0] + BLOCK_SIZE, impl->carry[0]);
            std::copy (surge->output[1], surge->output[1] + BLOCK_SIZE, impl->carry[1]);
            impl->carryAvail = BLOCK_SIZE;
            impl->carryPos   = 0;
        }

        const int take = std::min (impl->carryAvail, numSamples - produced);
        const int dst  = startSample + produced;
        if (outCh >= 2)
        {
            buffer.addFrom (0, dst, impl->carry[0] + impl->carryPos, take);
            buffer.addFrom (1, dst, impl->carry[1] + impl->carryPos, take);
        }
        else if (outCh == 1)
        {
            buffer.addFrom (0, dst, impl->carry[0] + impl->carryPos, take, 0.5f);
            buffer.addFrom (0, dst, impl->carry[1] + impl->carryPos, take, 0.5f);
        }
        impl->carryPos   += take;
        impl->carryAvail -= take;
        produced         += take;
    }
}

void SurgeGenerator::allNotesOff()
{
    if (impl->surge) impl->surge->allNotesOff();
    impl->carryAvail = 0;
    impl->carryPos   = 0;
}

#else  // !GLOOPY_WITH_SURGE — inert stubs so a lean build still links.

struct SurgeGenerator::Impl {};
SurgeGenerator::SurgeGenerator() : impl (nullptr) {}
SurgeGenerator::~SurgeGenerator() = default;
juce::File SurgeGenerator::dataDir() { return {}; }
void SurgeGenerator::prepare (double, int) {}
bool SurgeGenerator::loadPatch (const juce::File&, juce::String& error) { error = "Surge not built in"; return false; }
int  SurgeGenerator::numPatches() const { return 0; }
void SurgeGenerator::render (juce::AudioBuffer<float>&, juce::MidiBuffer&, int, int) {}
void SurgeGenerator::allNotesOff() {}

#endif
