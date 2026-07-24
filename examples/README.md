# Example projects

## demo-song.gloopy

A complete ~60-bar demo track (124 BPM, A minor) that exercises the whole app:

- **8 tracks** — Kick, Snare, Hat, Clap (samplers), Bass, Pad, Lead (synths),
  and an FX track (audio: crashes + synthesized risers).
- **Song structure** — Intro → Verse → Chorus → Verse → Chorus → **Breakdown**
  → **Final Chorus** → Outro, with drum fills and a filter-swept riser into
  every drop, octave-doubled leads in the choruses, and panned percussion.
- **Mixer effects** — reverb on the pad and crash, stereo ping-pong delay on
  the lead, a low-pass filter on the bass, a high-pass on the hats, and a
  master gain trim.
- **Background flourishes** — echoing *pings*, soft *bell dings*, synthesized
  *woowoo* swoops, and a *sparkle* arp in the final chorus, each on its own
  mixer insert with delay/reverb.

Open it via **File → Open**, or launch straight into it:

```sh
./build/Gloopy_artefacts/Release/Gloopy examples/demo-song.gloopy
```

## demo-song-surge.gloopy

The same arrangement, but the **Pad track is a hosted Surge XT (VST3)** instead
of the built-in synth — a warm, unison-detuned, low-passed pad patch baked
directly into the project's plugin state. Requires **Surge XT** installed (its
VST3 in `~/.vst3`); without it the Pad track simply loads silent.

### Sample credits
The drum/percussion one-shots are from the **Sonic Pi** sample set, which sources
them from **freesound.org** under **CC0 (public domain)**. They are embedded in
the project file, so it is fully self-contained.

## Genre demos

Five short songs across styles, each a **live** Gloopy project (MIDI clips driving
instruments — no baked audio), assembled through the gRPC control API:

| Project | Style | Instruments |
|---|---|---|
| `demo-synthwave.gloopy` | Synthwave (Am, 100 bpm) | built-in kit + saw bass, pad, arp, lead |
| `demo-lofi.gloopy` | Lo-fi / chill (Cmaj7, 76 bpm) | built-in kit + jazzy keys, bass, Rhodes |
| `demo-ambient.gloopy` | Ambient (60 bpm) | built-in synth: evolving pads, sub, bell |
| `demo-cinematic.gloopy` | Cinematic (Dm, 68 bpm) | **VPO** strings (sfizz) + violin melody |
| `demo-nocturne.gloopy` | Moody art-rock (Am, 76 bpm) | **VPO** strings + **Salamander** grand piano (sfizz), bass, lead |

The first three are **portable** (built-in synth/kit only). `demo-cinematic` and
`demo-nocturne` use SFZ instruments and reference sample libraries by absolute
path (like the Surge demo needs Surge), so they need these installed locally to
make sound:

- **Virtual Playing Orchestra** at `~/sfz/Virtual-Playing-Orchestra3/` (strings).
- **Salamander Grand Piano** at `~/sfz/SalamanderGrandPianoV3_OggVorbis/` (nocturne piano).

SFZ is now played by the **vendored, embedded sfizz** engine (`third_party/sfizz`,
via `Source/SfizzGenerator.h`) — no plugin install and no state hack. Add an SFZ
instrument with the **`+ SFZ`** button or `AddSfzTrack`. (These two demo files were
authored before the switch and still carry hosted-sfizz *plugin* state, so today
they also need the sfizz VST3/LV2 installed; re-saving them as native SFZ tracks is
a pending follow-up.) See `AGENTS.md` for details.
