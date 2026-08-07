// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <ableton/LinkAudio.hpp>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <cstdint>
#include <cmath>

/** Ableton Link epic (ideas.md). Gloopy uses the LinkAudio base (a superset of plain Link): as well
    as sharing tempo/beat/phase with Live, other DAWs, iOS apps and hardware on the LAN, it can
    announce and receive network AUDIO channels aligned to the shared beat grid.

    Stage A (here): join the session + enable audio, and expose the peer/channel counts for the
    toolbar. Sending Gloopy's master as a channel (LinkAudioSink) and receiving a peer's channel
    into a track (LinkAudioSource) are the following stages — both reuse the RT-safe ring pattern
    from JackCapture. LinkAudio runs its own discovery/transport threads; the accessors below are
    message-thread (the RT-safe capture/commit path lives in the audio callback in later stages). */
class LinkController
{
public:
    /** A view of one audio channel another peer is publishing (that we could subscribe to). */
    struct Channel { std::string name; std::string peerName; };

    explicit LinkController (double initialBpm = 120.0, std::string peerName = "Gloopy")
        : link (initialBpm, std::move (peerName)) {}

    /** Join / leave the session. Enables both Link sync and LinkAudio network-audio sharing. */
    void setEnabled (bool shouldBeEnabled)
    {
        link.enable (shouldBeEnabled);
        link.enableLinkAudio (shouldBeEnabled);
        audioSending.store (shouldBeEnabled, std::memory_order_relaxed);
    }
    bool isEnabled() const { return link.isEnabled(); }

    /** Create the master send channel once the audio block size is known (message thread). The
        sink lives for this object's lifetime so the audio thread never races its destruction; the
        channel is only announced to peers while Link is enabled. */
    void prepareAudio (int maxFrames)
    {
        const size_t maxSamples = (size_t) juceMax (maxFrames, 64) * 2;   // stereo interleaved
        if (masterSink == nullptr)
            masterSink = std::make_unique<ableton::LinkAudioSink> (link, "Gloopy Master", maxSamples);
        else
            masterSink->requestMaxNumSamples (maxSamples);
    }

    /** Audio thread: broadcast the just-mixed stereo master to any subscribed peer, aligned to the
        Link beat grid. RT-safe (captureAudioSessionState / BufferHandle / commit are all RT-safe);
        a no-op when Link is off or no peer has subscribed a source to our channel. */
    void sendMaster (const float* left, const float* right, int numFrames, double sampleRate)
    {
        auto* sink = masterSink.get();
        if (sink == nullptr || ! audioSending.load (std::memory_order_relaxed)) return;

        const auto sessionState = link.captureAudioSessionState();
        const double quantum    = 4.0;
        const double beats      = sessionState.beatAtTime (link.clock().micros(), quantum);

        ableton::LinkAudioSink::BufferHandle handle (*sink);
        if (! handle) return;                                  // no subscriber -> nothing to send
        const int cap = (int) (handle.maxNumSamples / 2);
        const int n   = numFrames < cap ? numFrames : cap;
        for (int i = 0; i < n; ++i)
        {
            handle.samples[2 * i]     = toI16 (left[i]);
            handle.samples[2 * i + 1] = toI16 (right[i]);
        }
        handle.commit (sessionState, beats, quantum, (size_t) n, 2, (uint32_t) sampleRate);
    }

    int numPeers() const { return (int) link.numPeers(); }

    /** Audio channels currently published by other peers in the session. Message thread. */
    std::vector<Channel> channels() const
    {
        std::vector<Channel> out;
        for (const auto& c : link.channels()) out.push_back ({ c.name, c.peerName });
        return out;
    }
    int numChannels() const { return (int) link.channels().size(); }

    /** Access the underlying LinkAudio (for the source/receive audio path in the next stage). */
    ableton::LinkAudio& audio() { return link; }

private:
    static inline int      juceMax (int a, int b) { return a > b ? a : b; }
    static inline int16_t  toI16 (float x)
    {
        const float y = x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
        return (int16_t) std::lround (y * 32767.0f);
    }

    ableton::LinkAudio link;
    std::unique_ptr<ableton::LinkAudioSink> masterSink;   // announces "Gloopy Master"; lives for the object's lifetime
    std::atomic<bool> audioSending { false };
};
