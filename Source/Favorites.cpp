// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

// Favorites — a user-level library of go-to browser items (plugins, samples, presets,
// templates) that the user pins for quick access. Like presets and user templates, they
// live OUTSIDE the composition (they follow the user, not the song), in a small readable
// text file under <userAppData>/Gloopy/favorites.txt (GLOOPY_FAVORITES_PATH override).
//
// One favorite per line as three tab-separated fields: kind \t ref \t label
//   kind  = plugin | sample | preset | template  (chooses the action on click)
//   ref   = the argument that action needs (plugin identifier / file path / patch path / template name)
//   label = the display text in the browser row
// This is a user library, not project state, so there's no ValueTree/composition mapping.

#include "MainComponent.h"

namespace
{
// A CriticalSection guarding the favorites file — the gRPC thread and the message thread
// can both touch it, and it's plain file I/O (no audio engine), so a small lock is enough.
juce::CriticalSection& favoritesLock()
{
    static juce::CriticalSection lock;
    return lock;
}

// Tabs and newlines can't appear in our fields (identifiers/paths/labels are single-line);
// strip them defensively so a stray value can't corrupt the line format.
juce::String sanitiseField (const juce::String& s)
{
    return s.replaceCharacters ("\t\r\n", "   ").trim();
}
}

juce::File MainComponent::favoritesFile() const
{
    auto base = juce::SystemStats::getEnvironmentVariable ("GLOOPY_FAVORITES_PATH", {});
    if (base.isNotEmpty())
        return juce::File (base);
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("Gloopy").getChildFile ("favorites.txt");
}

std::vector<MainComponent::FavoriteInfo> MainComponent::apiListFavorites() const
{
    const juce::ScopedLock sl (favoritesLock());
    std::vector<FavoriteInfo> out;
    const auto file = favoritesFile();
    if (! file.existsAsFile())
        return out;

    juce::StringArray lines;
    lines.addLines (file.loadFileAsString());
    for (auto& line : lines)
    {
        if (line.trim().isEmpty())
            continue;
        auto parts = juce::StringArray::fromTokens (line, "\t", "");
        if (parts.size() < 2)
            continue;                                   // need at least kind + ref
        FavoriteInfo f;
        f.kind  = parts[0].trim();
        f.ref   = parts[1];                             // paths keep their spacing; only tabs split
        f.label = parts.size() >= 3 ? parts[2] : parts[1];
        if (f.kind.isNotEmpty() && f.ref.isNotEmpty())
            out.push_back (std::move (f));
    }
    return out;
}

bool MainComponent::apiAddFavorite (const juce::String& kind, const juce::String& ref, const juce::String& label)
{
    const auto k = sanitiseField (kind), r = sanitiseField (ref);
    if (k.isEmpty() || r.isEmpty())
        return false;
    auto l = sanitiseField (label);
    if (l.isEmpty())
        l = r;

    auto favs = apiListFavorites();
    for (auto& f : favs)                                // dedupe by kind+ref (idempotent add)
        if (f.kind == k && f.ref == r)
            return true;

    const juce::ScopedLock sl (favoritesLock());
    const auto file = favoritesFile();
    file.getParentDirectory().createDirectory();
    return file.appendText (k + "\t" + r + "\t" + l + "\n");
}

bool MainComponent::apiRemoveFavorite (const juce::String& kind, const juce::String& ref)
{
    const auto k = sanitiseField (kind), r = sanitiseField (ref);
    auto favs = apiListFavorites();

    const juce::ScopedLock sl (favoritesLock());
    juce::String rebuilt;
    bool removed = false;
    for (auto& f : favs)
    {
        if (f.kind == k && f.ref == r) { removed = true; continue; }
        rebuilt << f.kind << "\t" << f.ref << "\t" << f.label << "\n";
    }
    if (removed)
        favoritesFile().replaceWithText (rebuilt);
    return removed;
}
