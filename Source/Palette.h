// SPDX-FileCopyrightText: 2026 Anthony Green <anthony@atgreen.org>
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <JuceHeader.h>

/** The app's single source of truth for colours and a couple of font helpers,
    so every view shares one cohesive, professional dark theme:
    neutral charcoals + one teal accent, with orange as a secondary accent. */
namespace Palette
{
    // Backgrounds (darkest → lightest).
    inline const juce::Colour bg         { 0xff17181b };  // app background
    inline const juce::Colour panel      { 0xff1f2126 };  // panel body
    inline const juce::Colour panelAlt   { 0xff24262c };  // alternating rows
    inline const juce::Colour header     { 0xff2b2e35 };  // header strips / buttons
    inline const juce::Colour headerHi   { 0xff353942 };  // hovered header
    inline const juce::Colour inset      { 0xff101114 };  // sunken areas (meters, displays)

    // Lines.
    inline const juce::Colour line       { 0xff343842 };  // borders / strong grid
    inline const juce::Colour lineSoft   { 0xff262a31 };  // subtle grid

    // Text.
    inline const juce::Colour text       { 0xffc9ccd4 };
    inline const juce::Colour textDim    { 0xff7f838d };
    inline const juce::Colour textBright { 0xfff0f2f6 };

    // Accents.
    inline const juce::Colour accent     { 0xff37c0d4 };  // teal (primary)
    inline const juce::Colour accentDim  { 0xff236f79 };
    inline const juce::Colour warm       { 0xffe6a23c };  // orange (secondary)
    inline const juce::Colour green      { 0xff5fbf6a };
    inline const juce::Colour red        { 0xffe05a52 };
    inline const juce::Colour solo       { 0xffe6c53c };
    inline const juce::Colour mute       { 0xffcf4b43 };
    inline const juce::Colour playhead   { 0xffff6b5f };

    /** Uppercased, kerned small font for section headers. */
    inline juce::Font sectionFont()
    {
        return juce::Font (juce::FontOptions (10.5f, juce::Font::bold)).withExtraKerningFactor (0.12f);
    }

    inline juce::Font valueFont (float h = 15.0f)
    {
        return juce::Font (juce::FontOptions (h, juce::Font::bold));
    }
}
