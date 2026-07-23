# Gloopy Control API

A hybrid remote-control surface so external programs (e.g. Common Lisp) can
drive Gloopy: create tracks, hand over MIDI sequences, control effects, and play
instruments live.

Two protocols, each matched to what it does best:

| | **gRPC** (Phase 2) | **OSC** (Phase 1) |
|---|---|---|
| Role | *change the world* — typed, reliable commands & queries | *perform now* — low-latency, fire-and-forget events |
| Transport | TCP / HTTP-2 | UDP |
| Examples | create track, load project, **schedule** a note sequence into a clip, add effect, query state, stream events out | **live** note on/off, CC, real-time knob turns |
| Mental model | *"here is a 16-note sequence at these beats"* | *"play C4 velocity 100 **right now**"* |

The dividing line is **scheduled vs. live**. gRPC edits the model (plays in
time); OSC injects ephemeral real-time events (plays this audio block).

## Threading — the three lanes

Every request lands in exactly one lane so the audio thread is never blocked:

```
external client ──► API layer ──► one of:
  structural  ──► MESSAGE THREAD  (callAsync → model edit under engineLock)
  atomic      ──► ANY THREAD      (atomic store on an Effect/track param)
  live MIDI   ──► AUDIO THREAD     (juce::MidiMessageCollector per track,
                                    drained each block, merged with clip MIDI)
```

- **Structural** (gRPC): add/remove track, add clip, set notes, routing,
  load/save → marshalled to the message thread, executed like a UI action.
- **Atomic** (OSC or gRPC): built-in effect params and track vol/pan/mute are
  already `std::atomic`, so handlers set them directly, lock-free.
- **Live MIDI** (OSC): each instrument track owns a `juce::MidiMessageCollector`.
  The OSC handler calls `addMessageToQueue()`; the audio callback calls
  `removeNextBlockOfMessages()` and merges it with the clip-scheduled MIDI —
  JUCE's purpose-built lock-free bridge for live notes.

## Shared identity

gRPC **creates and names** things and returns stable **IDs**; OSC **performs on
those IDs**. Tracks/effects therefore carry a stable `id` (not an array index,
which shifts). gRPC allocates, OSC drives.

## OSC address space (Phase 1)

Default port **9000**, localhost. Arguments: `i` = int32, `f` = float32.

```
/gloopy/track/<id>/note      <pitch:i> <vel:i>     vel>0 = note on, vel=0 = note off
/gloopy/track/<id>/cc        <cc:i>    <val:f>     0..1 → 0..127
/gloopy/track/<id>/vol       <v:f>                 0..1
/gloopy/track/<id>/pan       <p:f>                 -1..1
/gloopy/track/<id>/mute      <m:i>                 0/1

/gloopy/insert/<n>/fx/<m>/param/<name>  <v:f>      real-time effect knob
                                                   (n = mixer insert, m = effect slot)
/gloopy/transport/play                             — start
/gloopy/transport/stop                             — stop + rewind
/gloopy/transport/tempo      <bpm:f>
/gloopy/transport/seek       <beats:f>
```

Track ids are logged to stdout on load / track-add until gRPC's `GetState`
provides proper discovery.

## gRPC service (Phase 2 — implemented)

The authoritative definition is [`proto/gloopy.proto`](../proto/gloopy.proto);
`gloopy.v1.Gloopy` on `127.0.0.1:50051`. Every RPC below is implemented and
verified end-to-end. Structural RPCs hop to the message thread; effect-param
and transport edits are lock-free; meter snapshots use a try-lock so a streaming
subscriber never stalls the audio thread.

