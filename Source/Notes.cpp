// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

// Project notes — free-form markdown that travels with the song (composition
// notes.md). Message-thread state, edited by the notes window or the control API,
// serialised in toValueTree/loadFromTree and the composition (Composition.cpp).

#include "MainComponent.h"

juce::String MainComponent::apiGetProjectNotes()
{
    return callOnMessageThread ([&] { return projectNotes; });
}

void MainComponent::apiSetProjectNotes (const juce::String& text)
{
    callOnMessageThread ([&]
    {
        projectNotes = text;
        if (notesEditor.getText() != text)
            notesEditor.setText (text, juce::dontSendNotification);
        return true;
    });
}
