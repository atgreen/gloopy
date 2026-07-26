// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

// Export profiles — named render targets stored in the composition, so "give me
// the stems" or "bounce the chorus" is a one-call, reproducible operation instead
// of re-passing render coordinates each time. Profiles reuse the offline bounce
// (apiRenderToFile) and the timeline-location model (apiResolveRange) rather than
// re-implementing rendering. Output lands in <project>/exports/ with deterministic
// filenames so a composition repo can diff its deliverables.
//
// The container follows the profile's `format` ("flac" -> .flac, else .wav); the
// render encoder is chosen from the output extension by apiRenderToFile.
//
// target: "mix"   -> whole song                -> exports/<name>.<ext>
//         "range" -> named range (rangeName)   -> exports/<name>.<ext>
//         "track" -> one track (trackId)       -> exports/<name>.<ext>
//         "stems" -> every instrument track    -> exports/<name>/<id>-<slug>.<ext>

#include "MainComponent.h"

namespace
{
juce::String slug (const juce::String& s)
{
    juce::String o;
    for (auto c : s.toLowerCase())
        o << (juce::CharacterFunctions::isLetterOrDigit (c) ? juce::String::charToString (c) : juce::String ("-"));
    while (o.contains ("--")) o = o.replace ("--", "-");
    o = o.trimCharactersAtStart ("-").trimCharactersAtEnd ("-");
    return o.isEmpty() ? "export" : o;
}
}

// Bounce just the current transport loop window to a WAV/FLAC (the "export selection"
// action). Fails if no loop is set or it is empty; otherwise reuses the offline bounce
// with the loop's [start,end) beat range and a short tail so effect/reverb tails aren't
// clipped. The encoder is picked from the output extension by apiRenderToFile.
bool MainComponent::apiExportLoopRegion (const juce::String& path)
{
    if (! transport.isLoopEnabled()) return false;
    const double s = transport.getLoopStartBeats();
    const double e = transport.getLoopEndBeats();
    if (e <= s + 1.0e-9) return false;                       // empty / inverted loop
    return apiRenderToFile (path, 1.0, s, e, false, -1);
}

// Bounce a single track (its clips through its own insert chain, soloed) to a WAV/FLAC —
// a stem for mixing/collab. Fails if the track id is unknown. Reuses the offline bounce's
// single-soloed-track path with a short tail so effect tails aren't clipped.
bool MainComponent::apiExportTrack (int trackId, const juce::String& path,
                                    double startBeat, double endBeat)
{
    if (resolveTrack (trackId) == nullptr) return false;
    return apiRenderToFile (path, 2.0, startBeat, endBeat, true, trackId);
}

// Bounce every INSTRUMENT track to its own stem WAV in `dirPath`, named
// "<id>-<slug>.wav". Returns the list of files written. The engine must already be
// prepared (the live app always is; the headless CLI calls prepareToPlay first).
std::vector<juce::String> MainComponent::apiExportStems (const juce::String& dirPath,
                                                         double startBeat, double endBeat)
{
    std::vector<juce::String> out;
    juce::File dir (dirPath);
    dir.createDirectory();
    for (auto& t : apiListTracks())
        if (t.type == "instrument")
        {
            auto slug = t.name.toLowerCase().retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789-");
            auto f = dir.getChildFile (juce::String (t.id) + "-" + (slug.isEmpty() ? "track" : slug) + ".wav");
            if (apiExportTrack (t.id, f.getFullPathName(), startBeat, endBeat)) out.push_back (f.getFullPathName());
        }
    return out;
}

bool MainComponent::apiDefineExportProfile (const juce::String& name, const juce::String& target,
                                            const juce::String& rangeName, const juce::String& format,
                                            int trackId, double tailSeconds)
{
    if (name.trim().isEmpty()) return false;
    const ExportProfile p { name, target, rangeName, format, trackId, tailSeconds };
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        const juce::ScopedLock sl (engineLock);
        auto it = std::find_if (exportProfiles.begin(), exportProfiles.end(),
                                [&] (const ExportProfile& e) { return e.name == p.name; });
        if (it != exportProfiles.end()) *it = p;      // upsert
        else                            exportProfiles.push_back (p);
        return true;
    });
}