```proto
service Gloopy {
  // transport
  rpc Play(Empty) returns (Ack);   rpc Stop(Empty) returns (Ack);
  rpc SetTempo(Tempo) returns (Ack);   rpc Seek(Position) returns (Ack);
  rpc GetTransport(Empty) returns (TransportState);

  // tracks
  rpc AddSynthTrack(AddSynthTrackRequest) returns (TrackId);
  rpc SetTrackParams(TrackParams) returns (Ack);
  rpc ListTracks(Empty) returns (TrackList);
  rpc AddClip(AddClipRequest) returns (ClipId);        // schedule a MIDI sequence

  // track & clip management
  rpc RemoveTrack(TrackId) returns (Ack);
  rpc AddAudioTrack(AddAudioTrackRequest) returns (TrackId);
  rpc AddSamplerTrack(AddSamplerTrackRequest) returns (TrackId);   // load a .wav
  rpc AddPluginTrack(AddPluginTrackRequest) returns (TrackId);     // hosted instrument
  rpc RemoveClip(ClipRef) returns (Ack);
  rpc MoveClip(MoveClipRequest) returns (Ack);         // reposition / move to another track
  rpc AddAudioClip(AddAudioClipRequest) returns (ClipId);          // import a .wav clip

  // mixer & effects
  rpc ListInserts(Empty) returns (InsertList);
  rpc AddEffect(AddEffectRequest) returns (EffectRef);            // built-in effect
  rpc AddPluginEffect(AddPluginEffectRequest) returns (EffectRef); // hosted effect
  rpc RemoveEffect(EffectRef) returns (Ack);
  rpc SetEffectParam(EffectParamSet) returns (Ack);
  rpc SetEffectBypass(EffectBypassSet) returns (Ack);
  rpc GetEffectParams(EffectRef) returns (ParamList);

  // plugins
  rpc ScanPlugins(ScanPluginsRequest) returns (PluginList);
  rpc ListPlugins(Empty) returns (PluginList);          // name, format, identifier
  rpc OpenPluginEditor(TrackId) returns (Ack);

  // project & state
  rpc GetState(Empty) returns (ProjectState);
  rpc NewProject(Empty) returns (Ack);
  rpc LoadProject(FilePath) returns (Ack);
  rpc SaveProject(FilePath) returns (Ack);
  rpc RenderToFile(RenderRequest) returns (Ack);       // offline bounce to WAV

  // events (playhead, meters) — closed-loop control
  rpc Subscribe(SubscribeRequest) returns (stream Event);
}
```

Hosted plugins are addressed by the stable `identifier` string returned by
`ListPlugins` / `ScanPlugins` (JUCE's `PluginDescription::createIdentifierString`);
pass it to `AddPluginTrack` / `AddPluginEffect`.

`RenderToFile` bounces the whole song (plus a reverb/delay tail) to a 24-bit WAV
**offline** — it holds the engine lock and pumps blocks faster than real time
(~20× on the demo song), so the call blocks until the file is written but does
not disturb the live transport or playback state.

## Security

- **OSC** is unauthenticated/unencrypted (it's just UDP) — it is a **local
  performance lane, bound to the same machine**. Do not expose it to an
  untrusted network. (Phase 1 listens on the port on all interfaces; localhost
  binding / firewalling is the mitigation and a hardening item.)
- **gRPC** binds localhost by default and can add TLS + auth if you ever want
  to drive Gloopy from another machine.

## Phases

1. **OSC real-time lane** *(done)* — `juce_osc` receiver, stable track ids,
   `MidiMessageCollector` per instrument track, live note/CC/param/transport
   handlers, a Common Lisp example.
2. **gRPC command surface** *(done)* — build integration (system gRPC), transport
   / track / clip / effect / project services, state queries, event streaming.
3. **Plugins & structural editing over the API** *(done)* — scan/list plugins,
   create sampler/audio/plugin tracks, import audio clips, add plugin effects,
   remove/move tracks & clips, open plugin editors.
4. Full UI parity and a documented Common Lisp client library.

## Build / deps

- OSC: `juce_osc` (native JUCE module — no external dependency).
- gRPC: system libs (`dnf install grpc-devel grpc-plugins protobuf-compiler`).
