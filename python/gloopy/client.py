# SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
# SPDX-License-Identifier: AGPL-3.0-only
"""A thin, Pythonic client for Gloopy's gRPC control API.

Mirrors ``examples/gloopy-grpc.lisp``. The DAW listens on 127.0.0.1:50051;
structural edits and queries go over gRPC (live notes/knobs are on OSC 9000).

    from gloopy import Gloopy, note, seq, mini, scale, chord

    g = Gloopy()                      # connects to 127.0.0.1:50051
    g.set_tempo(128)
    tid = g.add_synth_track("lead", wave="SAW")

    # Notes are (pitch, start_beat, length_beats). pitch accepts a MIDI int or a
    # name; length accepts beats or shorthand ("q", "8t"). See gloopy.music for
    # seq() (lay steps end-to-end, with rests), the mini() string DSL, scale()
    # and chord().
    g.add_clip(tid, notes=[note(60, 0, 1), note("E4", 1, 1), note("G4", 2, 2)])
    g.add_clip(tid, notes=seq([("C4","q"), ("E4","e"), (None,"e"), ("G4","h")]))
    g.add_clip(tid, notes=mini("c4q d e f  g4h a b"))   # compact melody DSL
    g.play()
    ...
    g.render("/tmp/out.wav", tail_seconds=1.0)
"""
from __future__ import annotations

from contextlib import contextmanager
from typing import Iterable, Iterator, Optional

import grpc

from . import gloopy_pb2 as pb
from . import gloopy_pb2_grpc as rpc

WAVEFORMS = {"SINE": 0, "SAW": 1, "SQUARE": 2, "TRIANGLE": 3}
EFFECTS = {"GAIN": 0, "FILTER": 1, "DELAY": 2, "REVERB": 3, "LIMITER": 4,
           "BITCRUSHER": 5, "COMPRESSOR": 6, "EQ": 7, "WAVESHAPER": 8,
           "STEREO_WIDENER": 9, "TREMOLO": 10, "CHORUS": 11, "FLANGER": 12, "PHASER": 13,
           "AUTOPAN": 14, "NOISE_GATE": 15, "AUTOWAH": 16, "RINGMOD": 17, "SCOPE": 18, "SPECTRUM": 19, "VECTORSCOPE": 20}
AUTO_TARGETS = {"TRACK_VOL": 0, "TRACK_PAN": 1, "INSERT_VOL": 2,
                "INSERT_PAN": 3, "EFFECT_PARAM": 4}


def note(pitch, start_beat: float, length_beats,
         velocity: float = 0.8) -> pb.Note:
    """Build a Note for AddClip. Times are in beats, relative to the clip.

    ``pitch`` accepts a MIDI int or a name (``"C#4"``); ``length_beats`` accepts
    a number of beats or duration shorthand (``"q"``, ``"8t"``) — see
    ``gloopy.music``."""
    from .music import pitch as _pitch, dur as _dur
    return pb.Note(pitch=_pitch(pitch), start_beat=start_beat,
                   length_beats=_dur(length_beats), velocity=velocity)


def _infer_lang(script: str, generator: str) -> str:
    """Guess the kernel language for regenerate_clip when the caller omits `lang`, so Python
    users aren't forced to spell it out (defaulting everything to Lisp is hostile to them).
    Extension wins; else a dotted module in the generator ("pkg.mod:fn") reads as Python."""
    ref = (script or "").lower()
    if ref.endswith(".py"):
        return "python"
    if ref.endswith((".lisp", ".cl", ".lsp")):
        return "common-lisp"
    if generator:
        module = generator.split(":", 1)[0]
        return "python" if "." in module else "common-lisp"
    return "common-lisp"


def _wave(w) -> int:
    return WAVEFORMS.get(w.upper(), 0) if isinstance(w, str) else int(w)


def _fx(t) -> int:
    return EFFECTS.get(t.upper(), 0) if isinstance(t, str) else int(t)


def _discover_target(default: str = "127.0.0.1:50051") -> str:
    """Gloopy's gRPC address, read from its discovery file (the ``control_port`` it
    writes on startup) so a client still finds it when 50051 was busy and Gloopy fell
    back to a free port. Falls back to ``default`` when there's no discovery file."""
    import os
    import json
    base = os.environ.get("XDG_RUNTIME_DIR") or os.path.join(os.path.expanduser("~"), ".cache")
    path = os.path.join(base, "gloopy", "kernel.json")
    try:
        with open(path) as fh:
            port = json.load(fh).get("control_port")
        if isinstance(port, int) and port > 0:
            return f"127.0.0.1:{port}"
    except (OSError, ValueError, TypeError):
        pass
    return default


