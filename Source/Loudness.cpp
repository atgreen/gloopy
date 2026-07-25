// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

// Offline loudness analysis of a rendered file — peak, true-peak, RMS, and
// integrated LUFS (ITU-R BS.1770 / EBU R128). Pure offline DSP on a decoded buffer:
// no audio thread, no juce_dsp dependency (the K-weighting is hand-rolled RBJ
// biquads, correct at any sample rate; true-peak uses a small windowed-sinc 4x
// oversampler since linear interpolation can't reveal inter-sample overshoot).

#include "MainComponent.h"
#include <cmath>
#include <vector>

namespace
{
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;   // normalised (a0 = 1)
    void process (std::vector<float>& x) const
    {
        double z1 = 0, z2 = 0;   // transposed direct form II
        for (auto& s : x)
        {
            const double in = s;
            const double out = b0 * in + z1;
            z1 = b1 * in - a1 * out + z2;
            z2 = b2 * in - a2 * out;
            s = (float) out;
        }
    }
};

// RBJ high-shelf (with gain in dB) — K-weighting pre-filter.
Biquad highShelf (double fs, double f0, double q, double gainDb)
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w0 = 2.0 * juce::MathConstants<double>::pi * f0 / fs;
    const double c = std::cos (w0), s = std::sin (w0);
    const double alpha = s / (2.0 * q);
    const double sa = 2.0 * std::sqrt (A) * alpha;
    const double a0 =        (A + 1) - (A - 1) * c + sa;
    Biquad bq;
    bq.b0 = ( A * ((A + 1) + (A - 1) * c + sa) ) / a0;
    bq.b1 = ( -2 * A * ((A - 1) + (A + 1) * c) ) / a0;
    bq.b2 = ( A * ((A + 1) + (A - 1) * c - sa) ) / a0;
    bq.a1 = ( 2 * ((A - 1) - (A + 1) * c) ) / a0;
    bq.a2 = ( (A + 1) - (A - 1) * c - sa ) / a0;
    return bq;
}

// RBJ high-pass — K-weighting RLB stage.
Biquad highPass (double fs, double f0, double q)
{
    const double w0 = 2.0 * juce::MathConstants<double>::pi * f0 / fs;
    const double c = std::cos (w0), s = std::sin (w0);
    const double alpha = s / (2.0 * q);
    const double a0 = 1 + alpha;
    Biquad bq;
    bq.b0 = ((1 + c) / 2) / a0;
    bq.b1 = (-(1 + c)) / a0;
    bq.b2 = ((1 + c) / 2) / a0;
    bq.a1 = (-2 * c) / a0;
    bq.a2 = (1 - alpha) / a0;
    return bq;
}

double toDb (double lin) { return lin > 1.0e-12 ? 20.0 * std::log10 (lin) : -144.0; }

// True-peak via 4x windowed-sinc oversampling on one channel; returns linear peak.
double truePeakChannel (const float* x, int n)
{
    double peak = 0.0;
    for (int i = 0; i < n; ++i) peak = juce::jmax (peak, (double) std::abs (x[i]));

    constexpr int HALF = 8;                 // 16-tap kernel
    for (int phaseIdx = 1; phaseIdx <= 3; ++phaseIdx)
    {
        const double frac = phaseIdx * 0.25;
        for (int i = 0; i < n; ++i)
        {
            double acc = 0.0, wsum = 0.0;
            for (int k = -HALF + 1; k <= HALF; ++k)
            {
                const int idx = i + k;
                if (idx < 0 || idx >= n) continue;
                const double t = k - frac;
                double sinc = (std::abs (t) < 1e-9) ? 1.0
                              : std::sin (juce::MathConstants<double>::pi * t) / (juce::MathConstants<double>::pi * t);
                const double win = 0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * t / HALF);  // Hann
                const double w = sinc * win;
                acc += x[idx] * w; wsum += w;
            }
            if (wsum > 1e-9) peak = juce::jmax (peak, std::abs (acc / wsum));
        }
    }
    return peak;
}
} // namespace

