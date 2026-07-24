// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <memory>
#include <map>
#include <cmath>
#include "Generator.h"

/** A native SFZ sampler generator — a pragmatic subset of the SFZ spec that
    covers the opcodes the bundled libraries (Virtual Playing Orchestra strings,
    Salamander Grand Piano) actually use. This removes the sfizz plugin
    dependency and the reverse-engineered-state hack (see tools/sfizz-state.py).

    Supported: `<global>/<master>/<group>/<region>` hierarchy with opcode
    inheritance; `sample`, `lokey/hikey/key`, `pitch_keycenter`, `lovel/hivel`,
    `volume` (dB), `pan`, `tune` (cents), `transpose`, `offset`, `loop_mode`
    (one_shot / loop_continuous), `loop_start/loop_end`, and `ampeg_*`
    (attack/decay/sustain/release). Note names (c4 = 60) and integers both parse.

    Samples referenced by regions are preloaded (deduplicated) at load time on
    the message thread; playback is lock-free. */
class SfzInstrument : public Generator
{
public:
    void prepare (double sampleRate, int /*maxBlockSize*/) override
    {
        deviceRate = sampleRate;
    }

    /** Parse an .sfz file and preload its samples. Call on the message thread
        before installing the generator. Returns false (with @p error) on failure. */
    bool loadSfz (const juce::File& sfz, juce::AudioFormatManager& fm, juce::String& error)
    {
        if (! sfz.existsAsFile())
        {
            error = "SFZ file not found: " + sfz.getFullPathName();
            return false;
        }

        std::vector<Region> parsed;
        if (! parse (sfz, parsed, error))
            return false;

        // Preload unique samples (deduplicated), bounded so a huge library can't
        // exhaust memory. Regions whose sample is dropped are logged and skipped.
        struct Cached { std::shared_ptr<juce::AudioBuffer<float>> buf; double rate; };
        std::map<juce::String, Cached> cache;
        juce::int64 totalBytes = 0;
        const juce::int64 capBytes = (juce::int64) 900 * 1024 * 1024;   // ~900 MB decoded
        int dropped = 0, loaded = 0;

        for (auto& r : parsed)
        {
            auto it = cache.find (r.samplePath);
            if (it != cache.end()) { r.sample = it->second.buf; r.sourceRate = it->second.rate; continue; }

            juce::File sampleFile (r.samplePath);
            std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (sampleFile));
            if (reader == nullptr || reader->lengthInSamples <= 0)
            {
                ++dropped;
                continue;
            }

            const juce::int64 bytes = reader->lengthInSamples * (juce::int64) juce::jmax (1u, reader->numChannels) * 4;
            if (totalBytes + bytes > capBytes)
            {
                ++dropped;
                continue;
            }
            totalBytes += bytes;

            auto buf = std::make_shared<juce::AudioBuffer<float>> (
                (int) juce::jmax (1u, reader->numChannels), (int) reader->lengthInSamples);
            reader->read (buf.get(), 0, (int) reader->lengthInSamples, 0, true, true);
            cache[r.samplePath] = { buf, reader->sampleRate };
            r.sample = buf;
            r.sourceRate = reader->sampleRate;
            ++loaded;
        }

        // Keep only regions whose sample actually loaded.
        std::vector<Region> live;
        for (auto& r : parsed)
            if (r.sample != nullptr) live.push_back (std::move (r));

        if (live.empty())
        {
            error = "No playable regions loaded from " + sfz.getFileName()
                  + " (" + juce::String (dropped) + " samples missing/over cap)";
            return false;
        }

        regions   = std::move (live);
        sfzPath    = sfz.getFullPathName();
        displayName = sfz.getFileNameWithoutExtension();
        for (auto& v : voices) v.active = false;

