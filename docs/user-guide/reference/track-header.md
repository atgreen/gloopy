# Track header controls

The left column of each track in the **arrangement** view.

## On the header

| Control | Shown on | What it does |
|---------|----------|--------------|
| **Coloured spine** | all | The track's colour (set via the right-click menu) |
| **Name + type** | all | Track name over its type — INSTRUMENT / AUDIO / MIDI OUT. Hosted instruments show the patch name |
| **S** | all | Solo — hear this track alone |
| **M** | all | Mute |
| **Volume** | all | The track fader (drag; no typed value) |
| **UI** | plugin instruments | Open the hosted plugin's editor window |
| **●** | audio tracks | **Arm** for audio recording (red when armed) |
| **ARP** | instrument tracks | Open the [arpeggiator menu](#the-arp-menu); lit when the arp is on |
| **MIDI LED** | the receiving track | Pulses green while live notes arrive |

## Right-click menu

Right-click a track header for:

| Item | What it does |
|------|--------------|
| **Rename track…** | Change the name |
| **Colour ▸** | Red / Orange / Yellow / Green / Teal / Blue / Purple / Grey |
| **Duplicate track** | Copy the track and its clips |
| **Move up** / **Move down** | Reorder in the track list |
| **Invert phase** | Flip the track's polarity (checkable) |
| **Export track (WAV)…** | Bounce just this track ([stems recipe](../how-to/export-audio.md#one-track-a-stem)) |
| **Sampler playback window…** | Sampler tracks only — start/end, fades, loop crossfade, root note, direction, voicing, interpolation |
| **Delete track** | Remove the track |

## The ARP menu

Instrument tracks carry a live [arpeggiator](../../control-scripting/concepts/model.md#arpeggiator).
The **ARP** button opens:

| Setting | Choices |
|---------|---------|
| **Arpeggiator** | On / off |
| **Hold** | Keep the pattern running without holding keys |
| **Rate** | 1/4, 1/8, 1/16, 1/32 |
| **Octaves** | 1–4 |
| **Mode** | Up / Down / Up-Down / Random |
| **Swing** | Off / Subtle / Medium / Strong |
| **Chance** | 100 / 75 / 50 / 25 % (a generative gate) |
| **Gate** | 25 % (staccato) / 50 / 75 / 100 % (legato) |