std::vector<MainComponent::ExportProfile> MainComponent::apiListExportProfiles()
{
    return callOnMessageThread ([&] { const juce::ScopedLock sl (engineLock); return exportProfiles; });
}

bool MainComponent::apiRemoveExportProfile (const juce::String& name)
{
    return callOnMessageThread ([&] () -> bool
    {
        pushUndoSnapshot();
        const juce::ScopedLock sl (engineLock);
        const auto before = exportProfiles.size();
        exportProfiles.erase (std::remove_if (exportProfiles.begin(), exportProfiles.end(),
                                  [&] (const ExportProfile& e) { return e.name == name; }),
                              exportProfiles.end());
        return exportProfiles.size() != before;
    });
}

bool MainComponent::apiRunExport (const juce::String& name, const juce::String& outDirOverride,
                                  std::vector<juce::String>& filesOut)
{
    filesOut.clear();

    // Snapshot the profile (brief lock) — the render itself must not hold the lock,
    // apiRenderToFile takes it for the whole bounce.
    ExportProfile p;
    {
        const juce::ScopedLock sl (engineLock);
        auto it = std::find_if (exportProfiles.begin(), exportProfiles.end(),
                                [&] (const ExportProfile& e) { return e.name == name; });
        if (it == exportProfiles.end()) return false;
        p = *it;
    }

    // Resolve <project>/exports (or the override).
    juce::File base = outDirOverride.isNotEmpty()
        ? juce::File (juce::File::isAbsolutePath (outDirOverride) ? outDirOverride
                        : juce::File::getCurrentWorkingDirectory().getChildFile (outDirOverride).getFullPathName())
        : (currentProjectFile.exists() ? currentProjectFile.getParentDirectory()
                                       : juce::File::getCurrentWorkingDirectory()).getChildFile ("exports");
    base.createDirectory();

    const double tail = p.tailSeconds;
    const juce::String nm = slug (p.name);
    // Output container from the profile's format (default WAV). apiRenderToFile picks
    // the encoder from the file extension, so here we only choose the suffix.
    const juce::String ext = p.format.trim().toLowerCase() == "flac" ? ".flac" : ".wav";

    auto renderOne = [&] (const juce::File& f, double s, double e, bool hasTrack, int trackId) -> bool
    {
        f.getParentDirectory().createDirectory();
        if (! apiRenderToFile (f.getFullPathName(), tail, s, e, hasTrack, trackId)) return false;
        filesOut.push_back (f.getFullPathName());
        return true;
    };

    if (p.target == "mix")
        return renderOne (base.getChildFile (nm + ext), 0.0, 0.0, false, 0);

    if (p.target == "range")
    {
        double s = 0, e = 0;
        if (! apiResolveRange (p.rangeName, s, e)) return false;
        return renderOne (base.getChildFile (nm + ext), s, e, false, 0);
    }

    if (p.target == "track")
        return renderOne (base.getChildFile (nm + ext), 0.0, 0.0, true, p.trackId);

    if (p.target == "stems")
    {
        // One file per instrument track. Gather ids first (message thread), then render.
        std::vector<std::pair<int, juce::String>> stems;
        for (auto& t : apiListTracks())
            if (t.type == "instrument")
                stems.push_back ({ t.id, slug (t.name) });
        if (stems.empty()) return false;
        auto dir = base.getChildFile (nm);
        for (auto& st : stems)
            if (! renderOne (dir.getChildFile (juce::String (st.first) + "-" + st.second + ext),
                             0.0, 0.0, true, st.first))
                return false;
        return true;
    }

    return false;   // unknown target
}
