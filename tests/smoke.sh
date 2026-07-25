#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
# SPDX-License-Identifier: AGPL-3.0-only
#
# Headless smoke test: boot Gloopy, drive it over gRPC to build a one-note
# synth track, render to WAV offline, and assert the render is non-silent.
# Exercises the app end-to-end (transport, tracks, clips, offline bounce).
#
# Usage: tests/smoke.sh [path-to-Gloopy-binary]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${1:-$ROOT/build/Gloopy_artefacts/Release/Gloopy}"
PROTO_DIR="$ROOT/proto"
WORK="$(mktemp -d)"
WAV="$WORK/smoke.wav"
PORT=50051

[ -x "$BIN" ] || { echo "smoke: binary not found: $BIN" >&2; exit 2; }
command -v grpcurl >/dev/null || { echo "smoke: grpcurl not installed" >&2; exit 2; }

# Run under a virtual X server if there's no display (CI).
LAUNCH=("$BIN")
if [ -z "${DISPLAY:-}" ] && command -v xvfb-run >/dev/null; then
    LAUNCH=(xvfb-run -a "$BIN")
fi

pkill -x Gloopy 2>/dev/null || true
sleep 1
"${LAUNCH[@]}" >"$WORK/gloopy.log" 2>&1 &
APP_PID=$!
cleanup() { kill "$APP_PID" 2>/dev/null || true; pkill -x Gloopy 2>/dev/null || true; rm -rf "$WORK"; }
trap cleanup EXIT

g() { grpcurl -plaintext -import-path "$PROTO_DIR" -proto gloopy.proto "$@"; }

# Wait for the gRPC server to accept connections.
for i in $(seq 1 30); do
    if g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetTransport >/dev/null 2>&1; then break; fi
    sleep 0.5
    [ "$i" = 30 ] && { echo "smoke: server never came up" >&2; cat "$WORK/gloopy.log" >&2; exit 1; }
done
echo "smoke: server is up"

