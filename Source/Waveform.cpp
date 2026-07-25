// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

// Waveform thumbnail cache — min/max peaks per bucket for an audio file, so audio
// clips, the sampler, the sample browser, and external visualisers can draw a
// waveform without re-decoding. Keyed by resolved path + mtime + size (+ bucket
// count), so an edited file re-computes. Offline / message-thread only.

#include "MainComponent.h"

bool MainComponent::apiGetWaveform (const juce::String& path, int buckets,
                                    std::vector<float>& mins, std::vector<float>& maxs, double& durationSeconds)
{
    buckets = juce::jlimit (1, 8192, buckets);
    const juce::File f = resolveSamplePath (path);
    if (! f.existsAsFile()) return false;

    return callOnMessageThread ([&] () -> bool
    {
        const juce::int64 mtime = f.getLastModificationTime().toMilliseconds();
        const juce::int64 size  = f.getSize();
        const auto key = f.getFullPathName();

        auto it = waveformCache.find (key);
        if (it != waveformCache.end() && it->second.mtime == mtime
            && it->second.size == size && it->second.buckets == buckets)
        {
            mins = it->second.mins; maxs = it->second.maxs; durationSeconds = it->second.durationSeconds;
            return true;   // cache hit
        }

        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (f));
        if (reader == nullptr) return false;
        const int    n  = (int) reader->lengthInSamples;
        const int    ch = juce::jmax (1, (int) reader->numChannels);
        const double sr = reader->sampleRate > 0 ? reader->sampleRate : currentSampleRate;
        if (n <= 0) return false;

        juce::AudioBuffer<float> buf (ch, n);
        reader->read (&buf, 0, n, 0, true, true);

        CachedWave cw;
        cw.mtime = mtime; cw.size = size; cw.buckets = buckets;
        cw.durationSeconds = (double) n / sr;
        cw.mins.assign ((size_t) buckets, 0.0f);
        cw.maxs.assign ((size_t) buckets, 0.0f);
        for (int b = 0; b < buckets; ++b)
        {
            const int lo = (int) ((juce::int64) b * n / buckets);
            const int hi = juce::jmax (lo + 1, (int) ((juce::int64) (b + 1) * n / buckets));
            float mn = 0.0f, mx = 0.0f;
            for (int i = lo; i < hi && i < n; ++i)
            {
                float s = 0.0f;                             // mono mix
                for (int c = 0; c < ch; ++c) s += buf.getSample (c, i);
                s /= (float) ch;
                mn = juce::jmin (mn, s); mx = juce::jmax (mx, s);
            }
            cw.mins[(size_t) b] = mn; cw.maxs[(size_t) b] = mx;
        }

        mins = cw.mins; maxs = cw.maxs; durationSeconds = cw.durationSeconds;
        waveformCache[key] = std::move (cw);
        std::cout << "[waveform] cached " << f.getFileName() << " (" << buckets << " buckets)" << std::endl;
        return true;
    });
}
