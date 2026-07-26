// Isolation probe for embedding Surge XT's synth core (surge-common) headless.
// Proves: link surge::surge-common, construct SurgeSynthesizer with a data path
// (factory patches scanned), play a note, render blocks, get non-silent audio.
// No Gloopy code involved — this validates the engine + the embed contract before
// wiring it into Gloopy's build (see docs/surge-embed.md slice 1).
#include "SurgeSynthesizer.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

struct ProbeLayer : public SurgeSynthesizer::PluginLayer
{
    void surgeParameterUpdated(const SurgeSynthesizer::ID &, float) override {}
    void surgeMacroUpdated(long, float) override {}
};

int main(int argc, char **argv)
{
    const std::string dataPath = (argc > 1) ? argv[1] : "/home/green/git/surge/resources/data";

    ProbeLayer layer;
    auto surge = std::make_unique<SurgeSynthesizer>(&layer, dataPath);
    surge->setSamplerate(44100.f);
    surge->time_data.tempo = 120;
    surge->time_data.ppqPos = 0;

    const int nPatches = (int)surge->storage.patch_list.size();
    const int nCats = (int)surge->storage.patch_category.size();

    // Play middle C on the default (init) patch — already makes sound.
    surge->playNote(0, 60, 100, 0);

    double sumsq = 0.0;
    long n = 0;
    for (int b = 0; b < 300; ++b)
    {
        surge->process();
        for (int c = 0; c < N_OUTPUTS; ++c)
            for (int i = 0; i < BLOCK_SIZE; ++i)
            {
                const float s = surge->output[c][i];
                sumsq += (double)s * s;
                ++n;
            }
        if (b == 200)
            surge->releaseNote(0, 60, 0);
    }
    const double rms = std::sqrt(sumsq / (double)(n > 0 ? n : 1));

    std::printf("surge-probe: patches=%d categories=%d blockSize=%d rms=%.6f\n",
                nPatches, nCats, BLOCK_SIZE, rms);
    if (nPatches > 0 && rms > 1e-4)
    {
        std::printf("surge-probe: PASS (factory patches scanned + non-silent render)\n");
        return 0;
    }
    std::printf("surge-probe: FAIL (patches=%d rms=%.6f)\n", nPatches, rms);
    return 1;
}
