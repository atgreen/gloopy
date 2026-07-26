<!-- SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com> -->
<!-- SPDX-License-Identifier: AGPL-3.0-only -->

# Session view — design notes

Ableton-style clip-launch grid alongside the linear Arrangement view (Tab cycles
Arrange → Session → Mixer). Columns are tracks, rows are global **scenes**, each
cell a launchable looping clip. Playback model: **per-track override** — a track
with a playing session clip plays it (looped) *instead of* its arrangement clips;
other tracks keep playing the arrangement; "Back to Arrangement" resumes.

Roadmap + locked scope live in `ideas.md`. This doc records the **render-wiring**
design, mined from two working references we have locally: **Ardour** (its
"Cue"/Trigger system, `libs/ardour/triggerbox.cc`) and **tracktion_engine** (the
JUCE-based Waveform launcher: `LaunchHandle`, `LauncherClipPlaybackHandle`,
`ArrangerLauncherSwitchingNode`). Both agreed on the load-bearing ideas below.

## Components (already built)

- `SessionModel.h` — `Scene` + `Track::sessionSlots` (rectangular grid).
- `SessionLauncher.h` — pure launch FSM: per-track `{playing, pending, launchBeat}`,
  `advance(beatStart,beatEnd)` applies pending at the next quantum boundary,
  `changedTracks()` reports transitions. Maps to Ardour's Trigger state machine /
  Tracktion's `LaunchHandle`.

## Render-wiring design (the next slice)

### 1. Sample-accurate launch via a block split (NOT block-granular)
Applying a launch at "the block that contains the boundary" smears clip starts to
buffer edges (~10 ms jitter). Both references transition *within* the block:
- Ardour: writes the clip at a `quantize_offset` into the buffer (`triggerbox.cc`
  `maybe_compute_next_transition` → `accumulate_from(..., dest_offset)`).
- Tracktion: `LaunchHandle::advance` returns a `SplitStatus` (range1 = old state up
  to the boundary, range2 = launched clip after); the consumer renders each sub-
  range into its own frame slice (`llround(proportion * numSamples)`).

**Gloopy:** `SessionLauncher::advance` already stores the boundary in `launchBeat`
for a launched track. In `renderBlock`'s per-track loop, for a track in
`changedTracks()` compute `splitSample = round((transitionBeat - blockStartBeat) * spb)`
and render the outgoing content for `[0, splitSample)` and the incoming for
`[splitSample, num)`. Refinement needed: `SessionLauncher` must also record the
**stop** transition beat (today only launches set `launchBeat`) — store a per-track
`transitionBeat` used for both launch and stop splits.

### 2. Loop phase = a monotonic accumulator, never `currentBeat − launchBeat`
The arrangement playhead can loop/jump, so subtracting `launchBeat` from the live
playhead is unsafe, and float phase-math drifts (Ardour's explicit warning).
- Ardour keeps an integer `read_index` reset at (re)trigger, advanced by frames.
- Tracktion keeps a monotonically increasing `elapsed` (`duration += blockDuration`)
  and maps position with `fmod(elapsed + offset, loopLen)`; only the *mapping* wraps.

**Gloopy:** maintain a **monotonic session beat clock** (advance by `blockBeats`
each block while anything plays), separate from the looping playhead. Feed *that*
range to `advance()` (so quantization is loop-safe), and derive a playing clip's
window as `fmod(monotonicBeat − launchBeat, clip.contentLenBeats)`. For **MIDI**
session clips (v1), feed that phased window to the existing `collectClip`, whose
loop logic already handles the seam — so we don't need to hand-split the loop
point for MIDI. (Audio session clips would want an integer sample read-index; defer
with audio-slot support.)

### 3. Per-track override = one flag + choose which fills the buffer
Both keep the arrangement path intact and just *pick* which output is heard:
- Ardour: `triggerbox_overrides_disk_monitoring` → disk reader emits
  `MonitoringSilence`, triggerbox fills the buffer.
- Tracktion: `AudioTrack::playSlotClips` bool; `ArrangerLauncherSwitchingNode`
  renders the arranger only when `! playSlotClips`, with a ~10-sample crossfade at
  the switch sample to avoid a click.

**Gloopy:** in the per-track render, if `launcher.playingSlot(t) >= 0` collect the
session clip; else collect the arrangement clips (today's path). At a transition,
emit an `allNotesOff` for the outgoing content at `splitSample` (coarse but matches
Gloopy's existing loop-wrap note-off; upgrade to a tracked note-off list later, cf.
Ardour's `MidiStateTracker::resolve_notes`). A short fade for audio can come with
audio slots.

### 4. Threading
Both references are lock-free at the RT boundary. Gloopy already guards engine
state with `engineLock` (message thread holds it to mutate; audio thread
try-locks and skips), which is exactly the model the existing clip/arp mutations
use — so `SessionLauncher` requests (message thread, under the lock) and
`advance()` (audio thread, under the try-lock) need **no** extra atomics/seqlocks.
Just never allocate in `advance()` (it doesn't).

### 5. Follow-actions (deferred, but shape it right)
Decouple *when* from *what*: a `stopDuration` in beats (`loopLen * numLoops − offset`)
checked per block against elapsed-monotonic, plus a `std::function<void(beat)>`
callback that launches the successor at the *same* monotonic beat (so it's sample-
aligned). Ship `none / stop / playAgain / next / returnToArrangement`; the full
group-relative/round-robin/weighted matrix (Tracktion `FollowAction`) grows into
the same callback shape later.

## Slice order from here
1. **Render wiring** — items 1–4 above: monotonic clock, per-track override in
   `renderBlock`, split-at-boundary launch/stop, transition note-off; plus a launch
   API (`MainComponent::scenes` + launch/stop/scene methods) so it's observable.
2. **Persistence** — grid + scenes in `.gloopy` and composition-repo.
3. **Session UI + Tab** — the simplified color-coded grid + mini-mixer strips.
4. **Recording**, **control API**, **follow-actions/polish**.
