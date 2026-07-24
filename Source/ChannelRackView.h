// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <memory>
#include <functional>
#include "Channel.h"
#include "Pattern.h"
#include "Transport.h"

/** The Channel Rack: one row per channel, each with header controls (name, PR
    = open piano roll, mute, volume) and a step grid for the *current* pattern.
    The current pattern can change, so it's read through a provider callback. */
class ChannelRackView : public juce::Component,
                        private juce::Timer
{
public:
    ChannelRackView (std::vector<std::unique_ptr<Channel>>& channelsRef,
                     std::function<Pattern*()> patternProvider,
                     Transport& transportRef,
                     juce::CriticalSection& engineLockRef);
    ~ChannelRackView() override;

    /** Recreate per-row controls after channels are added/removed. */
    void rebuild();

    void setSelectedChannel (int channel) { selectedChannel = channel; repaint(); }

    int preferredHeight() const;

    /** Called after a step is toggled (arg = channel index). */
    std::function<void (int)> onPatternEdited;
    /** Called when a row's PR button is clicked (arg = channel index). */
    std::function<void (int)> onOpenPianoRoll;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    int   rowAt (float y) const;
    int   stepAt (float x) const;
    float cellWidth() const;

    static constexpr int headerWidth = 200;
    static constexpr int rowHeight    = 40;

    std::vector<std::unique_ptr<Channel>>& channels;
    std::function<Pattern*()>              getPattern;
    Transport&                             transport;
    juce::CriticalSection&                 engineLock;

    int selectedChannel { -1 };

    std::vector<std::unique_ptr<juce::TextButton>> muteButtons;
    std::vector<std::unique_ptr<juce::TextButton>> pianoButtons;
    std::vector<std::unique_ptr<juce::Slider>>     volSliders;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelRackView)
};