bool MainComponent::apiAnalyzeFile (const juce::String& path, LoudnessReport& out)
{
    juce::File f = juce::File::isAbsolutePath (path) ? juce::File (path)
                     : juce::File::getCurrentWorkingDirectory().getChildFile (path);
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (f));
    if (reader == nullptr) return false;

    const int    ch = juce::jmax (1, (int) reader->numChannels);
    const int    n  = (int) reader->lengthInSamples;
    const double fs = reader->sampleRate > 0 ? reader->sampleRate : currentSampleRate;
    if (n <= 0) return false;

    juce::AudioBuffer<float> buf (ch, n);
    reader->read (&buf, 0, n, 0, true, true);

    // --- peak, RMS, true-peak ---
    double peak = 0.0, sumSq = 0.0, truePeak = 0.0;
    for (int c = 0; c < ch; ++c)
    {
        const float* d = buf.getReadPointer (c);
        for (int i = 0; i < n; ++i) { const double s = d[i]; peak = juce::jmax (peak, std::abs (s)); sumSq += s * s; }
        truePeak = juce::jmax (truePeak, truePeakChannel (d, n));
    }
    const double rms = std::sqrt (sumSq / (double) (n * ch));

    // --- integrated LUFS (BS.1770): K-weight each channel, gate 400ms blocks ---
    const int lufsCh = juce::jmin (ch, 2);   // stereo weights 1.0; ignore >2 for now
    const auto shelf = highShelf (fs, 1681.974450955533, 0.7071752369554196, 3.999843853973347);
    const auto hp    = highPass  (fs, 38.13547087602444, 0.5003270373238773);

    std::vector<std::vector<float>> z ((size_t) lufsCh);
    for (int c = 0; c < lufsCh; ++c)
    {
        z[(size_t) c].assign (buf.getReadPointer (c), buf.getReadPointer (c) + n);
        shelf.process (z[(size_t) c]);
        hp.process (z[(size_t) c]);
    }

    const int blockLen = (int) std::round (0.400 * fs);
    const int hop      = (int) std::round (0.100 * fs);   // 75% overlap
    std::vector<double> blockZ;                            // channel-summed mean-square per block
    if (n >= blockLen && hop > 0)
        for (int start = 0; start + blockLen <= n; start += hop)
        {
            double zsum = 0.0;
            for (int c = 0; c < lufsCh; ++c)
            {
                double ms = 0.0; const float* d = z[(size_t) c].data();
                for (int i = start; i < start + blockLen; ++i) ms += (double) d[i] * d[i];
                zsum += ms / blockLen;   // weight 1.0 per channel
            }
            blockZ.push_back (zsum);
        }

    auto lufsOf = [] (double z0) { return z0 > 1e-12 ? -0.691 + 10.0 * std::log10 (z0) : -144.0; };

    double lufs = -144.0;
    if (! blockZ.empty())
    {
        // Absolute gate at -70 LUFS.
        std::vector<double> g1;
        for (double zb : blockZ) if (lufsOf (zb) > -70.0) g1.push_back (zb);
        if (! g1.empty())
        {
            double mean1 = 0.0; for (double zb : g1) mean1 += zb; mean1 /= (double) g1.size();
            const double relThresh = lufsOf (mean1) - 10.0;   // relative gate -10 LU
            double sum2 = 0.0; int cnt2 = 0;
            for (double zb : g1) if (lufsOf (zb) > relThresh) { sum2 += zb; ++cnt2; }
            if (cnt2 > 0) lufs = lufsOf (sum2 / (double) cnt2);
        }
    }

    out.peakDbfs     = (float) toDb (peak);
    out.truePeakDbtp = (float) toDb (truePeak);
    out.rmsDbfs      = (float) toDb (rms);
    out.lufs         = (float) lufs;
    std::cout << "[loudness] " << f.getFileName() << "  peak=" << out.peakDbfs
              << " tp=" << out.truePeakDbtp << " rms=" << out.rmsDbfs << " lufs=" << out.lufs << std::endl;
    return true;
}