        std::cout << "[sfz] " << displayName << ": " << regions.size() << " regions, "
                  << loaded << " samples (" << (totalBytes >> 20) << " MB)";
        if (dropped > 0) std::cout << ", " << dropped << " skipped (missing or over cap)";
        std::cout << std::endl;
        return true;
    }

    const juce::String& getSfzPath() const { return sfzPath; }
    const juce::String& getName()    const { return displayName; }

    void render (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                 int startSample, int numSamples) override
    {
        if (regions.empty())
            return;

        auto midiIt = midi.cbegin();
        const auto midiEnd = midi.cend();

        for (int i = 0; i < numSamples; ++i)
        {
            const int framePos = startSample + i;

            while (midiIt != midiEnd && (*midiIt).samplePosition <= framePos)
            {
                const auto msg = (*midiIt).getMessage();
                if (msg.isNoteOn())       startNote (msg.getNoteNumber(), msg.getFloatVelocity());
                else if (msg.isNoteOff()) releaseNote (msg.getNoteNumber());
                ++midiIt;
            }

            float outL = 0.0f, outR = 0.0f;
            for (auto& v : voices)
            {
                if (! v.active) continue;
                const auto* reg = v.region;
                const auto& s = *reg->sample;
                const int   n = s.getNumSamples();

                const float env = v.env.getNextSample();
                float l, r;
                readInterp (s, v.pos, l, r);
                const float g = env * v.gain;
                outL += l * g * v.panL;
                outR += r * g * v.panR;

                v.pos += v.rate;

                // Loop or terminate.
                const double endPos = reg->loopEnd > 0 ? (double) reg->loopEnd : (double) (n - 1);
                if (reg->loop && v.pos >= endPos)
                    v.pos -= (endPos - (double) reg->loopStart);
                if (v.pos >= (double) (n - 1) || ! v.env.isActive())
                    v.active = false;
            }

            const int chans = buffer.getNumChannels();
            if (chans >= 2)
            {
                buffer.addSample (0, framePos, outL);
                buffer.addSample (1, framePos, outR);
            }
            else
            {
                buffer.addSample (0, framePos, 0.5f * (outL + outR));
            }
        }
    }

    void allNotesOff() override
    {
        for (auto& v : voices) v.active = false;
    }

    juce::String typeName() const override { return "Sfz"; }