class Gloopy:
    """Connection to a running Gloopy instance.

    Usable as a context manager (``with Gloopy() as g: ...``) which closes the
    channel on exit. Methods return plain Python values (ints, dicts, lists).
    """

    def __init__(self, target: Optional[str] = None):
        # target=None auto-discovers the port from Gloopy's discovery file (so a moved
        # control port just works); pass an explicit "host:port" to override.
        self.channel = grpc.insecure_channel(target or _discover_target())
        self.stub = rpc.GloopyStub(self.channel)

    # -- lifecycle --------------------------------------------------------
    def close(self) -> None:
        self.channel.close()

    def __enter__(self) -> "Gloopy":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    # -- transport --------------------------------------------------------
    def play(self) -> None:
        self._ack(self.stub.Play(pb.Empty()))

    def stop(self) -> None:
        self._ack(self.stub.Stop(pb.Empty()))

    def panic(self) -> None:
        """All-notes-off across every track — clears stuck/hanging notes."""
        self._ack(self.stub.Panic(pb.Empty()))

    def set_tempo(self, bpm: float) -> None:
        self._ack(self.stub.SetTempo(pb.Tempo(bpm=bpm)))

    def set_swing(self, amount: float) -> None:
        """0.5 = straight, up to ~0.75 = triplet feel."""
        self._ack(self.stub.SetSwing(pb.Swing(amount=amount)))

    def seek(self, beats: float) -> None:
        self._ack(self.stub.Seek(pb.Position(beats=beats)))

    def transport(self) -> dict:
        t = self.stub.GetTransport(pb.Empty())
        return {"playing": t.playing, "bpm": t.bpm, "position_beats": t.position_beats,
                "loop_enabled": t.loop_enabled, "loop_start": t.loop_start, "loop_end": t.loop_end,
                "metronome": t.metronome, "swing": t.swing}

    def set_metronome(self, enabled: bool = True) -> None:
        """Toggle the beat-click metronome (a monitor layer; included in a bounce if left on)."""
        self._ack(self.stub.SetMetronome(pb.MetronomeRequest(enabled=enabled)))

    def set_metronome_level(self, level: float = 1.0) -> None:
        """Set the metronome click volume (0..1). A session setting (not saved)."""
        self._ack(self.stub.SetMetronomeLevel(pb.MetronomeLevel(level=level)))

    def get_metronome_level(self) -> float:
        return self.stub.GetMetronomeLevel(pb.Empty()).level

    def start_recording(self) -> None:
        """Record armed MIDI + armed audio tracks from the playhead."""
        self._ack(self.stub.StartRecording(pb.Empty()))

    def stop_recording(self) -> None:
        self._ack(self.stub.StopRecording(pb.Empty()))

    def list_audio_inputs(self) -> list[str]:
        return list(self.stub.ListAudioInputs(pb.Empty()).names)

    def list_midi_inputs(self) -> list[str]:
        """MIDI input sources Gloopy is listening to (it auto-opens all + hot-plugs)."""
        return list(self.stub.ListMidiInputs(pb.Empty()).names)

    def arm_track(self, track_id: int, armed: bool = True, input: int = 0,
                  channels: int = 2, monitor: bool = False) -> None:
        """Arm an audio track for recording (input = first hardware channel)."""
        self._ack(self.stub.ArmTrack(pb.ArmRequest(
            track_id=track_id, armed=armed, input=input, channels=channels, monitor=monitor)))

    def set_punch_range(self, enabled: bool = True, in_beat: float = 0.0,
                        out_beat: float = 0.0, count_in_beats: float = 0.0) -> None:
        """Punch in/out (record only within [in,out)) with an optional count-in lead."""
        self._ack(self.stub.SetPunchRange(pb.PunchRange(
            enabled=enabled, in_beat=in_beat, out_beat=out_beat, count_in_beats=count_in_beats)))

    def set_loop(self, enabled: bool = True, start_beat: float = 0.0, end_beat: float = 4.0) -> None:
        self._ack(self.stub.SetLoop(pb.Loop(enabled=enabled, start_beat=start_beat, end_beat=end_beat)))

    def set_record_settings(self, format: int = 0, latency_offset_seconds: float = 0.0) -> None:
        """Take format (0=WAV, 1=FLAC) and manual latency offset (added to device latency)."""
        self._ack(self.stub.SetRecordSettings(pb.RecordSettings(
            format=format, latency_offset_seconds=latency_offset_seconds)))

    def promote_take(self, take_id: str) -> None:
        """Move a scratch take from raw/ into the recordings dir and repoint its clips."""
        self._ack(self.stub.PromoteTake(pb.TakeRef(take_id=take_id)))

    def cleanup_takes(self) -> int:
        """Delete take files no clip references; returns the count removed."""
        return self.stub.CleanupTakes(pb.Empty()).count

    def recover_takes(self) -> int:
        """Create clips for orphan take files (crash recovery); returns the count."""
        return self.stub.RecoverTakes(pb.Empty()).count

    # -- tracks -----------------------------------------------------------
    def add_synth_track(self, name: str = "", wave="SINE", attack=0.01,
                        decay=0.1, sustain=0.8, release=0.2, gain=0.8) -> int:
        r = self.stub.AddSynthTrack(pb.AddSynthTrackRequest(
            name=name, wave=_wave(wave), attack=attack, decay=decay,
            sustain=sustain, release=release, gain=gain))
        return r.id

    def add_sampler_track(self, name: str, path: str, root_note: int = 60) -> int:
        return self.stub.AddSamplerTrack(pb.AddSamplerTrackRequest(
            name=name, path=path, root_note=root_note)).id

    def set_sampler_controls(self, track_id: int, start: float = 0.0, end: float = 1.0,
                             reverse: bool = False, root_note: int = 0,
                             fade_in: float = 0.0, fade_out: float = 0.0, loop: bool = False,
                             mono: bool = False, loop_xfade: float = 0.0, interp: int = 0) -> None:
        """Sampler playback controls: window [start,end] as fractions of the sample
        length, reverse plays the window back-to-front, root_note>0 sets the root
        (0 leaves it unchanged), fade_in/fade_out are per-voice fades in seconds
        (0 = off; de-click a mid-waveform trim). loop repeats the window until note-off
        (fade_out then acts as the release time). mono chokes: a new hit cuts every
        ringing voice (hi-hat choke / mono 808). loop_xfade (seconds) crossfades the loop
        seam so it has no discontinuity click. interp selects the resampling interpolation
        (0 = linear, 1 = cubic — smoother when pitch-shifted). Fails if not a Sampler."""
        self._ack(self.stub.SetSamplerControls(pb.SamplerControlsRequest(
            track_id=track_id, start=start, end=end, reverse=reverse, root_note=root_note,
            fade_in=fade_in, fade_out=fade_out, loop=loop, mono=mono, loop_xfade=loop_xfade,
            interp=interp)))

    def get_sampler_controls(self, track_id: int) -> dict:
        r = self.stub.GetSamplerControls(pb.TrackId(id=track_id))
        return {"ok": r.ok, "start": r.start, "end": r.end, "reverse": r.reverse,
                "root_note": r.root_note, "name": r.name,
                "fade_in": r.fade_in, "fade_out": r.fade_out, "loop": r.loop, "mono": r.mono,
                "loop_xfade": r.loop_xfade, "interp": r.interp}

    def add_sfz_track(self, path: str, name: str = "") -> int:
        """Load a native SFZ instrument (samples preloaded) onto a new track."""
        return self.stub.AddSfzTrack(pb.AddSfzTrackRequest(name=name, path=path)).id

    def add_surge_track(self, patch: str = "", name: str = "") -> int:
        """Add a new track backed by the embedded Surge XT engine (optional .fxp patch)."""
        return self.stub.AddSurgeTrack(pb.AddSurgeTrackRequest(name=name, patch=patch)).id

    def add_audio_track(self, name: str = "") -> int:
        return self.stub.AddAudioTrack(pb.AddAudioTrackRequest(name=name)).id

    def add_plugin_track(self, identifier: str) -> int:
        return self.stub.AddPluginTrack(pb.AddPluginTrackRequest(identifier=identifier)).id

    def set_track_params(self, track_id: int, *, volume=None, pan=None,
                        mute=None, solo=None, name=None) -> None:
        kw = {"id": track_id}
        for k, v in (("volume", volume), ("pan", pan), ("mute", mute),
                     ("solo", solo), ("name", name)):
            if v is not None:
                kw[k] = v
        self._ack(self.stub.SetTrackParams(pb.TrackParams(**kw)))

    def set_synth_param(self, track_id: int, name: str, value: float) -> None:
        """Tweak the built-in synth engine on a track. ``name`` is one of:
        wave osc2wave osc2detune oscmix sub · attack decay sustain release gain ·
        ftype cutoff reso fenvamt fattack fdecay fsustain frelease ·
        lfotarget lforate lfodepth."""
        self._ack(self.stub.SetSynthParam(
            pb.SynthParamSet(track_id=track_id, name=name, value=value)))

    # --- Macros (the rack layer) ------------------------------------------
    def add_macro(self, track_id: int, name: str = "Macro") -> int:
        """Add a perceptual macro (an encoder, 0..1) to a track. Returns its index."""
        return self.stub.AddMacro(pb.MacroAdd(track_id=track_id, name=name)).index

    def set_macro_value(self, track_id: int, macro: int, value: float) -> None:
        """Turn a macro (0..1); every mapped param sweeps across its authored [lo,hi]."""
        self._ack(self.stub.SetMacroValue(
            pb.MacroValue(track_id=track_id, macro=macro, value=value)))

    def map_macro_synth(self, track_id: int, macro: int, param: str,
                        lo: float = 0.0, hi: float = 1.0) -> None:
        """Map a macro onto a built-in synth param (see set_synth_param names), swept lo..hi."""
        self._ack(self.stub.MapMacroSynth(
            pb.MacroMapSynth(track_id=track_id, macro=macro, param=param, lo=lo, hi=hi)))

    def map_macro_effect(self, track_id: int, macro: int, insert: int, slot: int,
                         param: str, lo: float = 0.0, hi: float = 1.0) -> None:
        """Map a macro onto a mixer insert-effect param (insert/slot), swept lo..hi."""
        self._ack(self.stub.MapMacroEffect(
            pb.MacroMapEffect(track_id=track_id, macro=macro, insert=insert, slot=slot,
                              param=param, lo=lo, hi=hi)))

    def randomize_macros(self, track_id: int) -> None:
        """Roll every macro on the track to a fresh random 0..1 and apply — musical because
        each mapping stays within its authored [lo,hi] safe range."""
        self._ack(self.stub.RandomizeMacros(pb.TrackId(id=track_id)))

    def remove_track(self, track_id: int) -> None:
        self._ack(self.stub.RemoveTrack(pb.TrackId(id=track_id)))

    def rename_track(self, track_id: int, name: str) -> None:
        """Rename an existing track. Empty/whitespace names are rejected."""
        self._ack(self.stub.RenameTrack(pb.RenameTrackRequest(track_id=track_id, name=name)))

    def duplicate_track(self, track_id: int) -> int:
        """Clone a track — its clips, generator/instrument and insert state — as a new
        '<name> copy' track with its own mixer strip. Returns the new track id (-1 if unknown)."""
        return self.stub.DuplicateTrack(pb.TrackId(id=track_id)).id

    def list_tracks(self) -> list[dict]:
        r = self.stub.ListTracks(pb.Empty())
        return [{"id": t.id, "name": t.name, "type": t.type, "volume": t.volume,
                 "pan": t.pan, "mute": t.mute, "clips": t.clips, "colour": t.colour,
                 "polarity": t.polarity} for t in r.tracks]

    def set_track_colour(self, track_id: int, colour: str) -> None:
        """Recolour a track. `colour` is an 8-hex ARGB string (e.g. 'ffef5350'), with or
        without a leading '#'."""
        self._ack(self.stub.SetTrackColour(pb.SetTrackColourRequest(track_id=track_id, colour=colour)))

    def move_track(self, track_id: int, delta: int) -> None:
        """Reorder a track: delta<0 moves it up (toward the top), delta>0 down. No-op (error)
        if the track is already at that edge."""
        self._ack(self.stub.MoveTrack(pb.MoveTrackRequest(track_id=track_id, delta=delta)))

    def set_track_polarity(self, track_id: int, invert: bool) -> None:
        """Phase-invert (polarity flip) a track's contribution to the mix — negates its
        samples so it can cancel a correlated layer."""
        self._ack(self.stub.SetTrackPolarity(pb.SetTrackPolarityRequest(track_id=track_id, invert=invert)))

    # -- clips ------------------------------------------------------------
    def add_clip(self, track_id: int, notes: Iterable[pb.Note] = (),
                 start_beat: float = 0.0, length_beats: float = 4.0,
                 content_len_beats: Optional[float] = None, looped: bool = False,
                 name: str = "") -> tuple[int, int]:
        if content_len_beats is None:
            content_len_beats = length_beats
        r = self.stub.AddClip(pb.AddClipRequest(
            track_id=track_id, start_beat=start_beat, length_beats=length_beats,
            content_len_beats=content_len_beats, looped=looped,
            notes=list(notes), name=name))
        return (r.track_id, r.index)

    def add_audio_clip(self, track_id: int, path: str, start_beat: float = 0.0,
                       gain: float = 1.0) -> tuple[int, int]:
        r = self.stub.AddAudioClip(pb.AddAudioClipRequest(
            track_id=track_id, start_beat=start_beat, path=path, gain=gain))
        return (r.track_id, r.index)

    def remove_clip(self, track_id: int, index: int) -> None:
        self._ack(self.stub.RemoveClip(pb.ClipRef(track_id=track_id, index=index)))

    def move_clip(self, track_id: int, index: int, start_beat: float,
                  to_track_id: Optional[int] = None) -> None:
        kw = {"track_id": track_id, "index": index, "start_beat": start_beat}
        if to_track_id is not None:
            kw["to_track_id"] = to_track_id
        self._ack(self.stub.MoveClip(pb.MoveClipRequest(**kw)))

    def split_clip(self, track_id: int, index: int, beat: float) -> int:
        """Split a clip at an absolute beat; returns the new (right-hand) clip index."""
        r = self.stub.SplitClip(pb.SplitClipRequest(track_id=track_id, index=index, beat=beat))
        return r.index

    def split_clip_equal(self, track_id: int, index: int, pieces: int) -> int:
        """Chop a clip into `pieces` equal-length clips (a loop chopper); returns the number
        of pieces produced."""
        return self.stub.SplitClipEqual(pb.SplitEqualRequest(track_id=track_id, index=index, pieces=pieces)).slices

    def split_clip_at_marker(self, track_id: int, index: int, marker: str) -> int:
        """Split a clip at a named timeline location; new (right) clip index, or -1."""
        r = self.stub.SplitClipAtMarker(pb.SplitAtMarkerRequest(track_id=track_id, index=index, marker=marker))
        return r.index

    def slice_at_transients(self, track_id: int, index: int, sensitivity: float = 1.0) -> int:
        """Slice an audio clip at detected onsets; returns the resulting slice count (1 = none)."""
        r = self.stub.SliceAtTransients(pb.SliceTransientsRequest(track_id=track_id, index=index, sensitivity=sensitivity))
        return r.slices

    def set_clip_muted(self, track_id: int, index: int, muted: bool = True) -> None:
        """Mute (disable) or enable a clip in the arrangement without deleting it."""
        self._ack(self.stub.SetClipMuted(pb.ClipMuteRequest(track_id=track_id, index=index, muted=muted)))

    def rename_clip(self, track_id: int, index: int, name: str) -> None:
        """Rename a clip (its label). An empty name makes the clip fall back to showing the
        track's name."""
        self._ack(self.stub.RenameClip(pb.RenameClipRequest(track_id=track_id, index=index, name=name)))

    def set_clip_colour(self, track_id: int, index: int, colour: str) -> None:
        """Per-clip colour override (8-hex ARGB, e.g. 'ffef5350'). An empty string clears the
        override so the clip inherits the track's colour."""
        self._ack(self.stub.SetClipColour(pb.ClipColourRequest(track_id=track_id, index=index, colour=colour)))

    def set_loop_to_clip(self, track_id: int, index: int) -> None:
        """Set the transport loop to a clip's span and enable looping (audition on repeat)."""
        self._ack(self.stub.SetLoopToClip(pb.ClipRef(track_id=track_id, index=index)))

    def regenerate_clip(self, track_id: int, index: int, script: str = "",
                        lang: str = "", seed: int = 0,
                        generator: str = "", system: str = "") -> None:
        """Mark a clip as a script clip and generate its notes from the kernel.

        A clip's generator is EITHER a source file (``script``, a project-relative path) OR a
        named generator (``generator``): ``"pkg.mod:fn"`` in Python / ``"pkg:sym"`` in Lisp, with
        ``system`` the ASDF system / import root to load. ``lang`` is inferred when omitted — from
        the script extension (``.py`` -> python, ``.lisp``/``.cl`` -> common-lisp) or the
        generator's shape — so Python users needn't spell it out. It still routes to the matching
        kernel (SBCL for Lisp; the auto-launched or attached Python kernel for Python).
        """
        self._ack(self.stub.RegenerateClip(pb.RegenerateRequest(
            track_id=track_id, index=index, script=script,
            lang=lang or _infer_lang(script, generator), seed=seed,
            generator=generator, system=system)))

    def set_clip_script_live(self, track_id: int, index: int, live: bool = True) -> None:
        """Toggle a script clip's "Live" flag — auto-regenerate ~1 bar before it plays."""
        self._ack(self.stub.SetClipScriptLive(pb.ClipLiveRequest(
            track_id=track_id, index=index, live=live)))

    def get_clip_notes(self, track_id: int, index: int) -> list[dict]:
        """Return a clip's notes as dicts: pitch, start_beat, length_beats, velocity."""
        r = self.stub.GetClipNotes(pb.ClipRef(track_id=track_id, index=index))
        return [{"pitch": n.pitch, "start_beat": n.start_beat,
                 "length_beats": n.length_beats, "velocity": n.velocity} for n in r.notes]

    def duplicate_clip(self, track_id: int, index: int, at_beat: float = -1.0) -> int:
        """Copy a clip to at_beat (default -1 = butt up right after it); returns new index."""
        r = self.stub.DuplicateClip(pb.DuplicateClipRequest(track_id=track_id, index=index, at_beat=at_beat))
        return r.index

    def repeat_clip(self, track_id: int, index: int, copies: int) -> int:
        """Tile `copies` back-to-back duplicates after a clip; returns copies added."""
        return self.stub.RepeatClip(pb.RepeatClipRequest(track_id=track_id, index=index, copies=copies)).slices

    def reverse_clip(self, track_id: int, index: int) -> None:
        self._ack(self.stub.ReverseClip(pb.ClipRef(track_id=track_id, index=index)))

    def crop_clip(self, track_id: int, index: int, start_beat: float, end_beat: float) -> None:
        """Trim a MIDI clip to the absolute beat range [start_beat, end_beat)."""
        self._ack(self.stub.CropClip(pb.CropClipRequest(track_id=track_id, index=index,
                                                        start_beat=start_beat, end_beat=end_beat)))

    def consolidate_clip(self, track_id: int, index: int) -> None:
        """Flatten a looped MIDI clip's repetitions into explicit notes and un-loop it."""
        self._ack(self.stub.ConsolidateClip(pb.ClipRef(track_id=track_id, index=index)))

    def scale_clip_time(self, track_id: int, index: int, factor: float = 0.5) -> None:
        """Time-scale a MIDI clip: multiply note starts/lengths and the clip's content+slot
        by factor (0.5 = double-time / twice as fast, 2 = half-time / twice as slow)."""
        self._ack(self.stub.ScaleClipTime(pb.ScaleTimeRequest(track_id=track_id, index=index, factor=factor)))

    def bounce_clip(self, track_id: int, index: int) -> int:
        """Freeze a clip to audio on a new track; returns the new track id (-1 on failure)."""
        return self.stub.BounceClip(pb.ClipRef(track_id=track_id, index=index)).id

    def set_clip_gain(self, track_id: int, index: int, gain_db: float) -> None:
        """Set an audio clip's playback gain in dB."""
        self._ack(self.stub.SetClipGain(pb.ClipGainRequest(track_id=track_id, index=index, gain_db=gain_db)))

    def normalize_clip(self, track_id: int, index: int, target_dbfs: float = 0.0) -> None:
        """Set an audio clip's gain so its loudest sample sits at target_dbfs (0 = full scale)."""
        self._ack(self.stub.NormalizeClip(pb.NormalizeClipRequest(track_id=track_id, index=index, target_dbfs=target_dbfs)))

    def set_clip_fades(self, track_id: int, index: int, fade_in_beats: float = 0.0, fade_out_beats: float = 0.0) -> None:
        """Set an audio clip's fade-in / fade-out lengths, in beats."""
        self._ack(self.stub.SetClipFades(pb.ClipFadesRequest(
            track_id=track_id, index=index, fade_in_beats=fade_in_beats, fade_out_beats=fade_out_beats)))

    def set_clip_fade_shape(self, track_id: int, index: int, shape: int = 0) -> None:
        """Set an audio clip's fade curve shape: 0 linear, 1 equal-power (constant-power,
        louder than linear), 2 exponential (slow start). Applies to both fade edges."""
        self._ack(self.stub.SetClipFadeShape(pb.ClipFadeShapeRequest(
            track_id=track_id, index=index, shape=shape)))

    def clip_notes(self, track_id: int, index: int) -> list[dict]:
        r = self.stub.GetClipNotes(pb.ClipRef(track_id=track_id, index=index))
        return [{"pitch": n.pitch, "start_beat": n.start_beat,
                 "length_beats": n.length_beats, "velocity": n.velocity} for n in r.notes]

    def set_clip_transpose(self, track_id: int, index: int, semitones: int) -> None:
        """Non-destructive playback transpose of a MIDI clip (semitones, +/-48). The stored
        notes are untouched — the offset is applied at render time (unlike transpose_clip)."""
        self._ack(self.stub.SetClipTranspose(pb.ClipTransposeRequest(
            track_id=track_id, index=index, semitones=semitones)))

    def set_clip_velocity(self, track_id: int, index: int, scale: float) -> None:
        """Non-destructive playback velocity scale of a MIDI clip (multiplier 0..2). The
        stored note velocities are untouched — the scale is applied at render time."""
        self._ack(self.stub.SetClipVelocity(pb.ClipVelocityRequest(
            track_id=track_id, index=index, scale=scale)))

    def set_clip_probability(self, track_id: int, index: int, probability: float) -> None:
        """Set every note's fire probability in a MIDI clip (0..1). A generative gate: each
        note (per looped repetition) fires with this chance, deterministically (reproducible
        renders). Per-note probabilities can also be set via add_clip's Note.probability."""
        self._ack(self.stub.SetClipProbability(pb.ClipVelocityRequest(
            track_id=track_id, index=index, scale=probability)))

    def export_notes_json(self, track_id: int, index: int) -> str:
        """A clip's notes as a JSON array string [{pitch,start,length,velocity},...]."""
        return self.stub.ExportNotesJSON(pb.ClipRef(track_id=track_id, index=index)).json

    def import_notes_json(self, track_id: int, json_text: str, start_beat: float = 0.0) -> int:
        """Build a new clip on track_id at start_beat from a JSON note array (the shape
        export_notes_json emits). Returns the new clip index, or -1 if no usable notes."""
        return self.stub.ImportNotesJSON(pb.ImportNotesRequest(
            track_id=track_id, start_beat=start_beat, json=json_text)).index

    def quantize_clip(self, track_id: int, index: int, grid: float = 0.25, strength: float = 1.0) -> None:
        """Snap note starts toward a beat grid (0.25 = 16ths). strength 0..1: 1 = full snap,
        0.5 = halfway (tighten without robotizing)."""
        self._ack(self.stub.QuantizeClip(pb.QuantizeRequest(track_id=track_id, index=index, grid=grid, strength=strength)))

    def transpose_clip(self, track_id: int, index: int, semitones: int) -> None:
        self._ack(self.stub.TransposeClip(pb.TransposeRequest(track_id=track_id, index=index, semitones=semitones)))

    def humanize_clip(self, track_id: int, index: int, timing: float = 0.02, velocity: float = 0.1) -> None:
        self._ack(self.stub.HumanizeClip(pb.HumanizeRequest(
            track_id=track_id, index=index, timing=timing, velocity=velocity)))

    def strum_clip(self, track_id: int, index: int, step_beats: float = 0.05, down: bool = True) -> None:
        """Fan out chord voices (notes sharing a start beat) by step_beats each; down = high->low."""
        self._ack(self.stub.StrumClip(pb.StrumRequest(
            track_id=track_id, index=index, step_beats=step_beats, down=down)))

    def arpeggiate_clip(self, track_id: int, index: int, step_beats: float = 0.25, mode: int = 0) -> None:
        """Turn each chord into an arpeggio (destructive); mode 0=up, 1=down, 2=up-down."""
        self._ack(self.stub.ArpeggiateClip(pb.ArpeggiateRequest(
            track_id=track_id, index=index, step_beats=step_beats, mode=mode)))

    def legato_clip(self, track_id: int, index: int, amount: float = 1.0) -> None:
        """Legato: stretch each note's length so it reaches the next distinct onset, blended by
        amount (0 = unchanged, 1 = notes exactly touch the next onset). Chords extend together."""
        self._ack(self.stub.LegatoClip(pb.LegatoRequest(
            track_id=track_id, index=index, amount=amount)))

    def ramp_clip_velocity(self, track_id: int, index: int, frm: float = 0.3, to: float = 1.0) -> None:
        """Velocity ramp: interpolate each note's velocity from `frm` at the first onset to `to`
        at the last onset (crescendo if frm<to, decrescendo if frm>to). Destructive."""
        self._ack(self.stub.RampClipVelocity(pb.VelRampRequest(
            track_id=track_id, index=index, **{'from': frm, 'to': to})))

    def echo_clip(self, track_id: int, index: int, delay_beats: float = 0.5,
                  repeats: int = 3, feedback: float = 0.6) -> None:
        """MIDI echo: append `repeats` decaying copies of every note, each delay_beats later,
        velocity multiplied by feedback each step (copies fading below ~1% are dropped)."""
        self._ack(self.stub.EchoClip(pb.EchoRequest(
            track_id=track_id, index=index, delay_beats=delay_beats, repeats=repeats, feedback=feedback)))

    def invert_clip(self, track_id: int, index: int) -> None:
        """Melodic inversion: mirror every note's pitch around the earliest note's pitch, so
        intervals flip direction (ascending <-> descending). Timing/length/velocity kept."""
        self._ack(self.stub.InvertClip(pb.ClipRef(track_id=track_id, index=index)))

    def ratchet_clip(self, track_id: int, index: int, subdivisions: int = 2) -> None:
        """Ratchet/roll: subdivide each note into `subdivisions` equal same-pitch hits filling
        its span (a drum roll / stutter). Pitch and velocity kept."""
        self._ack(self.stub.RatchetClip(pb.RatchetRequest(track_id=track_id, index=index, subdivisions=subdivisions)))

    def harmonize_clip(self, track_id: int, index: int, semitones: int) -> None:
        """Harmonize: add a parallel voice `semitones` from each note (e.g. +7 a fifth, +12
        an octave), keeping the originals — a monophonic line becomes parallel harmony.
        Voices that fall off the 0..127 keyboard are dropped."""
        self._ack(self.stub.HarmonizeClip(pb.HarmonizeRequest(track_id=track_id, index=index, semitones=semitones)))

    def swing_clip(self, track_id: int, index: int, grid_beats: float = 0.5, amount: float = 0.33) -> None:
        """Swing/groove: delay every off-beat note (one sitting on an odd `grid_beats` line) by
        `amount` of a grid step, baking a shuffle into the clip. amount 0=straight, ~0.33=triplet
        feel. On-beat notes stay; length/pitch/velocity preserved."""
        self._ack(self.stub.SwingClip(pb.SwingClipRequest(track_id=track_id, index=index, grid_beats=grid_beats, amount=amount)))

    def chordify_clip(self, track_id: int, index: int, chord_type: int = 0) -> None:
        """Chordify: turn every note into a named chord by adding voices above the root.
        chord_type: 0 major {+4,+7}, 1 minor {+3,+7}, 2 dom7 {+4,+7,+10}, 3 diminished
        {+3,+6}, 4 sus4 {+5,+7}. Originals kept as roots; off-keyboard voices dropped."""
        self._ack(self.stub.ChordifyClip(pb.ChordifyRequest(track_id=track_id, index=index, chord_type=chord_type)))

    def gate_clip(self, track_id: int, index: int, factor: float = 0.5) -> None:
        """Gate/articulation: scale every note's length by `factor` (keeping its start) —
        0.5 = staccato (detached), 1.5 = tenuto (fuller). Rhythm is unchanged."""
        self._ack(self.stub.GateClip(pb.GateRequest(track_id=track_id, index=index, factor=factor)))

    def flatten_clip_velocity(self, track_id: int, index: int, velocity: float = 0.8) -> None:
        """Flatten dynamics: set every note's velocity to a single value (0..1) — make a part
        mechanically even. Distinct from velocity-scale (multiply) and ramp (gradient)."""
        self._ack(self.stub.FlattenClipVelocity(pb.FlattenVelRequest(track_id=track_id, index=index, velocity=velocity)))

    def split_notes_at_beat(self, track_id: int, index: int, beat: float) -> None:
        """Knife: cut every note spanning `beat` (clip-relative) into two abutting notes."""
        self._ack(self.stub.SplitNotesAtBeat(pb.SplitNotesRequest(
            track_id=track_id, index=index, beat=beat)))

    def set_track_arp(self, track_id: int, enabled: bool = True, rate: float = 0.25,
                      octaves: int = 1, gate: float = 0.5, mode: int = 0,
                      swing: float = 0.0, hold: bool = False, probability: float = 1.0) -> None:
        """Live (non-destructive) per-track arpeggiator; mode 0=up 1=down 2=updown 3=random.
        swing 0..0.9 delays every other step; hold latches the last chord across rests.
        probability 0..1 is the chance each generated step fires (a deterministic generative
        gate; each looped repetition rolls independently, renders stay reproducible)."""
        self._ack(self.stub.SetTrackArp(pb.ArpSpec(
            track_id=track_id, enabled=enabled, rate=rate, octaves=octaves, gate=gate,
            mode=mode, swing=swing, hold=hold, probability=probability)))

    def add_chord(self, track_id: int, index: int, root: int, type: str = "maj",
                  start_beat: float = 0.0, length_beats: float = 1.0,
                  velocity: float = 0.8, inversion: int = 0) -> None:
        """Stamp a chord (maj/min/7/maj7/min7/sus4/... ) into a clip at a beat position."""
        self._ack(self.stub.AddChord(pb.ChordRequest(
            track_id=track_id, index=index, root=root, type=type, start_beat=start_beat,
            length_beats=length_beats, velocity=velocity, inversion=inversion)))

    # -- mixer / effects --------------------------------------------------
    def list_inserts(self) -> list[dict]:
        r = self.stub.ListInserts(pb.Empty())
        return [{"index": i.index, "name": i.name, "volume": i.volume,
                 "pan": i.pan, "mute": i.mute, "solo": i.solo,
                 "effects": [{"slot": e.slot, "name": e.name, "bypassed": e.bypassed}
                             for e in i.effects]} for i in r.inserts]

    def set_insert_params(self, index: int, *, volume=None, pan=None,
                          mute=None, solo=None) -> None:
        kw = {"index": index}
        for k, v in (("volume", volume), ("pan", pan), ("mute", mute), ("solo", solo)):
            if v is not None:
                kw[k] = v
        self._ack(self.stub.SetInsertParams(pb.InsertParams(**kw)))

    def set_insert_name(self, index: int, name: str) -> None:
        """Rename a mixer strip (insert). Empty/whitespace names are rejected."""
        self._ack(self.stub.SetInsertName(pb.InsertName(index=index, name=name)))

    def add_effect(self, insert: int, type) -> tuple[int, int]:
        r = self.stub.AddEffect(pb.AddEffectRequest(insert=insert, type=_fx(type)))
        return (r.insert, r.slot)

    def add_plugin_effect(self, insert: int, identifier: str) -> tuple[int, int]:
        r = self.stub.AddPluginEffect(pb.AddPluginEffectRequest(
            insert=insert, identifier=identifier))
        return (r.insert, r.slot)

    def remove_effect(self, insert: int, slot: int) -> None:
        self._ack(self.stub.RemoveEffect(pb.EffectRef(insert=insert, slot=slot)))

    def set_effect_param(self, insert: int, slot: int, name: str, value: float) -> None:
        self._ack(self.stub.SetEffectParam(pb.EffectParamSet(
            insert=insert, slot=slot, name=name, value=value)))

    def set_effect_bypass(self, insert: int, slot: int, bypassed: bool) -> None:
        self._ack(self.stub.SetEffectBypass(pb.EffectBypassSet(
            insert=insert, slot=slot, bypassed=bypassed)))

    # -- buses & sends ----------------------------------------------------
    def add_bus(self, name: str = "Bus") -> int:
        """Append a bus mixer track (receives sends, sums to master); returns its insert index."""
        return self.stub.AddBus(pb.AddBusRequest(name=name)).id

    def remove_bus(self, index: int) -> None:
        """Remove a bus mixer track; sends are re-indexed (drops sends to it, shifts higher)."""
        self._ack(self.stub.RemoveBus(pb.TrackId(id=index)))

    def set_send(self, insert: int, bus: int, level: float, post_fader: bool = False) -> None:
        """Aux send from an insert to a bus at level (level<=0 removes). post_fader=True makes
        the send follow the source fader/mute; the default (pre-fader) taps at a fixed level."""
        self._ack(self.stub.SetSend(pb.SetSendRequest(insert=insert, bus=bus, level=level, post_fader=post_fader)))

    # -- control groups (VCA-lite) ----------------------------------------
    def define_control_group(self, name: str, gain: float = 1.0) -> None:
        """Create/update a control group whose fader scales its member inserts."""
        self._ack(self.stub.DefineControlGroup(pb.ControlGroupGain(name=name, gain=gain)))

    def set_control_group_gain(self, name: str, gain: float) -> None:
        self._ack(self.stub.SetControlGroupGain(pb.ControlGroupGain(name=name, gain=gain)))

    def set_control_group_mute(self, name: str, mute: bool) -> None:
        self._ack(self.stub.SetControlGroupMute(pb.ControlGroupMute(name=name, mute=mute)))

    def set_control_group_solo(self, name: str, solo: bool = True) -> None:
        """VCA solo: while any group is soloed, only members of soloed groups are audible."""
        self._ack(self.stub.SetControlGroupSolo(pb.ControlGroupSolo(name=name, solo=solo)))

    def assign_insert_to_group(self, insert: int, group: str) -> None:
        """Assign an insert to a control group (group='' clears membership)."""
        self._ack(self.stub.AssignInsertToGroup(pb.GroupAssign(insert=insert, group=group)))

    def remove_control_group(self, name: str) -> None:
        self._ack(self.stub.RemoveControlGroup(pb.GroupName(name=name)))

    def list_control_groups(self) -> list:
        """List control groups as (name, gain, mute, solo, members) tuples."""
        return [(g.name, g.gain, g.mute, g.solo, g.members)
                for g in self.stub.ListControlGroups(pb.Empty()).groups]

    # -- mixer scenes (named snapshots) -----------------------------------
    def define_mixer_scene(self, name: str) -> None:
        """Snapshot the current mixer strip (vol/pan/mute/solo + effect bypass)."""
        self._ack(self.stub.DefineMixerScene(pb.SceneName(name=name)))

    def list_mixer_scenes(self) -> list[str]:
        return list(self.stub.ListMixerScenes(pb.Empty()).names)

    def recall_mixer_scene(self, name: str) -> None:
        self._ack(self.stub.RecallMixerScene(pb.SceneName(name=name)))

    def remove_mixer_scene(self, name: str) -> None:
        self._ack(self.stub.RemoveMixerScene(pb.SceneName(name=name)))

    # -- presets ----------------------------------------------------------
    def list_presets(self, category: str) -> list[str]:
        """category = 'synth' | 'effects'."""
        return list(self.stub.ListPresets(pb.PresetCategory(category=category)).names)

    def save_synth_preset(self, track_id: int, name: str) -> None:
        self._ack(self.stub.SaveSynthPreset(pb.PresetRef(target=track_id, name=name)))

    def load_synth_preset(self, track_id: int, name: str) -> None:
        self._ack(self.stub.LoadSynthPreset(pb.PresetRef(target=track_id, name=name)))

    def save_instrument_preset(self, track_id: int, name: str) -> None:
        """Save a track's instrument (synth or SFZ) as a reusable preset."""
        self._ack(self.stub.SaveInstrumentPreset(pb.PresetRef(target=track_id, name=name)))

    def load_instrument_preset(self, track_id: int, name: str) -> None:
        """Replace a track's instrument with a saved preset (may change its type)."""
        self._ack(self.stub.LoadInstrumentPreset(pb.PresetRef(target=track_id, name=name)))

    def save_effect_preset(self, insert: int, name: str) -> None:
        self._ack(self.stub.SaveEffectPreset(pb.PresetRef(target=insert, name=name)))

    def load_effect_preset(self, insert: int, name: str) -> None:
        self._ack(self.stub.LoadEffectPreset(pb.PresetRef(target=insert, name=name)))

    def effect_params(self, insert: int, slot: int) -> list[dict]:
        r = self.stub.GetEffectParams(pb.EffectRef(insert=insert, slot=slot))
        return [{"name": p.name, "value": p.value, "min": p.min, "max": p.max}
                for p in r.params]

    # -- universal parameter model ----------------------------------------
    # Every automatable value under one stable string id, e.g.
    #   "track/0/volume"  "track/0/synth/cutoff"  "insert/0/pan"  "effect/0/1/Wet"
    def list_parameters(self) -> list[dict]:
        r = self.stub.ListParameters(pb.Empty())
        return [{"id": p.id, "name": p.name, "value": p.value, "min": p.min,
                 "max": p.max, "default": p.default_value,
                 "unit": p.unit, "scaling": p.scaling} for p in r.params]

    def get_parameter(self, id: str) -> dict:
        p = self.stub.GetParameter(pb.ParameterId(id=id))
        return {"id": p.id, "name": p.name, "value": p.value, "min": p.min,
                "max": p.max, "unit": p.unit, "scaling": p.scaling}

    def set_parameter(self, id: str, value: float) -> None:
        self._ack(self.stub.SetParameter(pb.ParameterSet(id=id, value=value)))

    def set_parameter_normalized(self, id: str, pos01: float) -> None:
        """Set a parameter from a 0..1 knob position, mapped through its scaling (log/dB/linear)."""
        self._ack(self.stub.SetParameterNormalized(pb.ParameterSet(id=id, value=pos01)))

    # -- controller mapping / MIDI-learn ----------------------------------
    def add_controller_map(self, source: str, target: str, lo: float = 0.0, hi: float = 1.0) -> None:
        """Map a source (cc:<n> / osc:<name> / any string) to a ParamModel target, scaled to [lo,hi].
        Set lo>hi to invert the mapping."""
        self._ack(self.stub.AddControllerMap(pb.ControllerMap(source=source, target=target, lo=lo, hi=hi)))

    def set_controller_bypass(self, source: str, target: str, bypass: bool) -> None:
        """Enable/disable a controller map without removing it."""
        self._ack(self.stub.SetControllerBypass(pb.ControllerBypass(source=source, target=target, bypass=bypass)))

    def remove_controller_map(self, source: str) -> None:
        self._ack(self.stub.RemoveControllerMap(pb.ControllerSource(source=source)))

    def list_controller_maps(self) -> list[dict]:
        r = self.stub.ListControllerMaps(pb.Empty())
        return [{"source": m.source, "target": m.target, "lo": m.lo, "hi": m.hi, "bypass": m.bypass} for m in r.maps]

    def set_controller(self, source: str, value: float) -> None:
        """Feed a controller source a 0..1 value (MIDI CC and OSC feed the same path)."""
        self._ack(self.stub.SetController(pb.ControllerValue(source=source, value=value)))

    def midi_learn(self, target: str) -> None:
        """Arm learn: the next controller fed binds to this ParamModel target ('' cancels)."""
        self._ack(self.stub.MidiLearn(pb.LearnRequest(target=target)))

    # -- modulation matrix (LFO -> parameter) -----------------------------
    def set_modulation(self, target: str, rate: float, depth: float,
                       shape: int = 0, center: float = 0.0, sync_beats: float = 0.0,
                       phase: float = 0.0, unipolar: bool = False, slew_ms: float = 0.0) -> None:
        """LFO on a ParamModel target: value = center + depth*unit(phase). shape 0=sine 1=tri 2=saw 3=square 4=random(S&H).
        Replaces any modulation already on the target (use add_modulation to stack a second source).
        sync_beats>0 tempo-syncs the LFO (one cycle per sync_beats beats); otherwise rate is in Hz.
        phase (0..1) offsets the waveform start; unipolar keeps the value on one side (center..center+depth);
        slew_ms>0 applies a one-pole slew (ms time constant) softening abrupt value changes."""
        self._ack(self.stub.SetModulation(pb.ModRoute(
            target=target, rate=rate, depth=depth, center=center, shape=shape,
            sync_beats=sync_beats, phase=phase, unipolar=unipolar, slew_ms=slew_ms)))

    def add_modulation(self, target: str, rate: float, depth: float,
                       shape: int = 0, center: float = 0.0, sync_beats: float = 0.0,
                       phase: float = 0.0, unipolar: bool = False, slew_ms: float = 0.0) -> None:
        """Append an ADDITIONAL modulation source on target. Multiple sources on the same
        target sum (value = center + sum of depth*unit), so e.g. two LFOs at different rates
        stack. Same args as set_modulation, which instead replaces any source on the target.
        shape 4 = random/sample-and-hold. Remove them all with remove_modulation(target)."""
        self._ack(self.stub.AddModulation(pb.ModRoute(
            target=target, rate=rate, depth=depth, center=center, shape=shape,
            sync_beats=sync_beats, phase=phase, unipolar=unipolar, slew_ms=slew_ms)))

    def remove_modulation(self, target: str) -> None:
        self._ack(self.stub.RemoveModulation(pb.ModTarget(target=target)))

    def list_modulations(self) -> list[dict]:
        r = self.stub.ListModulations(pb.Empty())
        return [{"target": m.target, "rate": m.rate, "depth": m.depth,
                 "center": m.center, "shape": m.shape, "sync_beats": m.sync_beats,
                 "phase": m.phase, "unipolar": m.unipolar, "slew_ms": m.slew_ms} for m in r.mods]

    # -- tempo map --------------------------------------------------------
    def add_tempo_marker(self, beat: float, bpm: float) -> None:
        self._ack(self.stub.AddTempoMarker(pb.TempoMarker(beat=beat, bpm=bpm)))

    def remove_tempo_marker(self, beat: float) -> None:
        self._ack(self.stub.RemoveTempoMarker(pb.TempoMarker(beat=beat, bpm=0)))

    def list_tempo_markers(self) -> list[dict]:
        r = self.stub.ListTempoMarkers(pb.Empty())
        return [{"beat": m.beat, "bpm": m.bpm} for m in r.markers]

    def beats_to_seconds(self, beats: float) -> float:
        return self.stub.BeatsToSeconds(pb.Position(beats=beats)).seconds

    def seconds_to_beats(self, seconds: float) -> float:
        return self.stub.SecondsToBeats(pb.SecondsValue(seconds=seconds)).beats

    def set_time_signature(self, numerator: int, denominator: int) -> None:
        self._ack(self.stub.SetTimeSignature(pb.TimeSignature(numerator=numerator, denominator=denominator)))

    def get_time_signature(self) -> dict:
        r = self.stub.GetTimeSignature(pb.Empty())
        return {"numerator": r.numerator, "denominator": r.denominator, "beats_per_bar": r.beats_per_bar}

    def beats_to_bar_beat(self, beat: float) -> tuple[int, float]:
        """Absolute beat -> (bar, beat_in_bar), both 1-based."""
        r = self.stub.BeatsToBarBeat(pb.BeatPos(beat=beat))
        return (r.bar, r.beat_in_bar)

    def bar_beat_to_beats(self, bar: int, beat_in_bar: float = 1.0) -> float:
        return self.stub.BarBeatToBeats(pb.BarBeat(bar=bar, beat_in_bar=beat_in_bar)).beat

    # -- scales -----------------------------------------------------------
    def set_scale(self, root: int = 0, name: str = "", intervals: Iterable[int] = ()) -> None:
        """Set the project scale by built-in name (major/minor/dorian/...) or explicit intervals."""
        self._ack(self.stub.SetScale(pb.Scale(root=root, name=name, intervals=list(intervals))))

    def get_scale(self) -> dict:
        s = self.stub.GetScale(pb.Empty())
        return {"root": s.root, "name": s.name, "intervals": list(s.intervals)}

    def set_tuning(self, cents12: Iterable[float]) -> None:
        """Microtuning: 12 per-pitch-class cents offsets from equal temperament (all 0 = 12-TET;
        applies to the built-in synth). Must be exactly 12 values."""
        self._ack(self.stub.SetTuning(pb.Tuning(cents=list(cents12))))

    def get_tuning(self) -> list[float]:
        return list(self.stub.GetTuning(pb.Empty()).cents)

    def import_scl(self, path: str) -> None:
        """Load a Scala .scl tuning file (12-note scales map to the chromatic keys)."""
        self._ack(self.stub.ImportScl(pb.FilePath(path=path)))

    def snap_clip_to_scale(self, track_id: int, index: int) -> None:
        self._ack(self.stub.SnapClipToScale(pb.ClipRef(track_id=track_id, index=index)))

    # -- timeline locations -----------------------------------------------
    def add_location(self, name: str, kind: str = "marker",
                     start_beat: float = 0.0, end_beat: float = 0.0) -> None:
        """Named marker/range/section on the timeline (upsert by name)."""
        self._ack(self.stub.AddLocation(pb.TimelineLocation(
            name=name, kind=kind, start_beat=start_beat, end_beat=end_beat)))

    def list_locations(self) -> list[dict]:
        r = self.stub.ListLocations(pb.Empty())
        return [{"name": l.name, "kind": l.kind, "start_beat": l.start_beat,
                 "end_beat": l.end_beat} for l in r.locations]

    def remove_location(self, name: str) -> None:
        self._ack(self.stub.RemoveLocation(pb.LocationName(name=name)))

    # -- session view (clip-launch grid) ----------------------------------

    def copy_clip_to_session_slot(self, track_id: int, clip_index: int, scene: int) -> None:
        """Populate a session slot from an existing arrangement clip (grows the grid)."""
        self._ack(self.stub.CopyClipToSessionSlot(
            pb.SessionSlotSrc(track_id=track_id, clip_index=clip_index, scene=scene)))

    def set_session_slot_colour(self, track_id: int, scene: int, colour: str = "") -> None:
        """Colour a session slot's clip (hex ARGB, e.g. 'ffef5350'); '' clears to inherit the track colour."""
        self._ack(self.stub.SetSessionSlotColour(
            pb.SessionSlotColour(track_id=track_id, scene=scene, colour=colour)))

    def session_launch_clip(self, track_id: int, scene: int) -> None:
        """Queue a clip launch (fires at the next launch-quantum boundary during playback)."""
        self._ack(self.stub.LaunchSessionClip(pb.SessionClipRef(track_id=track_id, scene=scene)))

    def session_launch_scene(self, scene: int) -> None:
        """Queue a scene (row) launch — every occupied slot in the row fires."""
        self._ack(self.stub.LaunchSessionScene(pb.SessionSceneRef(scene=scene)))

    def session_stop_track(self, track_id: int) -> None:
        """Queue a stop on one track (back to arrangement playback)."""
        self._ack(self.stub.StopSessionTrack(pb.TrackId(id=track_id)))

    def session_stop_all(self) -> None:
        self._ack(self.stub.StopSessionAll(pb.Empty()))

    def session_set_quantum(self, beats: float) -> None:
        """Set the session launch-quantize window in beats (0 = launch immediately, 4 = one bar)."""
        self._ack(self.stub.SetLaunchQuantum(pb.LaunchQuantum(beats=beats)))

    def session_state(self) -> dict:
        """Per-track playing/pending slot + scene count + launch quantum.

        playing: -1 arrangement / >=0 scene; pending: -2 none / -1 stop / >=0 scene."""
        r = self.stub.GetSessionState(pb.Empty())
        return {"scenes": r.scenes, "quantum_beats": r.quantum_beats, "any_playing": r.any_playing,
                "tracks": [{"track_id": t.track_id, "playing": t.playing,
                            "pending": t.pending, "slots": t.slots} for t in r.tracks]}

    # -- export profiles (named render targets) ---------------------------
    def define_export_profile(self, name: str, target: str = "mix", range_name: str = "",
                              track_id: int = 0, format: str = "wav",
                              tail_seconds: float = 0.0) -> None:
        """target: 'mix' | 'range' (+range_name) | 'track' (+track_id) | 'stems'."""
        self._ack(self.stub.DefineExportProfile(pb.ExportProfile(
            name=name, target=target, range_name=range_name, track_id=track_id,
            format=format, tail_seconds=tail_seconds)))

    def list_export_profiles(self) -> list[dict]:
        r = self.stub.ListExportProfiles(pb.Empty())
        return [{"name": p.name, "target": p.target, "range_name": p.range_name,
                 "track_id": p.track_id, "format": p.format, "tail_seconds": p.tail_seconds}
                for p in r.profiles]

    def remove_export_profile(self, name: str) -> None:
        self._ack(self.stub.RemoveExportProfile(pb.ExportName(name=name)))

    def run_export(self, name: str, out_dir: str = "") -> list[str]:
        """Render the named profile; returns the list of files written."""
        r = self.stub.RunExport(pb.ExportRun(name=name, out_dir=out_dir))
        if not r.ok:
            raise RuntimeError(r.error or "export failed")
        return list(r.files)

    # -- automation -------------------------------------------------------
    def set_automation(self, target, id: int, points: Iterable[tuple[float, float]],
                       slot: int = 0, param: str = "") -> None:
        tgt = AUTO_TARGETS.get(target.upper(), 0) if isinstance(target, str) else int(target)
        pts = [pb.AutoPoint(beat=b, value=v) for b, v in points]
        self._ack(self.stub.SetAutomation(pb.Automation(
            target=tgt, id=id, slot=slot, param=param, points=pts)))

    def set_automation_by_id(self, param_id: str, points: Iterable[tuple[float, float]]) -> None:
        """Automate a ParamModel id (the same id a controller/LFO addresses). Empty points clears it."""
        pts = [pb.AutoPoint(beat=b, value=v) for b, v in points]
        self._ack(self.stub.SetAutomation(pb.Automation(param_id=param_id, points=pts)))

    def add_automation_point(self, param_id: str, beat: float, value: float) -> None:
        """Append/replace one keyframe on an id-addressed automation lane."""
        self._ack(self.stub.AddAutomationPoint(pb.AddAutoPointRequest(param_id=param_id, beat=beat, value=value)))

    def set_automation_step(self, param_id: str, step: bool = True) -> None:
        """Toggle a param's automation lane between stepped (hold each value until the next
        point) and linear interpolation. The lane must already exist."""
        self._ack(self.stub.SetAutomationStep(pb.AutoStepRequest(param_id=param_id, step=step)))

    def set_automation_curve(self, param_id: str, curve: float) -> None:
        """Ease curve for a param's automation lane: -1 = ease-out (fast start), 0 = linear,
        +1 = ease-in (slow start). Ignored while the lane is stepped. Lane must exist."""
        self._ack(self.stub.SetAutomationCurve(pb.AutoCurveRequest(param_id=param_id, curve=curve)))

    def get_automation(self) -> list[dict]:
        r = self.stub.GetAutomation(pb.Empty())
        return [{"target": a.target, "id": a.id, "slot": a.slot, "param": a.param,
                 "param_id": a.param_id, "points": [(p.beat, p.value) for p in a.points]} for a in r.lanes]

    # -- plugins ----------------------------------------------------------
    def scan_plugins(self, force: bool = False) -> list[dict]:
        return self._plugins(self.stub.ScanPlugins(pb.ScanPluginsRequest(force=force)))

    def list_plugins(self) -> list[dict]:
        return self._plugins(self.stub.ListPlugins(pb.Empty()))

    def open_plugin_editor(self, track_id: int) -> None:
        self._ack(self.stub.OpenPluginEditor(pb.TrackId(id=track_id)))

    # -- project / state --------------------------------------------------
    def get_state(self) -> dict:
        s = self.stub.GetState(pb.Empty())
        return {
            "transport": {"playing": s.transport.playing, "bpm": s.transport.bpm,
                          "position_beats": s.transport.position_beats},
            "tracks": [{"id": t.id, "name": t.name, "type": t.type} for t in s.tracks],
            "inserts": [{"index": i.index, "name": i.name} for i in s.inserts],
        }

    def new_project(self) -> None:
        self._ack(self.stub.NewProject(pb.Empty()))

    def list_templates(self) -> list[str]:
        """Project templates: built-ins ('Starter Beat', 'Drum Kit', 'Lead + Bass')
        plus any user templates saved with save_as_template."""
        return list(self.stub.ListTemplates(pb.Empty()).names)

    def new_from_template(self, name: str) -> None:
        """Empty the project and seed a template (built-in or a saved user template)."""
        self._ack(self.stub.NewFromTemplate(pb.TemplateRef(name=name)))

    def save_as_template(self, name: str) -> None:
        """Save the current project as a reusable user template under the templates dir
        (<userAppData>/Gloopy/templates, or $GLOOPY_TEMPLATE_PATH). It then appears in
        list_templates and can be seeded with new_from_template."""
        self._ack(self.stub.SaveAsTemplate(pb.TemplateRef(name=name)))

    def analyzer_data(self, insert: int, slot: int) -> list[float]:
        """A scope/analyzer effect's live snapshot (the captured waveform samples).
        Non-empty only while audio is flowing through that insert during playback."""
        r = self.stub.GetAnalyzerData(pb.EffectRef(insert=insert, slot=slot))
        return list(r.samples)

    def list_favorites(self) -> list[dict]:
        """The user's pinned browser items (outside the composition). Each is a dict
        {kind, ref, label}: kind in {plugin, sample, preset, template} chooses the action,
        ref is its argument (plugin identifier / file path / patch path / template name)."""
        r = self.stub.ListFavorites(pb.Empty())
        return [{"kind": f.kind, "ref": f.ref, "label": f.label} for f in r.favorites]

    def add_favorite(self, kind: str, ref: str, label: str = "") -> None:
        """Pin a browser item as a favorite (idempotent; deduped by kind+ref)."""
        self._ack(self.stub.AddFavorite(pb.AddFavoriteRequest(kind=kind, ref=ref, label=label or ref)))

    def remove_favorite(self, kind: str, ref: str) -> None:
        """Unpin a favorite (matched by kind+ref)."""
        self._ack(self.stub.RemoveFavorite(pb.RemoveFavoriteRequest(kind=kind, ref=ref)))

    def undo(self) -> None:
        self._ack(self.stub.Undo(pb.Empty()))

    def redo(self) -> None:
        self._ack(self.stub.Redo(pb.Empty()))

    def load_project(self, path: str) -> None:
        self._ack(self.stub.LoadProject(pb.FilePath(path=path)))

    def save_project(self, path: str) -> None:
        self._ack(self.stub.SaveProject(pb.FilePath(path=path)))

    def save_composition(self, path: str) -> None:
        """Save the project as a directory 'composition as repo' (diff-friendly TOML+notes)."""
        self._ack(self.stub.SaveComposition(pb.FilePath(path=path)))

    def load_composition(self, path: str) -> None:
        """Load a composition directory (or its gloopy.toml)."""
        self._ack(self.stub.LoadComposition(pb.FilePath(path=path)))

    # -- MIDI file import/export ------------------------------------------
    def export_midi(self, path: str) -> None:
        """Write all instrument tracks to a Type-1 standard MIDI file."""
        self._ack(self.stub.ExportMidi(pb.FilePath(path=path)))

    def export_loop_region(self, path: str) -> None:
        """Bounce the current transport loop window to a WAV/FLAC (the 'export selection'
        action). Errors if no loop is set or it is empty. Encoder picked from the extension."""
        self._ack(self.stub.ExportLoopRegion(pb.FilePath(path=path)))

    def export_track(self, track_id: int, path: str) -> None:
        """Bounce a single track (through its own insert chain, soloed) to a WAV/FLAC stem."""
        self._ack(self.stub.ExportTrack(pb.ExportTrackRequest(track_id=track_id, path=path)))

    def export_stems(self, directory: str) -> None:
        """Bounce every instrument track to its own stem WAV (<id>-<slug>.wav) in `directory`."""
        self._ack(self.stub.ExportStems(pb.FilePath(path=directory)))

    def import_midi(self, path: str) -> None:
        """Load a standard MIDI file as synth tracks + clips."""
        self._ack(self.stub.ImportMidi(pb.FilePath(path=path)))

    def import_audio(self, path: str) -> None:
        """Load an audio file (wav/aiff/flac) as a new audio track."""
        self._ack(self.stub.ImportAudio(pb.FilePath(path=path)))

    def set_project_notes(self, text: str) -> None:
        """Free-form markdown notes saved with the song (composition notes.md)."""
        self._ack(self.stub.SetProjectNotes(pb.TextValue(text=text)))

    def get_project_notes(self) -> str:
        return self.stub.GetProjectNotes(pb.Empty()).text

    def git_available(self) -> dict:
        """Whether the system git binary is present, and its version string."""
        r = self.stub.GitAvailable(pb.Empty())
        return {"available": r.available, "version": r.version}

    def git_status(self, dir: str = "") -> dict:
        """Git status of the project's dir (or `dir`): branch, ahead/behind, dirty files."""
        r = self.stub.GitStatus(pb.GitDir(dir=dir))
        return {"available": r.available, "is_repo": r.is_repo, "detached": r.detached,
                "branch": r.branch, "ahead": r.ahead, "behind": r.behind, "dir": r.dir,
                "changes": [{"xy": c.xy, "path": c.path} for c in r.changes]}

    def git_init(self, dir: str) -> None:
        """Turn a folder into a git repository (`git init`); creates the folder if needed."""
        self._ack(self.stub.GitInit(pb.GitDir(dir=dir)))

    def git_add(self, dir: str, paths: list[str] | None = None) -> None:
        """Stage changes in `dir` (all of them when `paths` is omitted)."""
        self._ack(self.stub.GitAdd(pb.GitAddRequest(dir=dir, paths=paths or [])))

    def git_commit(self, dir: str, message: str, amend: bool = False) -> None:
        """Commit staged changes in `dir` with `message` (reuses your git identity)."""
        self._ack(self.stub.GitCommit(pb.GitCommitRequest(dir=dir, message=message, amend=amend)))

    def git_log(self, dir: str, max: int = 50) -> list[dict]:
        """Recent commits in `dir` (newest first): hash, parents, refs, author, date, subject."""
        r = self.stub.GitLog(pb.GitLogRequest(dir=dir, max=max))
        return [{"hash": c.hash, "parents": list(c.parents), "refs": c.refs,
                 "author": c.author, "date": c.date, "subject": c.subject} for c in r.commits]

    def git_branches(self, dir: str) -> dict:
        """Current branch + all local branches in `dir`."""
        r = self.stub.GitBranches(pb.GitDir(dir=dir))
        return {"current": r.current, "branches": list(r.branches)}

    def git_branch_create(self, dir: str, name: str, start_point: str = "") -> None:
        """Create a branch `name` (from `start_point`, or the current HEAD)."""
        self._ack(self.stub.GitBranchCreate(pb.GitBranchRequest(dir=dir, name=name, start_point=start_point)))

    def git_checkout(self, dir: str, ref: str) -> None:
        """Switch the working tree to a branch, tag, or commit (changes files on disk)."""
        self._ack(self.stub.GitCheckout(pb.GitRefRequest(dir=dir, ref=ref)))

    def git_merge(self, dir: str, name: str) -> None:
        """Merge branch `name` into the current branch."""
        self._ack(self.stub.GitMerge(pb.GitRefRequest(dir=dir, ref=name)))

    def git_branch_delete(self, dir: str, name: str, force: bool = False) -> None:
        """Delete branch `name` (force=True for -D)."""
        self._ack(self.stub.GitBranchDelete(pb.GitBranchRequest(dir=dir, name=name, force=force)))

    def git_branch_rename(self, dir: str, name: str, new_name: str) -> None:
        """Rename branch `name` to `new_name`."""
        self._ack(self.stub.GitBranchRename(pb.GitBranchRequest(dir=dir, name=name, new_name=new_name)))

    def git_tags(self, dir: str) -> list[str]:
        """All tags in `dir`, newest-created first."""
        return list(self.stub.GitTags(pb.GitDir(dir=dir)).tags)

    def git_tag_create(self, dir: str, name: str, message: str = "") -> None:
        """Tag the current commit (annotated when `message` is given, else lightweight)."""
        self._ack(self.stub.GitTagCreate(pb.GitTagRequest(dir=dir, name=name, message=message)))

    def git_tag_delete(self, dir: str, name: str) -> None:
        """Delete tag `name`."""
        self._ack(self.stub.GitTagDelete(pb.GitTagRequest(dir=dir, name=name)))

    def git_diff(self, dir: str, pathspec: str = "", rev_a: str = "", rev_b: str = "") -> dict:
        """Unified diff. rev_a+rev_b = between two revisions; rev_a only = rev_a..working tree;
        neither = working tree vs HEAD. Returns {ok, error, diff, files:[{status,path}]}."""
        r = self.stub.GitDiff(pb.GitDiffRequest(dir=dir, pathspec=pathspec, rev_a=rev_a, rev_b=rev_b))
        return {"ok": r.ok, "error": r.error, "diff": r.diff,
                "files": [{"status": f.status, "path": f.path} for f in r.files]}

    def git_discard(self, dir: str, paths: list[str] | None = None) -> None:
        """Discard uncommitted tracked changes (empty paths = all)."""
        self._ack(self.stub.GitDiscard(pb.GitPathsRequest(dir=dir, paths=paths or [])))

    def git_stash(self, dir: str, message: str = "") -> None:
        """Shelve tracked changes onto a stash."""
        self._ack(self.stub.GitStash(pb.GitStashRequest(dir=dir, message=message)))

    def git_stash_pop(self, dir: str) -> None:
        """Restore (and drop) the latest stash."""
        self._ack(self.stub.GitStashPop(pb.GitDir(dir=dir)))

    def git_stash_list(self, dir: str) -> list[str]:
        """List stashes ("stash@{0}: ...")."""
        return list(self.stub.GitStashList(pb.GitDir(dir=dir)).stashes)

    def git_revert(self, dir: str, commit: str) -> None:
        """Create a new commit that undoes `commit`."""
        self._ack(self.stub.GitRevert(pb.GitRefRequest(dir=dir, ref=commit)))

    def git_reset(self, dir: str, mode: str = "mixed", ref: str = "HEAD") -> None:
        """Reset to `ref` (mode: soft | mixed | hard). Hard discards uncommitted work."""
        self._ack(self.stub.GitReset(pb.GitResetRequest(dir=dir, mode=mode, ref=ref)))

    def git_add_remote(self, dir: str, name: str, url: str) -> None:
        """Add a remote (git remote add <name> <url>)."""
        self._ack(self.stub.GitAddRemote(pb.GitAddRemoteRequest(dir=dir, name=name, url=url)))

    def git_list_remotes(self, dir: str) -> list[dict]:
        """List remotes as [{name, url}]."""
        return [{"name": r.name, "url": r.url} for r in self.stub.GitListRemotes(pb.GitDir(dir=dir)).remotes]

    def git_fetch(self, dir: str, remote: str = "") -> None:
        """Fetch from `remote` (empty = --all)."""
        self._ack(self.stub.GitFetch(pb.GitSyncRequest(dir=dir, remote=remote)))

    def git_pull(self, dir: str, remote: str = "", branch: str = "") -> None:
        """Pull remote/branch."""
        self._ack(self.stub.GitPull(pb.GitSyncRequest(dir=dir, remote=remote, branch=branch)))

    def git_push(self, dir: str, remote: str = "", branch: str = "") -> None:
        """Push remote/branch (explicit; sets upstream)."""
        self._ack(self.stub.GitPush(pb.GitSyncRequest(dir=dir, remote=remote, branch=branch)))

    def git_conflicts(self, dir: str) -> list[str]:
        """List files with merge conflicts."""
        return list(self.stub.GitConflicts(pb.GitDir(dir=dir)).files)

    def git_resolve(self, dir: str, path: str, mode: str) -> None:
        """Resolve a conflicted file (mode: ours | theirs | both), then stage it."""
        self._ack(self.stub.GitResolve(pb.GitResolveRequest(dir=dir, path=path, mode=mode)))

    def git_merge_continue(self, dir: str) -> None:
        """Finish the merge once all conflicts are resolved."""
        self._ack(self.stub.GitMergeContinue(pb.GitDir(dir=dir)))

    def git_merge_abort(self, dir: str) -> None:
        """Abort the in-progress merge."""
        self._ack(self.stub.GitMergeAbort(pb.GitDir(dir=dir)))

    def git_set_identity(self, dir: str, name: str = "", email: str = "") -> None:
        """Set the per-project commit identity (local user.name/email)."""
        self._ack(self.stub.GitSetIdentity(pb.GitIdentity(dir=dir, name=name, email=email)))

    def git_get_identity(self, dir: str) -> dict:
        """Read the per-project commit identity as {name, email}."""
        r = self.stub.GitGetIdentity(pb.GitDir(dir=dir))
        return {"name": r.name, "email": r.email}

    def git_set_auto_commit(self, dir: str, on: bool) -> None:
        """Enable/disable auto-commit-on-save (gloopy.autocommit in .git/config)."""
        self._ack(self.stub.GitSetAutoCommit(pb.GitBoolRequest(dir=dir, value=on)))

    def git_get_auto_commit(self, dir: str) -> bool:
        """Whether auto-commit-on-save is enabled for this repo."""
        return self.stub.GitGetAutoCommit(pb.GitDir(dir=dir)).value

    def diagnostics(self) -> dict:
        """Engine health: device settings, callback timing, DSP load, dropouts, render speed."""
        d = self.stub.GetDiagnostics(pb.Empty())
        return {"sample_rate": d.sample_rate, "block_size": d.block_size,
                "inputs": d.inputs, "outputs": d.outputs, "callback_us": d.callback_us,
                "max_callback_us": d.max_callback_us, "dsp_load": d.dsp_load,
                "dropouts": d.dropouts, "render_speed_x": d.render_speed_x,
                "audio_thread_allocs": d.audio_thread_allocs}

    def project_status(self) -> dict:
        """Status-bar summary: which project, whether it has unsaved edits, the Gloopy
        version, and a compact git working-tree summary (branch + uncommitted count)."""
        s = self.stub.GetProjectStatus(pb.Empty())
        return {"version": s.version, "dir": s.dir, "name": s.name,
                "modified": s.modified, "untitled": s.untitled,
                "is_composition": s.is_composition,
                "git_available": s.git_available, "git_repo": s.git_repo,
                "git_branch": s.git_branch, "git_detached": s.git_detached,
                "git_uncommitted": s.git_uncommitted,
                "git_ahead": s.git_ahead, "git_behind": s.git_behind}

    def get_waveform(self, path: str, buckets: int = 256) -> dict:
        """Cached min/max waveform peaks for an audio file (per-bucket)."""
        r = self.stub.GetWaveform(pb.WaveformRequest(path=path, buckets=buckets))
        return {"mins": list(r.mins), "maxs": list(r.maxs),
                "buckets": r.buckets, "duration_seconds": r.duration_seconds}

    def analyze_file(self, path: str) -> dict:
        """Offline loudness of a WAV: peak/true-peak (dBFS/dBTP), RMS (dBFS), integrated LUFS."""
        r = self.stub.AnalyzeFile(pb.FilePath(path=path))
        return {"peak_dbfs": r.peak_dbfs, "true_peak_dbtp": r.true_peak_dbtp,
                "rms_dbfs": r.rms_dbfs, "lufs": r.lufs,
                "momentary_lufs": r.momentary_lufs, "short_term_lufs": r.short_term_lufs, "lra": r.lra}

    def render(self, path: str, tail_seconds: float = 0.0, start_beat: float = 0.0,
               end_beat: float = 0.0, track_id: Optional[int] = None,
               range_name: str = "", report: bool = False) -> Optional[dict]:
        """Offline bounce. With report=True, also analyse the output and return its
        loudness {peak_dbfs, true_peak_dbtp, rms_dbfs, lufs}; otherwise return None."""
        kw = {"path": path, "tail_seconds": tail_seconds,
              "start_beat": start_beat, "end_beat": end_beat, "range_name": range_name,
              "report": report}
        if track_id is not None:
            kw["track_id"] = track_id
        res = self.stub.RenderToFile(pb.RenderRequest(**kw))
        self._ack(res)
        if not report:
            return None
        lr = res.report
        return {"peak_dbfs": lr.peak_dbfs, "true_peak_dbtp": lr.true_peak_dbtp,
                "rms_dbfs": lr.rms_dbfs, "lufs": lr.lufs,
                "momentary_lufs": lr.momentary_lufs, "short_term_lufs": lr.short_term_lufs, "lra": lr.lra}

    # -- events -----------------------------------------------------------
    def subscribe(self, transport: bool = True, meters: bool = False,
                  changes: bool = False, interval_ms: int = 100) -> Iterator[dict]:
        """Yield a dict per streamed Event until the caller stops iterating."""
        req = pb.SubscribeRequest(transport=transport, meters=meters,
                                  changes=changes, interval_ms=interval_ms)
        for ev in self.stub.Subscribe(req):
            which = ev.WhichOneof("kind")
            if which == "transport":
                yield {"kind": "transport", "playing": ev.transport.playing,
                       "bpm": ev.transport.bpm, "position_beats": ev.transport.position_beats}
            elif which == "meters":
                yield {"kind": "meters", "peak_l": list(ev.meters.peak_l),
                       "peak_r": list(ev.meters.peak_r), "clipped": list(ev.meters.clipped)}
            elif which == "change":
                yield {"kind": "change", "change": ev.change.kind,
                       "track_id": ev.change.track_id, "insert": ev.change.insert}

    # -- helpers ----------------------------------------------------------
    @staticmethod
    def _ack(ack) -> None:
        if not ack.ok:
            raise RuntimeError(ack.error or "Gloopy RPC returned ok=false")

    @staticmethod
    def _plugins(r) -> list[dict]:
        return [{"name": p.name, "format": p.format, "is_instrument": p.is_instrument,
                 "identifier": p.identifier, "vendor": p.vendor, "category": p.category,
                 "version": p.version, "num_inputs": p.num_inputs, "num_outputs": p.num_outputs}
                for p in r.plugins]


@contextmanager
def connect(target: Optional[str] = None) -> Iterator[Gloopy]:
    """Context-manager sugar: ``with connect() as g: ...``. ``target=None``
    auto-discovers Gloopy's port (see :class:`Gloopy`)."""
    g = Gloopy(target)
    try:
        yield g
    finally:
        g.close()
