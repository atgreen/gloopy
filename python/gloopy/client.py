# SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
# SPDX-License-Identifier: AGPL-3.0-only
"""A thin, Pythonic client for Gloopy's gRPC control API.

Mirrors ``examples/gloopy-grpc.lisp``. The DAW listens on 127.0.0.1:50051;
structural edits and queries go over gRPC (live notes/knobs are on OSC 9000).

    from gloopy import Gloopy, note

    g = Gloopy()                      # connects to 127.0.0.1:50051
    g.set_tempo(128)
    tid = g.add_synth_track("lead", wave="SAW")
    g.add_clip(tid, notes=[note(60, 0, 1), note(64, 1, 1), note(67, 2, 2)])
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
EFFECTS = {"GAIN": 0, "FILTER": 1, "DELAY": 2, "REVERB": 3, "LIMITER": 4}
AUTO_TARGETS = {"TRACK_VOL": 0, "TRACK_PAN": 1, "INSERT_VOL": 2,
                "INSERT_PAN": 3, "EFFECT_PARAM": 4}


def note(pitch: int, start_beat: float, length_beats: float,
         velocity: float = 0.8) -> pb.Note:
    """Build a Note for AddClip. Times are in beats, relative to the clip."""
    return pb.Note(pitch=pitch, start_beat=start_beat,
                   length_beats=length_beats, velocity=velocity)


def _wave(w) -> int:
    return WAVEFORMS.get(w.upper(), 0) if isinstance(w, str) else int(w)


def _fx(t) -> int:
    return EFFECTS.get(t.upper(), 0) if isinstance(t, str) else int(t)


class Gloopy:
    """Connection to a running Gloopy instance.

    Usable as a context manager (``with Gloopy() as g: ...``) which closes the
    channel on exit. Methods return plain Python values (ints, dicts, lists).
    """

    def __init__(self, target: str = "127.0.0.1:50051"):
        self.channel = grpc.insecure_channel(target)
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

    def set_tempo(self, bpm: float) -> None:
        self._ack(self.stub.SetTempo(pb.Tempo(bpm=bpm)))

    def set_swing(self, amount: float) -> None:
        """0.5 = straight, up to ~0.75 = triplet feel."""
        self._ack(self.stub.SetSwing(pb.Swing(amount=amount)))

    def seek(self, beats: float) -> None:
        self._ack(self.stub.Seek(pb.Position(beats=beats)))

    def transport(self) -> dict:
        t = self.stub.GetTransport(pb.Empty())
        return {"playing": t.playing, "bpm": t.bpm, "position_beats": t.position_beats}

    def start_recording(self) -> None:
        """Record armed MIDI + armed audio tracks from the playhead."""
        self._ack(self.stub.StartRecording(pb.Empty()))

    def stop_recording(self) -> None:
        self._ack(self.stub.StopRecording(pb.Empty()))

    def list_audio_inputs(self) -> list[str]:
        return list(self.stub.ListAudioInputs(pb.Empty()).names)

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

    def add_sfz_track(self, path: str, name: str = "") -> int:
        """Load a native SFZ instrument (samples preloaded) onto a new track."""
        return self.stub.AddSfzTrack(pb.AddSfzTrackRequest(name=name, path=path)).id

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

    def remove_track(self, track_id: int) -> None:
        self._ack(self.stub.RemoveTrack(pb.TrackId(id=track_id)))

    def list_tracks(self) -> list[dict]:
        r = self.stub.ListTracks(pb.Empty())
        return [{"id": t.id, "name": t.name, "type": t.type, "volume": t.volume,
                 "pan": t.pan, "mute": t.mute, "clips": t.clips} for t in r.tracks]

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

    def get_automation(self) -> list[dict]:
        r = self.stub.GetAutomation(pb.Empty())
        return [{"target": a.target, "id": a.id, "slot": a.slot, "param": a.param,
                 "points": [(p.beat, p.value) for p in a.points]} for a in r.lanes]

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

    def import_midi(self, path: str) -> None:
        """Load a standard MIDI file as synth tracks + clips."""
        self._ack(self.stub.ImportMidi(pb.FilePath(path=path)))

    def render(self, path: str, tail_seconds: float = 0.0, start_beat: float = 0.0,
               end_beat: float = 0.0, track_id: Optional[int] = None,
               range_name: str = "") -> None:
        kw = {"path": path, "tail_seconds": tail_seconds,
              "start_beat": start_beat, "end_beat": end_beat, "range_name": range_name}
        if track_id is not None:
            kw["track_id"] = track_id
        self._ack(self.stub.RenderToFile(pb.RenderRequest(**kw)))

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
                 "identifier": p.identifier} for p in r.plugins]


@contextmanager
def connect(target: str = "127.0.0.1:50051") -> Iterator[Gloopy]:
    """Context-manager sugar: ``with connect() as g: ...``."""
    g = Gloopy(target)
    try:
        yield g
    finally:
        g.close()
