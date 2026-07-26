// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <memory>
#include <functional>
#include "MixerTrack.h"
#include "Effects.h"

/** The Mixer: a row of channel strips (master + inserts), each with a fader,
    pan, mute/solo, a peak meter and an FX menu; plus a bottom editor showing the
    selected effect's parameters. */
class MixerView : public juce::Component,
                  private juce::Timer
{
public:
    MixerView (std::vector<std::unique_ptr<MixerTrack>>& tracksRef,
               juce::CriticalSection& engineLockRef,
               std::function<std::unique_ptr<Effect> (const juce::String&)> makeEffectFn);
    ~MixerView() override;

    void rebuild();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;   // right-click FX param -> MIDI Learn

    /** Right-click "MIDI Learn" on a fader/pan/FX-param -> arm learn for that ParamModel
        target id (e.g. "insert/0/volume", "effect/1/0/Cutoff"). Wired by the owner to
        apiMidiLearn so the desktop and the API arm the same learn. */
    std::function<void (const juce::String&)> onMidiLearn;
    /** "Add LFO..." on the same right-click menu -> owner resolves the target's current
        value as the LFO centre and calls apiSetModulation (target, rate, depth, shape). */
    std::function<void (const juce::String&, float, float, int, float, float, bool, float)> onSetModulation;   // target, rate, depth, shape, syncBeats, phase, unipolar, slewMs
    std::function<void (const juce::String&)>                     onRemoveModulation;
    /** "Automate at playhead" / "Clear automation" — id-addressed automation (the same
        ParamModel id MIDI-learn and the LFO use), authored one keyframe at a time. */
    std::function<void (const juce::String&)>                     onAutomatePoint;
    std::function<void (const juce::String&)>                     onClearAutomation;
    /** Controller mapping: report the CC/OSC source bound to this target (empty if none)
        and remove that binding — so a MIDI-learned mapping can be seen and cleared from
        the desktop, not only the API. */
    std::function<juce::String (const juce::String&)>            onControllerSourceFor;   // "" if unmapped
    std::function<void (const juce::String&)>                    onRemoveControllerMap;

    // Control groups (VCA-lite): the strip name's right-click menu creates/assigns
    // groups and rides the group fader. Wired by the owner to the apiControlGroup* calls.
    struct GroupState { juce::String name; float gain; bool mute; };
    std::function<std::vector<GroupState>()>       onListGroups;   // all groups + their state
    std::function<juce::String (int)>              onInsertGroup;  // an insert's current group ("" = none)
    std::function<void (int, const juce::String&)> onAssignGroup;  // assign insert -> group ("" clears; defines if new)
    std::function<void (const juce::String&, float)> onGroupGain;  // set a group's fader
    std::function<void (const juce::String&, bool)>  onGroupMute;  // set a group's mute
    std::function<void (const juce::String&)>        onRemoveGroup;

    // Plugin hooks (wired by the owner).
    std::function<void()>                                       ensurePlugins;
    std::function<juce::Array<juce::PluginDescription>()>       getEffectPlugins;
    std::function<std::unique_ptr<Effect> (const juce::PluginDescription&)> makePluginEffect;
    std::function<void (juce::AudioProcessor*, const juce::String&)> onOpenPluginEditor;
    std::function<void()>                                       onBeforeStructuralChange;

private:
    class Strip;   // fwd

    void timerCallback() override;
    void showParamMenu (const juce::String& target, const juce::String& label);   // MIDI Learn / LFO
    void promptAddLfo (const juce::String& target);                               // rate/depth/shape prompt
    void showGroupMenu (int insertIndex);                                         // VCA-lite: assign / gain / mute
    void promptNewGroup (int insertIndex);                                        // name entry -> assign
    void showFxMenu (int trackIndex);
    void selectEffect (int trackIndex, int effectIndex);
    void rebuildEditor();

    static constexpr int stripWidth  = 96;
    static constexpr int editorHeight = 132;

    std::vector<std::unique_ptr<MixerTrack>>& tracks;
    juce::CriticalSection&                    engineLock;
    std::function<std::unique_ptr<Effect> (const juce::String&)> makeEffect;

    juce::Viewport stripViewport;
    juce::Component stripHolder;
    std::vector<std::unique_ptr<Strip>> strips;

    // Effect editor (bottom).
    juce::Label      editorTitle;
    juce::TextButton bypassButton { "Bypass" };
    juce::TextButton removeButton { "Remove" };
    juce::TextButton openEditorButton { "Plugin UI" };
    std::vector<std::unique_ptr<juce::Slider>> paramSliders;
    std::vector<std::unique_ptr<juce::Label>>  paramLabels;
    std::vector<juce::String>                  paramTargets;   // ParamModel id per slider (for MIDI Learn)

    int selectedTrack  { -1 };
    int selectedEffect { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerView)
};