TID=$(g -d '{"name":"smoke","wave":"SAW","attack":0.01,"decay":0.1,"sustain":0.8,"release":0.2,"gain":0.9}' \
        127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
echo "smoke: added synth track id=$TID"

# Universal parameter model: the synth track must expose a stable-id cutoff param;
# set it and read it back to prove list/get/set are wired to the live engine.
PCOUNT=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/ListParameters | grep -c '"id"')
g -d "{\"id\":\"track/$TID/synth/cutoff\",\"value\":800}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetParameter >/dev/null
CUT=$(g -d "{\"id\":\"track/$TID/synth/cutoff\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetParameter | grep -o '"value": [0-9.]*' | grep -o '[0-9.]*')
[ "${CUT%.*}" = "800" ] || { echo "smoke: parameter round-trip failed (cutoff='$CUT', $PCOUNT params listed)" >&2; exit 1; }
echo "smoke: PASS — parameter model list/get/set ($PCOUNT params; cutoff=$CUT)"

# Mixer scenes: snapshot the mixer, change a fader, recall, confirm exact restore
# (captures/restores current state, so it leaves the mixer where it found it).
insvol0() { g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/ListInserts | python3 -c "import json,sys;print(json.load(sys.stdin)['inserts'][0].get('volume',0.0))"; }
V0=$(insvol0)
g -d '{"name":"snap"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/DefineMixerScene >/dev/null
g -d '{"index":0,"volume":0.2}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetInsertParams >/dev/null
g -d '{"name":"snap"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/RecallMixerScene >/dev/null
V1=$(insvol0)
python3 -c "import sys;a=float('$V0');b=float('$V1');sys.exit(0 if abs(a-b)<1e-4 else 1)" \
    || { echo "smoke: mixer scene recall did not restore insert 0 volume ($V0 -> mangled 0.2 -> $V1)" >&2; exit 1; }
echo "smoke: PASS — mixer scene snapshot/recall restores fader ($V1)"

# Buses & sends: add a bus, wire an aux send, confirm the routing is reported, then
# clear the send so the mix is untouched for later level assertions.
BUS=$(g -d '{"name":"fxbus"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddBus | grep -o '[0-9]\+' | head -1)
g -d "{\"insert\":1,\"bus\":$BUS,\"level\":0.5}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetSend >/dev/null
g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/ListInserts | python3 -c "
import json,sys
d=json.load(sys.stdin)['inserts']
assert any(i.get('isBus') for i in d), 'no bus reported'
assert any(s.get('bus')==$BUS and abs(s.get('level',0)-0.5)<1e-4 for s in d[1].get('sends',[])), 'send not reported'
print('smoke: PASS — bus + aux send routed (bus index $BUS)')" || { echo "smoke: buses/sends routing not reported" >&2; exit 1; }
g -d "{\"insert\":1,\"bus\":$BUS,\"level\":0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetSend >/dev/null   # clear send (bus left inert)

# Scales: set C major, snap an out-of-scale note (C#=61) into the scale, on a
# scratch track that's removed afterwards so the mix is untouched.
g -d '{"root":0,"name":"major"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetScale >/dev/null
ST=$(g -d '{"name":"scaletest","wave":"SAW"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$ST,\"start_beat\":0,\"length_beats\":1,\"content_len_beats\":1,\"looped\":false,\"notes\":[{\"pitch\":61,\"start_beat\":0,\"length_beats\":1,\"velocity\":0.8}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
g -d "{\"track_id\":$ST,\"index\":0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SnapClipToScale >/dev/null
SP=$(g -d "{\"track_id\":$ST,\"index\":0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetClipNotes | python3 -c "import json,sys;print(json.load(sys.stdin)['notes'][0]['pitch'])")
python3 -c "p=int('$SP');assert p!=61 and p%12 in {0,2,4,5,7,9,11}, 'snap left note out of C major: '+str(p)" \
    || { echo "smoke: snap-to-scale failed (61 -> $SP)" >&2; exit 1; }
echo "smoke: PASS — snap-to-scale moved C# into C major ($SP)"
g -d "{\"id\":$ST}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null
g -d '{"root":0,"name":"chromatic"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetScale >/dev/null   # reset

# Tempo map: exact piecewise beat<->seconds. 8 beats across 120->240 bpm = 3.0s
# (2.0 @120 + 1.0 @240), not 4.0. (Render path not yet tempo-mapped; markers removed after.)
g -d '{"beat":0,"bpm":120}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddTempoMarker >/dev/null
g -d '{"beat":4,"bpm":240}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddTempoMarker >/dev/null
TSEC=$(g -d '{"beats":8}' 127.0.0.1:$PORT gloopy.v1.Gloopy/BeatsToSeconds | python3 -c "import json,sys;print(json.load(sys.stdin).get('seconds',0))")
TBEAT=$(g -d '{"seconds":3.0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SecondsToBeats | python3 -c "import json,sys;print(json.load(sys.stdin).get('beats',0))")
python3 -c "assert abs(float('$TSEC')-3.0)<0.01 and abs(float('$TBEAT')-8.0)<0.01, 'tempo map math wrong (8b=%ss, 3s=%sb)'%('$TSEC','$TBEAT')" \
    || { echo "smoke: tempo map beat/second conversion wrong" >&2; exit 1; }
echo "smoke: PASS — tempo map beats<->seconds (8 beats @120->240 = ${TSEC}s)"
g -d '{"beat":0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTempoMarker >/dev/null
g -d '{"beat":4}' 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTempoMarker >/dev/null

# Controller mapping: a source (cc:20) drives a ParamModel target scaled to [lo,hi].
CT=$(g -d '{"name":"ctltest","wave":"SAW"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"source\":\"cc:20\",\"target\":\"track/$CT/synth/cutoff\",\"lo\":500,\"hi\":5000}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddControllerMap >/dev/null
g -d '{"source":"cc:20","value":1.0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetController >/dev/null
CVAL=$(g -d "{\"id\":\"track/$CT/synth/cutoff\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetParameter | python3 -c "import json,sys;print(json.load(sys.stdin).get('value',0))")
python3 -c "assert abs(float('$CVAL')-5000)<1, 'controller did not drive param (got %s)'%'$CVAL'" \
    || { echo "smoke: controller mapping did not drive the parameter" >&2; exit 1; }
echo "smoke: PASS — controller map cc:20 -> cutoff (=$CVAL at full)"
g -d "{\"id\":$CT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null

# Project notes: set markdown, save+reload a composition, confirm notes.md carries it
# (NewProject in between must clear notes, so a stale value can't pass).
g -d '{"text":"# smoke notes\nTODO-XYZZY"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetProjectNotes >/dev/null
NCOMP="$WORK/notescomp"
g -d "{\"path\":\"$NCOMP\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SaveComposition >/dev/null
grep -q XYZZY "$NCOMP/notes.md" || { echo "smoke: notes.md missing content" >&2; exit 1; }
g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/NewProject >/dev/null
CLEARED=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetProjectNotes | python3 -c "import json,sys;print(len(json.load(sys.stdin).get('text','')))")
[ "$CLEARED" = 0 ] || { echo "smoke: NewProject did not clear project notes" >&2; exit 1; }
g -d "{\"path\":\"$NCOMP\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/LoadComposition >/dev/null
g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetProjectNotes | python3 -c "import json,sys;assert 'XYZZY' in json.load(sys.stdin).get('text',''),'notes lost on reload';print('smoke: PASS — project notes survive composition round-trip')" \
    || { echo "smoke: project notes did not round-trip" >&2; exit 1; }

# Modulation matrix: an LFO on a synth cutoff must change the render vs a static
# cutoff. Dedicated track + two renders, then cleaned up.
MT=$(g -d '{"name":"modtest","wave":"SAW","attack":0.01,"decay":0.1,"sustain":0.9,"release":0.2,"gain":0.8}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$MT,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"looped\":false,\"notes\":[{\"pitch\":72,\"start_beat\":0,\"length_beats\":4,\"velocity\":0.9}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
g -d "{\"id\":\"track/$MT/synth/cutoff\",\"value\":1500}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetParameter >/dev/null
g -d "{\"path\":\"$WORK/mod_base.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"target\":\"track/$MT/synth/cutoff\",\"rate\":6.0,\"depth\":1400,\"center\":1500,\"shape\":0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetModulation >/dev/null
g -d "{\"path\":\"$WORK/mod_lfo.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
python3 -c "
import wave
def rd(p):
    w=wave.open(p);f=w.readframes(w.getnframes())
    return [int.from_bytes(f[i:i+3],'little',signed=True)/(1<<23) for i in range(0,len(f),3)]
a=rd('$WORK/mod_base.wav');b=rd('$WORK/mod_lfo.wav');m=min(len(a),len(b))
d=sum(abs(a[i]-b[i]) for i in range(m))/max(1,m)
assert d>0.003,'modulation did not change the render (diff=%.5f)'%d
print('smoke: PASS — cutoff LFO changes the render (mean abs diff %.4f)'%d)
" || { echo "smoke: modulation had no audible effect" >&2; exit 1; }
# Tempo-synced LFO (sync_beats>0): still modulates the render, and ListModulations
# reports the sync length. The sync-vs-free phase math is unit-tested (GloopyTests::Lfo).
g -d "{\"target\":\"track/$MT/synth/cutoff\",\"depth\":1400,\"center\":1500,\"shape\":0,\"sync_beats\":1}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetModulation >/dev/null
g -d "{\"path\":\"$WORK/mod_sync.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
SYNCB=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/ListModulations | python3 -c "import json,sys;m=json.load(sys.stdin).get('mods',[]);print(m[0].get('syncBeats',0) if m else 0)")
python3 -c "
import wave
def rd(p):
    w=wave.open(p);f=w.readframes(w.getnframes());return [int.from_bytes(f[i:i+3],'little',signed=True) for i in range(0,len(f),3)]
a=rd('$WORK/mod_base.wav');b=rd('$WORK/mod_sync.wav');m=min(len(a),len(b))
d=sum(abs(a[i]-b[i]) for i in range(m))/m/(1<<23)
assert d>0.003, 'tempo-synced LFO did not change the render (diff=%.5f)'%d
assert abs(float('$SYNCB')-1.0)<1e-4, 'ListModulations sync_beats not 1 (got $SYNCB)'
print('smoke: PASS — tempo-synced LFO modulates render (diff %.4f), sync_beats=$SYNCB round-trips'%d)
" || { echo "smoke: tempo-synced modulation failed" >&2; exit 1; }
g -d "{\"target\":\"track/$MT/synth/cutoff\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveModulation >/dev/null
g -d "{\"id\":$MT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null

# Live arpeggiator: a held chord renders differently with the arp on, and GetTrackArp
# round-trips. Dedicated track, two renders (chord vs arp), cleaned up.
AT=$(g -d '{"name":"arptest","wave":"SAW","attack":0.005,"decay":0.05,"sustain":0.9,"release":0.05,"gain":0.8}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$AT,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"looped\":false,\"notes\":[{\"pitch\":60,\"start_beat\":0,\"length_beats\":4,\"velocity\":0.9},{\"pitch\":64,\"start_beat\":0,\"length_beats\":4,\"velocity\":0.9},{\"pitch\":67,\"start_beat\":0,\"length_beats\":4,\"velocity\":0.9}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
g -d "{\"path\":\"$WORK/arp_chord.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"track_id\":$AT,\"enabled\":true,\"rate\":0.25,\"octaves\":1,\"gate\":0.5,\"mode\":0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetTrackArp >/dev/null
ARPON=$(g -d "{\"track_id\":$AT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetTrackArp | python3 -c "import json,sys;print(json.load(sys.stdin).get('enabled',False))")
g -d "{\"path\":\"$WORK/arp_on.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
python3 -c "
import wave
def rd(p):
    w=wave.open(p);f=w.readframes(w.getnframes())
    return [int.from_bytes(f[i:i+3],'little',signed=True)/(1<<23) for i in range(0,len(f),3)]
assert '$ARPON'=='True','GetTrackArp did not report enabled'
a=rd('$WORK/arp_chord.wav');b=rd('$WORK/arp_on.wav');m=min(len(a),len(b))
d=sum(abs(a[i]-b[i]) for i in range(m))/max(1,m)
assert d>0.003,'live arp did not change the render (diff=%.5f)'%d
print('smoke: PASS — live arpeggiator changes the render (mean abs diff %.4f)'%d)
" || { echo "smoke: live arp had no audible effect" >&2; exit 1; }
g -d "{\"id\":$AT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null

g -d "{\"track_id\":$TID,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"looped\":false,\
\"notes\":[{\"pitch\":60,\"start_beat\":0,\"length_beats\":1,\"velocity\":0.9},\
{\"pitch\":64,\"start_beat\":1,\"length_beats\":1,\"velocity\":0.9},\
{\"pitch\":67,\"start_beat\":2,\"length_beats\":2,\"velocity\":0.9}]}" \
    127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
echo "smoke: added clip"

# Clip/region operations on a dedicated track: split distributes notes, reverse
# mirrors them, duplicate copies — verified precisely via GetClipNotes.
CO=$(g -d '{"name":"clipops","wave":"SAW"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$CO,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"looped\":false,\
\"notes\":[{\"pitch\":60,\"start_beat\":0,\"length_beats\":1,\"velocity\":0.8},\
{\"pitch\":61,\"start_beat\":1,\"length_beats\":1,\"velocity\":0.8},\
{\"pitch\":62,\"start_beat\":2,\"length_beats\":1,\"velocity\":0.8},\
{\"pitch\":63,\"start_beat\":3,\"length_beats\":1,\"velocity\":0.8}]}" \
    127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
ncount() { g -d "{\"track_id\":$CO,\"index\":$1}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetClipNotes | grep -c '"pitch"'; }
g -d "{\"track_id\":$CO,\"index\":0,\"beat\":2}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SplitClip >/dev/null
[ "$(ncount 0)" = 2 ] && [ "$(ncount 1)" = 2 ] || { echo "smoke: SplitClip did not distribute notes 2/2 (got $(ncount 0)/$(ncount 1))" >&2; exit 1; }
echo "smoke: PASS — SplitClip distributed notes (2 + 2)"
# reverse the left clip: pitch 60 (start 0) must move to start 1 within the 2-beat span
g -d "{\"track_id\":$CO,\"index\":0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/ReverseClip >/dev/null
S60=$(g -d "{\"track_id\":$CO,\"index\":0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetClipNotes | python3 -c "import json,sys;s=next(n.get('startBeat',0.0) for n in json.load(sys.stdin)['notes'] if n['pitch']==60);print('ok' if abs(s-1.0)<1e-6 else 'bad:%s'%s)")
[ "$S60" = "ok" ] || { echo "smoke: ReverseClip wrong (pitch 60 -> $S60, expected beat 1.0)" >&2; exit 1; }
echo "smoke: PASS — ReverseClip mirrored notes (pitch 60 -> beat 1)"
g -d "{\"track_id\":$CO,\"index\":1,\"at_beat\":-1}" 127.0.0.1:$PORT gloopy.v1.Gloopy/DuplicateClip >/dev/null
CLIPN=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetState | python3 -c "import json,sys;print(next(t['clips'] for t in json.load(sys.stdin)['tracks'] if t.get('name')=='clipops'))")
[ "$CLIPN" = 3 ] || { echo "smoke: DuplicateClip wrong clip count ($CLIPN, expected 3)" >&2; exit 1; }
echo "smoke: PASS — DuplicateClip (clipops track now has 3 clips)"
# Piano-roll note ops (quantize/transpose) on an off-grid clip, verified via GetClipNotes.
g -d "{\"track_id\":$CO,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"looped\":false,\
\"notes\":[{\"pitch\":60,\"start_beat\":0.1,\"length_beats\":1,\"velocity\":0.8},\
{\"pitch\":64,\"start_beat\":0.6,\"length_beats\":1,\"velocity\":0.8}]}" \
    127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null   # appended as a new clip index
NQ=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetState | python3 -c "import json,sys;print(next(t['clips'] for t in json.load(sys.stdin)['tracks'] if t.get('name')=='clipops')-1)")
g -d "{\"track_id\":$CO,\"index\":$NQ,\"grid\":0.25}" 127.0.0.1:$PORT gloopy.v1.Gloopy/QuantizeClip >/dev/null
g -d "{\"track_id\":$CO,\"index\":$NQ,\"semitones\":12}" 127.0.0.1:$PORT gloopy.v1.Gloopy/TransposeClip >/dev/null
g -d "{\"track_id\":$CO,\"index\":$NQ}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetClipNotes | python3 -c "
import json,sys
d=json.load(sys.stdin)['notes']
got=sorted((n['pitch'],round(n.get('startBeat',0.0),3)) for n in d)
assert got==[(72,0.0),(76,0.5)], 'quantize/transpose wrong: '+str(got)
print('smoke: PASS — quantize+transpose note ops (%s)'%got)
" || { echo "smoke: piano-roll note ops wrong" >&2; exit 1; }
# Chord stamp: an empty clip + AddChord (Cmin7) yields the right 4-note voicing.
g -d "{\"track_id\":$CO,\"start_beat\":0,\"length_beats\":2,\"content_len_beats\":2,\"looped\":false,\"notes\":[]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
CC=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetState | python3 -c "import json,sys;print(next(t['clips'] for t in json.load(sys.stdin)['tracks'] if t.get('name')=='clipops')-1)")
g -d "{\"track_id\":$CO,\"index\":$CC,\"root\":60,\"type\":\"min7\",\"start_beat\":0,\"length_beats\":1,\"velocity\":0.8}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddChord >/dev/null
g -d "{\"track_id\":$CO,\"index\":$CC}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetClipNotes | python3 -c "
import json,sys
p=sorted(n['pitch'] for n in json.load(sys.stdin)['notes'])
assert p==[60,63,67,70], 'Cmin7 voicing wrong: '+str(p)
print('smoke: PASS — AddChord Cmin7 voicing [60,63,67,70]')
" || { echo "smoke: chord stamp wrong" >&2; exit 1; }
# Strum: fan out the Cmin7 voicing — down-strum staggers starts high->low by step.
g -d "{\"track_id\":$CO,\"index\":$CC,\"step_beats\":0.1,\"down\":true}" 127.0.0.1:$PORT gloopy.v1.Gloopy/StrumClip >/dev/null
g -d "{\"track_id\":$CO,\"index\":$CC}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetClipNotes | python3 -c "
import json,sys
ns=sorted(json.load(sys.stdin)['notes'], key=lambda n:-n['pitch'])   # high->low
starts=[round(n.get('startBeat',0),3) for n in ns]
assert starts==[0.0,0.1,0.2,0.3], 'strum starts wrong: '+str(starts)
print('smoke: PASS — StrumClip fanned Cmin7 starts to '+str(starts))
" || { echo "smoke: strum wrong" >&2; exit 1; }
# Arpeggiate: a fresh Cmaj triad -> up arp at 0.25 spacing = 3 sequential notes.
g -d "{\"track_id\":$CO,\"start_beat\":0,\"length_beats\":2,\"content_len_beats\":2,\"looped\":false,\"notes\":[]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
AC=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetState | python3 -c "import json,sys;print(next(t['clips'] for t in json.load(sys.stdin)['tracks'] if t.get('name')=='clipops')-1)")
g -d "{\"track_id\":$CO,\"index\":$AC,\"root\":60,\"type\":\"maj\",\"start_beat\":0,\"length_beats\":2,\"velocity\":0.8}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddChord >/dev/null
g -d "{\"track_id\":$CO,\"index\":$AC,\"step_beats\":0.25,\"mode\":0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/ArpeggiateClip >/dev/null
g -d "{\"track_id\":$CO,\"index\":$AC}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetClipNotes | python3 -c "
import json,sys
ns=sorted(json.load(sys.stdin)['notes'], key=lambda n:n.get('startBeat',0))
seq=[(n['pitch'],round(n.get('startBeat',0),3)) for n in ns]
assert seq==[(60,0.0),(64,0.25),(67,0.5)], 'arp seq wrong: '+str(seq)
print('smoke: PASS — ArpeggiateClip up -> '+str(seq))
" || { echo "smoke: arpeggiate wrong" >&2; exit 1; }
g -d "{\"id\":$CO}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null   # isolate: drop the scratch track

g -d "{\"path\":\"$WAV\",\"tail_seconds\":1.0,\"start_beat\":0,\"end_beat\":4}" \
    127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
echo "smoke: rendered $WAV"

[ -s "$WAV" ] || { echo "smoke: render produced no file" >&2; exit 1; }

# Assert the render is audibly non-silent (peak well above the noise floor).
python3 - "$WAV" <<'PY'
import sys, wave, struct
w = wave.open(sys.argv[1], 'rb')
n = w.getnframes(); ch = w.getnchannels(); sw = w.getsampwidth()
frames = w.readframes(min(n, w.getframerate()*6))
if sw == 2:
    vals = struct.unpack("<%dh" % (len(frames)//2), frames); peak = max(abs(v) for v in vals)/32768.0
elif sw == 3:
    peak = 0.0
    for i in range(0, len(frames), 3):
        b = frames[i:i+3]; v = int.from_bytes(b, 'little', signed=True); peak = max(peak, abs(v)/8388608.0)
else:
    vals = struct.unpack("<%di" % (len(frames)//4), frames); peak = max(abs(v) for v in vals)/2147483648.0
print(f"smoke: frames={n} ch={ch} bytes/sample={sw} peak={peak:.3f}")
assert peak > 0.02, f"render is essentially silent (peak={peak:.4f})"
print("smoke: PASS — render is non-silent")
PY

# Built-in effects (on master, removed after): bitcrusher quantizes the signal to a
# handful of distinct sample values; compressor with makeup 0 ducks over-threshold peaks.
distinct() { python3 -c "import wave,sys;w=wave.open(sys.argv[1]);f=w.readframes(w.getnframes());print(len(set(f[i:i+3] for i in range(0,len(f),3))))" "$1"; }
apeak() { g -d "{\"path\":\"$1\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AnalyzeFile | python3 -c "import json,sys;print(json.load(sys.stdin).get('peakDbfs',0.0))"; }
BASE_DISTINCT=$(distinct "$WAV")
g -d '{"insert":0,"type":"BITCRUSHER"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddEffect >/dev/null
g -d '{"insert":0,"slot":0,"name":"Bits","value":3}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d '{"insert":0,"slot":0,"name":"Mix","value":1.0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d "{\"path\":\"$WORK/bc.wav\",\"tail_seconds\":1.0,\"start_beat\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
BC_DISTINCT=$(distinct "$WORK/bc.wav")
[ "$BC_DISTINCT" -lt $((BASE_DISTINCT / 10)) ] || { echo "smoke: bitcrusher did not quantize ($BC_DISTINCT vs $BASE_DISTINCT)" >&2; exit 1; }
echo "smoke: PASS — bitcrusher quantized signal ($BC_DISTINCT distinct vs $BASE_DISTINCT)"
g -d '{"insert":0,"slot":0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveEffect >/dev/null
BASE_PEAK=$(apeak "$WAV")
g -d '{"insert":0,"type":"COMPRESSOR"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddEffect >/dev/null
g -d '{"insert":0,"slot":0,"name":"Thresh dB","value":-30}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d '{"insert":0,"slot":0,"name":"Ratio","value":10}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d '{"insert":0,"slot":0,"name":"Attack ms","value":0.1}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d "{\"path\":\"$WORK/comp.wav\",\"tail_seconds\":1.0,\"start_beat\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
COMP_PEAK=$(apeak "$WORK/comp.wav")
python3 -c "assert float('$COMP_PEAK') < float('$BASE_PEAK')-3.0, 'compressor did not duck peak (%s -> %s)'%('$BASE_PEAK','$COMP_PEAK')" \
    || { echo "smoke: compressor did not reduce peak" >&2; exit 1; }
echo "smoke: PASS — compressor ducked peak ($BASE_PEAK -> $COMP_PEAK dBFS)"
g -d '{"insert":0,"slot":0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveEffect >/dev/null
arms() { g -d "{\"path\":\"$1\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AnalyzeFile | python3 -c "import json,sys;print(json.load(sys.stdin).get('rmsDbfs',-99.0))"; }
# EQ: a wide boost vs cut at a band shifts RMS.
g -d '{"insert":0,"type":"EQ"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddEffect >/dev/null
g -d '{"insert":0,"slot":0,"name":"Freq","value":500}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d '{"insert":0,"slot":0,"name":"Q","value":0.7}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d '{"insert":0,"slot":0,"name":"Gain dB","value":18}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d "{\"path\":\"$WORK/eqb.wav\",\"tail_seconds\":0.5,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
EQB=$(arms "$WORK/eqb.wav")
g -d '{"insert":0,"slot":0,"name":"Gain dB","value":-18}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d "{\"path\":\"$WORK/eqc.wav\",\"tail_seconds\":0.5,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
EQC=$(arms "$WORK/eqc.wav")
python3 -c "assert float('$EQB') > float('$EQC')+3, 'EQ boost/cut did not shift RMS (%s vs %s)'%('$EQB','$EQC')" \
    || { echo "smoke: EQ did not shift band energy" >&2; exit 1; }
echo "smoke: PASS — EQ boost vs cut shifted RMS ($EQB vs $EQC dBFS)"
g -d '{"insert":0,"slot":0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveEffect >/dev/null
# Waveshaper: drive raises RMS (soft-clip saturation).
BASE_RMS=$(arms "$WAV")
g -d '{"insert":0,"type":"WAVESHAPER"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddEffect >/dev/null
g -d '{"insert":0,"slot":0,"name":"Drive","value":25}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d "{\"path\":\"$WORK/ws.wav\",\"tail_seconds\":0.5,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
WS=$(arms "$WORK/ws.wav")
python3 -c "assert float('$WS') > float('$BASE_RMS')+2, 'waveshaper drive did not raise RMS (%s -> %s)'%('$BASE_RMS','$WS')" \
    || { echo "smoke: waveshaper had no effect" >&2; exit 1; }
echo "smoke: PASS — waveshaper drive raised RMS ($BASE_RMS -> $WS dBFS)"
g -d '{"insert":0,"slot":0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveEffect >/dev/null

# Stereo Widener: prove the STEREO_WIDENER enum -> factory -> params wiring and a clean
# render (the mid/side DSP itself is unit-tested in GloopyTests::StereoWidener).
g -d '{"insert":0,"type":"STEREO_WIDENER"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddEffect >/dev/null
g -d '{"insert":0,"slot":0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetEffectParams | grep -q '"Width"' \
    || { echo "smoke: Stereo Widener has no Width param" >&2; exit 1; }
g -d '{"insert":0,"slot":0,"name":"Width","value":2}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d "{\"path\":\"$WORK/wide.wav\",\"tail_seconds\":0.5,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
WIDE=$(arms "$WORK/wide.wav")
python3 -c "assert float('$WIDE') > -60, 'stereo widener render is silent (%s dBFS)'%'$WIDE'" \
    || { echo "smoke: stereo widener produced silence" >&2; exit 1; }
echo "smoke: PASS — Stereo Widener wired (Width param, renders $WIDE dBFS)"
g -d '{"insert":0,"slot":0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveEffect >/dev/null

# Chorus: Mix=0 must be a bit-exact passthrough of the dry render; Mix>0 must audibly
# differ (the modulated delay is active). Proves the CHORUS enum -> factory + Mix param.
g -d "{\"path\":\"$WORK/chdry.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d '{"insert":0,"type":"CHORUS"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddEffect >/dev/null
g -d '{"insert":0,"slot":0,"name":"Mix","value":0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d "{\"path\":\"$WORK/ch0.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d '{"insert":0,"slot":0,"name":"Mix","value":0.8}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d "{\"path\":\"$WORK/chw.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
python3 - "$WORK/chdry.wav" "$WORK/ch0.wav" "$WORK/chw.wav" <<'PY'
import sys, wave
def s(p):
    w=wave.open(p); f=w.readframes(w.getnframes())
    return [int.from_bytes(f[i:i+3],'little',signed=True) for i in range(0,len(f),3)]
dry,c0,cw=map(s,sys.argv[1:4])
n=min(len(dry),len(c0),len(cw))
d0=sum(abs(dry[i]-c0[i]) for i in range(n))/n/(1<<23)
dw=sum(abs(dry[i]-cw[i]) for i in range(n))/n/(1<<23)
assert d0 < 1e-6, 'chorus Mix=0 is not a passthrough (mean|diff|=%.2e)'%d0
assert dw > 1e-3, 'chorus Mix=0.8 did not change the signal (mean|diff|=%.2e)'%dw
print('smoke: PASS — Chorus (Mix=0 passthrough %.1e, Mix=0.8 active %.3f)'%(d0,dw))
PY
g -d '{"insert":0,"slot":0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveEffect >/dev/null

# Timeline locations: add a named range, prove render-by-range is shorter than the
# full render, and (below) that the location survives the composition round-trip.
g -d '{"name":"half","kind":"range","start_beat":0,"end_beat":2}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddLocation >/dev/null
# Render full (0..4) and the named range (0..2) with identical tail, so only the
# range differs — the range render must be strictly shorter.
g -d "{\"path\":\"$WORK/full0.wav\",\"tail_seconds\":0,\"start_beat\":0,\"end_beat\":4}" \
    127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"path\":\"$WORK/half.wav\",\"tail_seconds\":0,\"range_name\":\"half\"}" \
    127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
FULLF=$(python3 -c "import wave;print(wave.open('$WORK/full0.wav').getnframes())")
HALFF=$(python3 -c "import wave;print(wave.open('$WORK/half.wav').getnframes())")
[ "$HALFF" -lt "$FULLF" ] || { echo "smoke: render-by-range not shorter (half=$HALFF full=$FULLF)" >&2; exit 1; }
echo "smoke: PASS — render-by-range 'half' shorter than full ($HALFF < $FULLF frames)"

# Tempo map: a mid-song speed-up (120->240 bpm at beat 2) must shorten a fixed 0..4
# beat render, because beat->sample now integrates the tempo map. Anchor 120 at beat 0
# so beats 0..2 stay slow — a lone late marker would (by the before-first-marker rule)
# speed up the whole song. full0.wav above is the constant-tempo baseline.
g -d '{"beat":0,"bpm":120}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddTempoMarker >/dev/null
g -d '{"beat":2,"bpm":240}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddTempoMarker >/dev/null
g -d "{\"path\":\"$WORK/tvary.wav\",\"tail_seconds\":0,\"start_beat\":0,\"end_beat\":4}" \
    127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
TVARYF=$(python3 -c "import wave;print(wave.open('$WORK/tvary.wav').getnframes())")
HALFFULL=$(( FULLF / 2 ))
[ "$TVARYF" -lt "$FULLF" ] || { echo "smoke: mid-song tempo change did not shorten render (vary=$TVARYF const=$FULLF)" >&2; exit 1; }
[ "$TVARYF" -gt "$HALFFULL" ] || { echo "smoke: tempo change over-shortened; pre-marker beats not preserved (vary=$TVARYF half=$HALFFULL)" >&2; exit 1; }
echo "smoke: PASS — mid-song tempo speed-up shortened render ($HALFFULL < $TVARYF < $FULLF frames)"
# Restore the empty-map default so downstream composition assertions are unaffected.
g -d '{"beat":0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTempoMarker >/dev/null
g -d '{"beat":2}' 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTempoMarker >/dev/null

# Export profiles: a named 'master' mix target renders a deterministic file, and the
# profile survives the composition round-trip (checked after the reload below).
g -d '{"name":"master","target":"mix","format":"wav"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/DefineExportProfile >/dev/null
EXPFILE=$(g -d "{\"name\":\"master\",\"out_dir\":\"$WORK/exp\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RunExport | grep -o '"[^"]*master.wav"' | tr -d '"' | head -1)
[ -s "$EXPFILE" ] || { echo "smoke: export profile produced no file" >&2; exit 1; }
python3 -c "import wave;assert wave.open('$EXPFILE').getnframes()>1000, 'export too short'"
echo "smoke: PASS — export profile 'master' rendered $(basename "$EXPFILE")"

# FLAC export: the same mix as a lossless FLAC. Proves apiRenderToFile picks the
# encoder from the output extension and the profile's format threads through.
g -d '{"name":"archive","target":"mix","format":"flac"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/DefineExportProfile >/dev/null
FLACFILE=$(g -d "{\"name\":\"archive\",\"out_dir\":\"$WORK/exp\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RunExport | grep -o '"[^"]*archive.flac"' | tr -d '"' | head -1)
[ -s "$FLACFILE" ] || { echo "smoke: FLAC export produced no file" >&2; exit 1; }
[ "$(head -c 4 "$FLACFILE")" = "fLaC" ] || { echo "smoke: FLAC export lacks fLaC magic" >&2; exit 1; }
[ "$(stat -c%s "$FLACFILE")" -lt "$(stat -c%s "$EXPFILE")" ] || { echo "smoke: FLAC not smaller than the WAV mix" >&2; exit 1; }
if command -v ffprobe >/dev/null; then
    CODEC=$(ffprobe -v error -select_streams a:0 -show_entries stream=codec_name -of default=nk=1:nw=1 "$FLACFILE")
    [ "$CODEC" = "flac" ] || { echo "smoke: ffprobe says codec=$CODEC, not flac" >&2; exit 1; }
fi
echo "smoke: PASS — FLAC export ($(stat -c%s "$FLACFILE") < $(stat -c%s "$EXPFILE") bytes, decodes as flac)"

# Composition (directory) round-trip: save the current project as a composition,
# reload it, and re-render — the reloaded render must also be non-silent.
COMP="$WORK/comp"
RT="$WORK/rt.wav"
g -d "{\"path\":\"$COMP\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SaveComposition >/dev/null
[ -f "$COMP/gloopy.toml" ] || { echo "smoke: composition save produced no gloopy.toml" >&2; exit 1; }
echo "smoke: saved composition ($(find "$COMP" -type f | wc -l) files)"
g -d "{\"path\":\"$COMP\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/LoadComposition >/dev/null
LOC=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/ListLocations | grep -c '"name"')
[ "$LOC" -ge 1 ] || { echo "smoke: timeline location did not survive composition round-trip" >&2; exit 1; }
echo "smoke: PASS — timeline location survived composition round-trip ($LOC found)"
EXP=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/ListExportProfiles | grep -c '"name"')
[ "$EXP" -ge 1 ] || { echo "smoke: export profile did not survive composition round-trip" >&2; exit 1; }
echo "smoke: PASS — export profile survived composition round-trip ($EXP found)"
g -d "{\"path\":\"$RT\",\"tail_seconds\":1.0,\"start_beat\":0,\"end_beat\":4}" \
    127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
python3 - "$RT" <<'PY'
import sys, wave
w = wave.open(sys.argv[1], 'rb'); n = w.getnframes(); sw = w.getsampwidth(); ch = w.getnchannels()
frames = w.readframes(min(n, w.getframerate()))
peak = 0.0
for i in range(0, len(frames), sw*ch):
    peak = max(peak, abs(int.from_bytes(frames[i:i+sw], 'little', signed=True)) / float(1 << (8*sw-1)))
assert peak > 0.02, f"composition round-trip render is silent (peak={peak:.4f})"
print(f"smoke: PASS — composition round-trip non-silent (peak={peak:.3f})")
PY

# Phase 2: dirty-file tracking — a re-save with no model change must write 0 files,
# and round-trip must be byte-stable (dir -> runtime -> same dir writes nothing).
before=$(find "$COMP" -type f -newer "$COMP/gloopy.toml" | wc -l)
g -d "{\"path\":\"$COMP\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SaveComposition >/dev/null   # after a load
snap1=$(find "$COMP" -type f -printf '%p %s %T@\n' | sort | md5sum)
g -d "{\"path\":\"$COMP\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SaveComposition >/dev/null   # no-op re-save
snap2=$(find "$COMP" -type f -printf '%p %s %T@\n' | sort | md5sum)
[ "$snap1" = "$snap2" ] || { echo "smoke: no-op re-save rewrote files (dirty tracking broken)" >&2; exit 1; }
echo "smoke: PASS — no-op composition re-save is a true no-op (dirty tracking)"

# Zip read: zip the composition and load the archive.
( cd "$COMP" && command -v zip >/dev/null && zip -qr "$WORK/comp.zip" . ) || echo "smoke: (zip cli absent, skipping zip test)"
if [ -f "$WORK/comp.zip" ]; then
    g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/NewProject >/dev/null
    ok=$(g -d "{\"path\":\"$WORK/comp.zip\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/LoadComposition | grep -o 'true\|false' | head -1)
    [ "$ok" = "true" ] && echo "smoke: PASS — zip composition loads" || { echo "smoke: zip load failed" >&2; exit 1; }
fi

# Headless CLI utilities: inspect (clean JSON on stdout), validate (exit 0 clean),
# pack (produces a loadable zip). Runs as a separate headless process — no port
# bind — so it coexists with the server instance above.
"$BIN" inspect "$COMP" 2>/dev/null | python3 -c "import json,sys; d=json.load(sys.stdin); assert d['tracks'], 'no tracks'; print(f\"smoke: PASS — CLI inspect ({len(d['tracks'])} tracks, {len(d['exports'])} exports)\")" \
    || { echo "smoke: CLI inspect did not emit clean JSON" >&2; exit 1; }
"$BIN" validate "$COMP" >/dev/null 2>&1 && echo "smoke: PASS — CLI validate (clean project, exit 0)" \
    || { echo "smoke: CLI validate failed on a clean project" >&2; exit 1; }
# validate --loudness renders the mix and adds a loudness section (stdout stays pure JSON).
"$BIN" validate "$COMP" --loudness 2>/dev/null | python3 -c "
import json,sys
d=json.load(sys.stdin)
l=d.get('loudness')
assert l and 'lufs' in l and 'true_peak_dbtp' in l, 'validate --loudness has no loudness report'
assert l['peak_dbfs'] > -60, 'validate --loudness rendered silence (%.1f dBFS)'%l['peak_dbfs']
print('smoke: PASS — CLI validate --loudness (%.1f LUFS, tp %.1f dBTP)'%(l['lufs'], l['true_peak_dbtp']))
" || { echo "smoke: CLI validate --loudness did not emit a loudness report" >&2; exit 1; }
"$BIN" pack "$COMP" "$WORK/packed.zip" >/dev/null 2>&1
[ -s "$WORK/packed.zip" ] || { echo "smoke: CLI pack produced no zip" >&2; exit 1; }
g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/NewProject >/dev/null
packok=$(g -d "{\"path\":\"$WORK/packed.zip\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/LoadComposition | grep -o 'true\|false' | head -1)
[ "$packok" = "true" ] && echo "smoke: PASS — CLI pack zip loads" || { echo "smoke: CLI pack zip did not load" >&2; exit 1; }
# Plugin scan cache: `gloopy scan` emits a valid JSON array (empty is fine) with
# enriched metadata fields on each entry.
"$BIN" scan 2>/dev/null | python3 -c "
import json,sys
d=json.load(sys.stdin)
assert isinstance(d,list), 'scan did not emit a JSON array'
for p in d: assert 'vendor' in p and 'category' in p and 'num_outputs' in p, 'missing enriched fields'
print('smoke: PASS — CLI scan emitted %d cached plugins as JSON'%len(d))
" || { echo "smoke: CLI scan did not emit a valid JSON array" >&2; exit 1; }

# MIDI file export/import round-trip (last — import resets the project). Export the
# loaded project to an SMF, reimport into a fresh project, and confirm notes survive
# as playable clips.
g -d "{\"path\":\"$WORK/rt.mid\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/ExportMidi >/dev/null
[ "$(head -c 4 "$WORK/rt.mid")" = "MThd" ] || { echo "smoke: MIDI export is not an SMF" >&2; exit 1; }
echo "smoke: PASS — MIDI export wrote a standard MIDI file"
g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/NewProject >/dev/null
g -d "{\"path\":\"$WORK/rt.mid\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/ImportMidi >/dev/null
CLIPS=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetState | python3 -c "import json,sys; print(sum(t['clips'] for t in json.load(sys.stdin)['tracks']))")
[ "$CLIPS" -ge 1 ] || { echo "smoke: MIDI import produced no clips" >&2; exit 1; }
g -d "{\"path\":\"$WORK/rt2.wav\",\"tail_seconds\":0.5}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
python3 -c "import wave;w=wave.open('$WORK/rt2.wav');n=w.getnframes();f=w.readframes(n);pk=max(abs(int.from_bytes(f[i:i+3],'little',signed=True))/(1<<23) for i in range(0,len(f),3));assert pk>0.02,'imported MIDI renders silent'"
echo "smoke: PASS — MIDI import round-trip (clips=$CLIPS, renders non-silent)"

# Audio import (ImportAudio): load the WAV just rendered as a new audio track. Audio
# import was GUI-only before; this exercises the RPC that the + Audio button now shares.
TB=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetState | python3 -c "import json,sys;print(len(json.load(sys.stdin)['tracks']))")
g -d "{\"path\":\"$WORK/rt2.wav\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/ImportAudio >/dev/null
read -r NA TYPE <<< "$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetState | python3 -c "import json,sys;ts=json.load(sys.stdin)['tracks'];print(len(ts), ts[-1].get('type'))")"
{ [ "$NA" -eq "$((TB+1))" ] && [ "$TYPE" = "audio" ]; } || { echo "smoke: ImportAudio did not add an audio track (before=$TB after=$NA type=$TYPE)" >&2; exit 1; }
# A non-audio file must be rejected cleanly (Ack.ok=false), not add a track.
BAD=$(g -d "{\"path\":\"$WORK/rt.mid\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/ImportAudio | python3 -c "import json,sys;print(json.load(sys.stdin).get('ok', False))")
[ "$BAD" = "False" ] || { echo "smoke: ImportAudio accepted a non-audio file" >&2; exit 1; }
echo "smoke: PASS — ImportAudio added an audio track ($TB -> $NA), rejected a non-audio file"

# Clip gain + normalize (audio clips): normalize sets clip gain so the clip's own peak
# hits the target; the render goes through the mixer insert (a constant attenuation), so
# we assert on level *deltas* (insert loss cancels): two normalize targets 12 dB apart
# must yield rendered peaks 12 dB apart, and a -6 dB SetClipGain must drop the peak 6 dB.
AID=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetState | python3 -c "import json,sys;ts=json.load(sys.stdin)['tracks'];print([t.get('id',0) for t in ts if t.get('type')=='audio'][-1])")
g -d "{\"track_id\":$AID,\"index\":0,\"target_dbfs\":-6}" 127.0.0.1:$PORT gloopy.v1.Gloopy/NormalizeClip >/dev/null
g -d "{\"path\":\"$WORK/nrm6.wav\",\"tail_seconds\":0,\"track_id\":$AID}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"track_id\":$AID,\"index\":0,\"target_dbfs\":-18}" 127.0.0.1:$PORT gloopy.v1.Gloopy/NormalizeClip >/dev/null
g -d "{\"path\":\"$WORK/nrm18.wav\",\"tail_seconds\":0,\"track_id\":$AID}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
python3 -c "
p6,p18=float('$(apeak "$WORK/nrm6.wav")'),float('$(apeak "$WORK/nrm18.wav")')
assert abs((p6-p18)-12) < 1.0, 'normalize -6 vs -18 delta not ~12 dB (%.2f vs %.2f)'%(p6,p18)
print('smoke: PASS — NormalizeClip scales to target (-6 vs -18 -> %.1f dB apart)'%(p6-p18))
" || { echo "smoke: NormalizeClip did not scale to target" >&2; exit 1; }
g -d "{\"track_id\":$AID,\"index\":0,\"gain_db\":0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetClipGain >/dev/null
g -d "{\"path\":\"$WORK/g0.wav\",\"tail_seconds\":0,\"track_id\":$AID}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"track_id\":$AID,\"index\":0,\"gain_db\":-6}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetClipGain >/dev/null
g -d "{\"path\":\"$WORK/gm6.wav\",\"tail_seconds\":0,\"track_id\":$AID}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
python3 -c "
a,b=float('$(apeak "$WORK/g0.wav")'),float('$(apeak "$WORK/gm6.wav")')
assert abs((a-b)-6) < 1.0, 'SetClipGain -6 dB did not drop peak ~6 dB (%.2f -> %.2f)'%(a,b)
print('smoke: PASS — SetClipGain -6 dB dropped peak %.1f -> %.1f dBFS'%(a,b))
" || { echo "smoke: SetClipGain did not drop the level" >&2; exit 1; }

# Clip fades: a fade-in ramps the clip from silence, so the first 0.25 s is much quieter
# than the same clip with no fade (same audio content, isolating the fade). Reset gain to
# unity first so the comparison is about the fade alone.
g -d "{\"track_id\":$AID,\"index\":0,\"gain_db\":0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetClipGain >/dev/null
firstrms() { python3 -c "import wave,sys,math;w=wave.open(sys.argv[1]);n=min(w.getnframes(),int(0.25*w.getframerate()));f=w.readframes(n);v=[int.from_bytes(f[i:i+3],'little',signed=True)/(1<<23) for i in range(0,len(f),3)];r=(sum(x*x for x in v)/max(1,len(v)))**0.5;print(round(20*math.log10(r+1e-12),2))" "$1"; }
g -d "{\"track_id\":$AID,\"index\":0,\"fade_in_beats\":0,\"fade_out_beats\":0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetClipFades >/dev/null
g -d "{\"path\":\"$WORK/nofade.wav\",\"tail_seconds\":0,\"track_id\":$AID}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"track_id\":$AID,\"index\":0,\"fade_in_beats\":2}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetClipFades >/dev/null
g -d "{\"path\":\"$WORK/fadein.wav\",\"tail_seconds\":0,\"track_id\":$AID}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
python3 -c "
a,b=float('$(firstrms "$WORK/nofade.wav")'),float('$(firstrms "$WORK/fadein.wav")')
assert b < a - 6, 'fade-in did not attenuate the clip start (nofade %.1f vs fade %.1f dB)'%(a,b)
print('smoke: PASS — clip fade-in attenuates the start (%.1f -> %.1f dB, first 0.25 s)'%(a,b))
" || { echo "smoke: clip fade-in had no effect" >&2; exit 1; }

# Offline loudness: analyze the earlier synth render; sanity-check the metrics and
# that the headless `analyze` CLI emits the same numbers as the RPC.
g -d "{\"path\":\"$WAV\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AnalyzeFile | python3 -c "
import json,sys
d=json.load(sys.stdin)
p=d.get('peakDbfs',0.0); tp=d.get('truePeakDbtp',0.0); lu=d.get('lufs',-144.0)
assert -40 < p < 0.5, 'peak dBFS out of range: '+str(p)
assert tp >= p-0.2, 'true peak below sample peak'
assert -60 < lu < 0, 'lufs out of range: '+str(lu)
print('smoke: PASS — loudness analysis (peak %.1f dBFS, true-peak %.1f dBTP, %.1f LUFS)'%(p,tp,lu))
" || { echo "smoke: loudness analysis out of range" >&2; exit 1; }
"$BIN" analyze "$WAV" 2>/dev/null | python3 -c "import json,sys;json.load(sys.stdin);print('smoke: PASS — CLI analyze emits JSON')" \
    || { echo "smoke: CLI analyze did not emit JSON" >&2; exit 1; }

# Render report: RenderToFile with report=true returns the output's loudness inline,
# so a script gets "render + measure" from one call. It must match a standalone
# AnalyzeFile of the same file (and be non-silent on this populated project).
REP=$(g -d "{\"path\":\"$WORK/rep.wav\",\"tail_seconds\":0,\"end_beat\":4,\"report\":true}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile)
g -d "{\"path\":\"$WORK/rep.wav\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AnalyzeFile > "$WORK/rep_ana.json"
echo "$REP" | python3 -c "
import json,sys
rep=json.load(sys.stdin).get('report',{})
ana=json.load(open('$WORK/rep_ana.json'))
assert 'lufs' in rep, 'render report missing loudness'
for k in ('peakDbfs','truePeakDbtp','rmsDbfs','lufs'):
    assert abs(rep.get(k,0)-ana.get(k,0)) < 1e-2, 'render report %s != AnalyzeFile (%s vs %s)'%(k,rep.get(k),ana.get(k))
assert rep['lufs'] > -60, 'render report is silent (%.1f LUFS)'%rep['lufs']
print('smoke: PASS — RenderToFile report matches AnalyzeFile (%.1f LUFS inline)'%rep['lufs'])
" || { echo "smoke: render report did not match AnalyzeFile" >&2; exit 1; }

# Waveform thumbnail cache: min/max peaks for the render, with the expected bucket count.
g -d "{\"path\":\"$WAV\",\"buckets\":64}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetWaveform | python3 -c "
import json,sys
d=json.load(sys.stdin)
assert d.get('buckets')==64 and len(d.get('mins',[]))==64 and len(d.get('maxs',[]))==64, 'bucket count wrong'
assert max(d['maxs'])>0.05 and min(d['mins'])<-0.05, 'no waveform energy'
assert d.get('durationSeconds',0)>0, 'no duration'
print('smoke: PASS — waveform cache (64 buckets, peak %.2f)'%max(d['maxs']))
" || { echo "smoke: waveform cache wrong" >&2; exit 1; }

# RT diagnostics: after the offline renders above, the engine reports device settings
# and a render speed well above realtime.
g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetDiagnostics | python3 -c "
import json,sys
d=json.load(sys.stdin)
sr=d.get('sampleRate',0); bs=d.get('blockSize',0); rx=d.get('renderSpeedX',0.0)
assert sr>0 and bs>0, 'bad device settings: %s/%s'%(sr,bs)
assert rx>1.0, 'offline render not faster than realtime: '+str(rx)
print('smoke: PASS — diagnostics (%g Hz / %d smp, render %.0fx realtime)'%(sr,bs,rx))
" || { echo "smoke: diagnostics out of range" >&2; exit 1; }

# RemoveBus re-indexes sends: add two buses, send insert 1 -> the 2nd bus, remove the
# 1st bus, and confirm the send follows the 2nd bus down to its new (decremented) index
# and one bus is gone. Then remove the 2nd bus too, so state is clean for the next test.
RB1=$(g -d '{"name":"RmA"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddBus | python3 -c "import json,sys;print(json.load(sys.stdin).get('id',0))")
RB2=$(g -d '{"name":"RmB"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddBus | python3 -c "import json,sys;print(json.load(sys.stdin).get('id',0))")
g -d "{\"insert\":1,\"bus\":$RB2,\"level\":0.6}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetSend >/dev/null
g -d "{\"id\":$RB1}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveBus >/dev/null
NEWB=$((RB2-1))
SENDBUS=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/ListInserts | python3 -c "import json,sys;ins=json.load(sys.stdin)['inserts'];s=[x for x in ins if x.get('index',0)==1][0].get('sends',[]);print(s[0].get('bus',-1) if s else -1)")
[ "$SENDBUS" = "$NEWB" ] || { echo "smoke: RemoveBus did not re-index the send (got bus $SENDBUS, expected $NEWB)" >&2; exit 1; }
# The removed bus's index must no longer be a bus (it was RmA); RmB is now at NEWB.
BADREMOVE=$(g -d '{"id":0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveBus | python3 -c "import json,sys;print(json.load(sys.stdin).get('ok',False))")
[ "$BADREMOVE" = "False" ] || { echo "smoke: RemoveBus accepted a non-bus (master)" >&2; exit 1; }
g -d "{\"id\":$NEWB}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveBus >/dev/null   # clean up RmB
echo "smoke: PASS — RemoveBus re-indexes sends (send followed bus $RB2 -> $NEWB) and rejects non-buses"

# Mixer scenes now also capture aux-send levels: add a bus + send, snapshot, change the
# send, recall, and confirm the send level is restored (last — it adds a bus insert).
SB=$(g -d '{"name":"SceneBus"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddBus | python3 -c "import json,sys;print(json.load(sys.stdin).get('id',0))")
sendlvl() { g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/ListInserts | python3 -c "import json,sys;ins=json.load(sys.stdin)['inserts'];s=[x for x in ins if x.get('index',0)==0][0].get('sends',[]);print(([e.get('level',0) for e in s if e.get('bus',0)==$SB] or [0])[0])"; }
g -d "{\"insert\":0,\"bus\":$SB,\"level\":0.5}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetSend >/dev/null
g -d '{"name":"sendscene"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/DefineMixerScene >/dev/null
g -d "{\"insert\":0,\"bus\":$SB,\"level\":0.9}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetSend >/dev/null
g -d '{"name":"sendscene"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/RecallMixerScene >/dev/null
python3 -c "
lv=float('$(sendlvl)')
assert abs(lv-0.5) < 1e-4, 'scene recall did not restore the send level (got %.3f, expected 0.5)'%lv
print('smoke: PASS — mixer scene restores aux-send level (%.2f)'%lv)
" || { echo "smoke: mixer scene did not restore send level" >&2; exit 1; }

echo "smoke: OK"
