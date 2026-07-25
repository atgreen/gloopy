// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

// Real-time diagnostics — a read-only snapshot of the engine's health: audio device
// settings, live audio-callback timing + DSP load, dropped blocks (engine-lock
// contention), and the last offline render's speed. The audio-thread counters are
// plain relaxed atomics written in getNextAudioBlock / apiRenderToFile (see
// MainComponent.cpp); this just reads them, so the audio path stays lock-free.

#include "MainComponent.h"

MainComponent::DiagSnap MainComponent::apiGetDiagnostics()
{
    return callOnMessageThread ([&]
    {
        DiagSnap d {};
        d.sampleRate = currentSampleRate;
        d.blockSize  = currentBlockSize;
        d.inputs = d.outputs = 0;
        if (auto* dev = deviceManager.getCurrentAudioDevice())
        {
            d.inputs  = dev->getActiveInputChannels().countNumberOfSetBits();
            d.outputs = dev->getActiveOutputChannels().countNumberOfSetBits();
            if (dev->getCurrentSampleRate() > 0)     d.sampleRate = dev->getCurrentSampleRate();
            if (dev->getCurrentBufferSizeSamples() > 0) d.blockSize = dev->getCurrentBufferSizeSamples();
        }
        d.callbackUs    = diagLastCallbackUs.load (std::memory_order_relaxed);
        d.maxCallbackUs = diagMaxCallbackUs.load (std::memory_order_relaxed);
        d.dropouts      = (long long) diagDropouts.load (std::memory_order_relaxed);
        d.renderSpeedX  = diagRenderSpeedX.load (std::memory_order_relaxed);

        // DSP load = callback time / the block's real-time budget.
        const double budgetUs = d.blockSize > 0 && d.sampleRate > 0
                                  ? (double) d.blockSize / d.sampleRate * 1.0e6 : 0.0;
        d.dspLoad = budgetUs > 0.0 ? d.callbackUs / budgetUs : 0.0;
        return d;
    });
}