private:
    struct Region
    {
        juce::String samplePath;
        std::shared_ptr<juce::AudioBuffer<float>> sample;
        double sourceRate   { 44100.0 };
        int    loKey        { 0 };
        int    hiKey        { 127 };
        int    loVel        { 1 };
        int    hiVel        { 127 };
        int    pitchKeycenter { 60 };
        int    transpose    { 0 };
        double tuneCents    { 0.0 };
        double offset       { 0.0 };
        float  volumeDb     { 0.0f };
        float  pan          { 0.0f };   // -100..100
        float  ampAttack    { 0.0f };
        float  ampDecay     { 0.0f };
        float  ampSustain   { 1.0f };   // 0..1 (SFZ 0..100 / 100)
        float  ampRelease   { 0.02f };
        bool   loop         { false };
        double loopStart    { 0.0 };
        double loopEnd      { 0.0 };    // 0 => whole sample
        bool   oneShot      { false };
    };

    struct Voice
    {
        bool          active { false };
        const Region* region { nullptr };
        double        pos    { 0.0 };
        double        rate   { 1.0 };
        float         gain   { 1.0f };
        float         panL   { 1.0f };
        float         panR   { 1.0f };
        int           note   { -1 };
        juce::ADSR    env;
    };

    void startNote (int note, float velocity)
    {
        const int vel = juce::jlimit (1, 127, (int) std::round (velocity * 127.0f));
        for (auto& reg : regions)
        {
            if (note < reg.loKey || note > reg.hiKey) continue;
            if (vel  < reg.loVel || vel  > reg.hiVel) continue;

            Voice* slot = nullptr;
            for (auto& v : voices) if (! v.active) { slot = &v; break; }
            if (slot == nullptr) slot = &voices[0];   // steal

            const double semis = (note - reg.pitchKeycenter + reg.transpose) + reg.tuneCents / 100.0;
            slot->active = true;
            slot->region = &reg;
            slot->pos    = juce::jlimit (0.0, (double) (reg.sample->getNumSamples() - 1), reg.offset);
            slot->rate   = (reg.sourceRate / deviceRate) * std::pow (2.0, semis / 12.0);
            slot->gain   = juce::Decibels::decibelsToGain (reg.volumeDb) * velocity;
            const float p = juce::jlimit (-1.0f, 1.0f, reg.pan / 100.0f);
            slot->panL   = std::cos ((p + 1.0f) * 0.25f * juce::MathConstants<float>::pi) * 1.41421356f;
            slot->panR   = std::sin ((p + 1.0f) * 0.25f * juce::MathConstants<float>::pi) * 1.41421356f;
            slot->note   = note;

            juce::ADSR::Parameters ep;
            ep.attack  = reg.ampAttack;
            ep.decay   = reg.ampDecay;
            ep.sustain = reg.ampSustain;
            ep.release = juce::jmax (0.005f, reg.ampRelease);
            slot->env.setSampleRate (deviceRate);
            slot->env.setParameters (ep);
            slot->env.noteOn();
        }
    }

    void releaseNote (int note)
    {
        for (auto& v : voices)
            if (v.active && v.note == note && v.region != nullptr && ! v.region->oneShot)
                v.env.noteOff();   // one-shots ring out; sustained notes release
    }

    static void readInterp (const juce::AudioBuffer<float>& s, double pos, float& l, float& r)
    {
        const int   i0   = (int) pos;
        const int   i1   = juce::jmin (i0 + 1, s.getNumSamples() - 1);
        const float frac = (float) (pos - i0);
        const auto* d0 = s.getReadPointer (0);
        l = d0[i0] * (1.0f - frac) + d0[i1] * frac;
        if (s.getNumChannels() > 1)
        {
            const auto* d1 = s.getReadPointer (1);
            r = d1[i0] * (1.0f - frac) + d1[i1] * frac;
        }
        else
        {
            r = l;
        }
    }

    // ── parser ──────────────────────────────────────────────────────────────
    static int parseNote (const juce::String& raw)
    {
        auto t = raw.trim();
        if (t.isEmpty()) return -1;
        if (t.containsOnly ("0123456789")) return t.getIntValue();

        const juce::String lower = t.toLowerCase();
        static const int pc[7] = { 9, 11, 0, 2, 4, 5, 7 };   // a b c d e f g
        const int letter = lower[0] - 'a';
        if (letter < 0 || letter > 6) return t.getIntValue();
        int semis = pc[letter];
        int idx = 1;
        if (idx < lower.length() && (lower[idx] == '#' || lower[idx] == 's')) { ++semis; ++idx; }
        else if (idx < lower.length() && lower[idx] == 'b')                   { --semis; ++idx; }
        const int octave = lower.substring (idx).getIntValue();   // c4 = 60 → (oct+1)*12
        return (octave + 1) * 12 + semis;
    }

    bool parse (const juce::File& sfz, std::vector<Region>& out, juce::String& error)
    {
        juce::String text = sfz.loadFileAsString();
        if (text.isEmpty()) { error = "SFZ file empty or unreadable"; return false; }

        // Strip /* */ block comments then // line comments.
        text = juce::String (text).replace ("\r", " ");
        stripBlockComments (text);

        const juce::File dir = sfz.getParentDirectory();
        std::map<juce::String, juce::String> global, master, group, region;
        std::map<juce::String, juce::String>* cur = &global;
        bool inRegion = false;

        auto flushRegion = [&] ()
        {
            if (! inRegion) return;
            std::map<juce::String, juce::String> m = global;
            for (auto& kv : master) m[kv.first] = kv.second;
            for (auto& kv : group)  m[kv.first] = kv.second;
            for (auto& kv : region) m[kv.first] = kv.second;
            if (m.count ("sample")) out.push_back (makeRegion (m, dir));
            region.clear();
            inRegion = false;
        };

        // Tokenise into <headers> and key=value (value may contain spaces, e.g. paths).
        auto lines = juce::StringArray::fromLines (text);
        for (auto& line : lines)
        {
            juce::String l = line;
            const int c = l.indexOf ("//");
            if (c >= 0) l = l.substring (0, c);
            l = l.trim();
            if (l.isEmpty()) continue;

            // Walk tokens; a token starting '<' is a header, else opcode=value where
            // value extends to the next opcode token.
            auto toks = juce::StringArray::fromTokens (l, " \t", "");
            toks.removeEmptyStrings();
            for (int ti = 0; ti < toks.size(); )
            {
                const juce::String tk = toks[ti];
                if (tk.startsWithChar ('<') && tk.endsWithChar ('>'))
                {
                    const juce::String h = tk.substring (1, tk.length() - 1).toLowerCase();
                    if (h == "region") { flushRegion(); inRegion = true; cur = &region; }
                    else if (h == "group")  { flushRegion(); group.clear();  cur = &group;  }
                    else if (h == "master") { flushRegion(); master.clear(); cur = &master; }
                    else if (h == "global") { flushRegion(); global.clear(); cur = &global; }
                    else if (h == "control" || h == "curve" || h == "effect") { cur = nullptr; }
                    ++ti;
                    continue;
                }

                const int eq = tk.indexOf ("=");
                if (eq <= 0) { ++ti; continue; }
                const juce::String key = tk.substring (0, eq).toLowerCase();
                juce::String val = tk.substring (eq + 1);
                // Accumulate following non-opcode tokens into the value (space paths).
                int tj = ti + 1;
                while (tj < toks.size() && ! toks[tj].startsWithChar ('<') && toks[tj].indexOf ("=") < 0)
                {
                    val += " " + toks[tj];
                    ++tj;
                }
                if (cur != nullptr) (*cur)[key] = val.trim();
                ti = tj;
            }
        }
        flushRegion();
        return true;
    }

    static void stripBlockComments (juce::String& text)
    {
        for (;;)
        {
            const int a = text.indexOf ("/*");
            if (a < 0) break;
            const int b = text.indexOf (a + 2, "*/");
            if (b < 0) { text = text.substring (0, a); break; }
            text = text.substring (0, a) + " " + text.substring (b + 2);
        }
    }

    static Region makeRegion (std::map<juce::String, juce::String>& m, const juce::File& dir)
    {
        Region r;
        auto has = [&] (const char* k) { return m.count (k) > 0; };
        auto sv  = [&] (const char* k) { return m[k]; };
        auto fv  = [&] (const char* k, double def) { return has (k) ? m[k].getDoubleValue() : def; };

        juce::String rel = sv ("sample").replaceCharacter ('\\', '/');
        r.samplePath = dir.getChildFile (rel).getFullPathName();

        if (has ("key"))
        {
            const int k = parseNote (sv ("key"));
            r.loKey = r.hiKey = r.pitchKeycenter = k;
        }
        if (has ("lokey")) r.loKey = parseNote (sv ("lokey"));
        if (has ("hikey")) r.hiKey = parseNote (sv ("hikey"));
        if (has ("pitch_keycenter")) r.pitchKeycenter = parseNote (sv ("pitch_keycenter"));
        if (has ("lovel")) r.loVel = (int) fv ("lovel", 1);
        if (has ("hivel")) r.hiVel = (int) fv ("hivel", 127);
        r.transpose = (int) fv ("transpose", 0);
        r.tuneCents = fv ("tune", 0.0);
        r.offset    = fv ("offset", 0.0);
        r.volumeDb  = (float) fv ("volume", 0.0);
        r.pan       = (float) fv ("pan", 0.0);
        r.ampAttack  = (float) fv ("ampeg_attack", 0.0);
        r.ampDecay   = (float) fv ("ampeg_decay", 0.0);
        r.ampSustain = (float) (fv ("ampeg_sustain", 100.0) / 100.0);
        r.ampRelease = (float) fv ("ampeg_release", 0.02);
        r.loopStart  = fv ("loop_start", 0.0);
        r.loopEnd    = fv ("loop_end", 0.0);

        const juce::String lm = sv ("loop_mode").toLowerCase();
        r.oneShot = (lm == "one_shot");
        r.loop    = (lm == "loop_continuous" || lm == "loop_sustain");
        return r;
    }

    std::vector<Region> regions;
    juce::String sfzPath, displayName;
    double deviceRate { 44100.0 };

    static constexpr int kNumVoices = 48;
    std::array<Voice, kNumVoices> voices;
};
