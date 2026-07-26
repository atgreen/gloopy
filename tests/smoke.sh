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
# Isolate user templates to a temp dir (read by the app at launch, per templatesDir()).
export GLOOPY_TEMPLATE_PATH="$WORK/templates"

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

# Scaling-aware normalized set: the cutoff param is log-scaled (20..20000 Hz), so a 0..1
# knob position 0.5 must land at the geometric mean (~632 Hz), not the arithmetic mean.
g -d "{\"id\":\"track/$TID/synth/cutoff\",\"value\":0.5}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetParameterNormalized >/dev/null
CUTMID=$(g -d "{\"id\":\"track/$TID/synth/cutoff\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetParameter | grep -o '"value": [0-9.]*' | grep -o '[0-9.]*')
python3 -c "
v=float('$CUTMID')
assert 560 < v < 710, 'log-scaled normalized 0.5 should be ~632 Hz (geometric mean), got %.1f'%v
print('smoke: PASS — SetParameterNormalized honours log scaling (cutoff 0.5 -> %.0f Hz ~ geo mean)'%v)
" || { echo 'smoke: normalized-set ignored log scaling' >&2; exit 1; }
g -d "{\"id\":\"track/$TID/synth/cutoff\",\"value\":800}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetParameter >/dev/null   # restore

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

# Time signature: bars<->beats conversion follows it. Beat 6 is bar 2.3 in 4/4 but bar
# 3.1 in 3/4; the inverse round-trips; 6/8 gives 3 beats/bar. Reset to 4/4 after.
bb() { g -d "{\"beat\":$1}" 127.0.0.1:$PORT gloopy.v1.Gloopy/BeatsToBarBeat | python3 -c "import json,sys;d=json.load(sys.stdin);print(d.get('bar'),round(d.get('beatInBar',0),3))"; }
read -r B4 X4 <<< "$(bb 6)"
g -d '{"numerator":3,"denominator":4}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetTimeSignature >/dev/null
read -r B3 X3 <<< "$(bb 6)"
BPB=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetTimeSignature | python3 -c "import json,sys;print(json.load(sys.stdin).get('beatsPerBar'))")
INV=$(g -d '{"bar":3,"beat_in_bar":1}' 127.0.0.1:$PORT gloopy.v1.Gloopy/BarBeatToBeats | python3 -c "import json,sys;print(json.load(sys.stdin).get('beat'))")
python3 -c "
assert ($B4,$X4)==(2,3.0), '4/4 beat6 should be bar2.3, got $B4.$X4'
assert ($B3,$X3)==(3,1.0), '3/4 beat6 should be bar3.1, got $B3.$X3'
assert abs(float('$BPB')-3.0)<1e-6, '3/4 beats_per_bar should be 3, got $BPB'
assert abs(float('$INV')-6.0)<1e-6, 'bar3.1 @3/4 should invert to beat 6, got $INV'
print('smoke: PASS — time signature bars<->beats (4/4 6=bar2.3, 3/4 6=bar3.1, inverse=$INV)')
" || { echo "smoke: time signature conversion wrong" >&2; exit 1; }
g -d '{"numerator":4,"denominator":4}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetTimeSignature >/dev/null

# Controller mapping: a source (cc:20) drives a ParamModel target scaled to [lo,hi].
CT=$(g -d '{"name":"ctltest","wave":"SAW"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"source\":\"cc:20\",\"target\":\"track/$CT/synth/cutoff\",\"lo\":500,\"hi\":5000}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddControllerMap >/dev/null
g -d '{"source":"cc:20","value":1.0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetController >/dev/null
CVAL=$(g -d "{\"id\":\"track/$CT/synth/cutoff\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetParameter | python3 -c "import json,sys;print(json.load(sys.stdin).get('value',0))")
python3 -c "assert abs(float('$CVAL')-5000)<1, 'controller did not drive param (got %s)'%'$CVAL'" \
    || { echo "smoke: controller mapping did not drive the parameter" >&2; exit 1; }
echo "smoke: PASS — controller map cc:20 -> cutoff (=$CVAL at full)"
# Inversion (lo>hi) + per-map bypass: invert so value 1 -> low cutoff; bypass freezes
# the param against further feeds; un-bypass re-applies.
TGT="track/$CT/synth/cutoff"
cut() { g -d "{\"id\":\"$TGT\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetParameter | python3 -c "import json,sys;print(round(json.load(sys.stdin).get('value',0)))"; }
g -d "{\"source\":\"cc:20\",\"target\":\"$TGT\",\"lo\":5000,\"hi\":500}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddControllerMap >/dev/null
g -d '{"source":"cc:20","value":1.0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetController >/dev/null; INVL=$(cut)
g -d '{"source":"cc:20","value":0.0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetController >/dev/null; INVH=$(cut)
g -d "{\"source\":\"cc:20\",\"target\":\"$TGT\",\"bypass\":true}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetControllerBypass >/dev/null
g -d '{"source":"cc:20","value":1.0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetController >/dev/null; BYP=$(cut)
g -d "{\"source\":\"cc:20\",\"target\":\"$TGT\",\"bypass\":false}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetControllerBypass >/dev/null
g -d '{"source":"cc:20","value":1.0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetController >/dev/null; UNB=$(cut)
python3 -c "
invl,invh,byp,unb=float('$INVL'),float('$INVH'),float('$BYP'),float('$UNB')
assert abs(invl-500)<2 and abs(invh-5000)<2, 'inverted map wrong (v1=%.0f v0=%.0f)'%(invl,invh)
assert abs(byp-5000)<2, 'bypassed map still moved the param (%.0f, expected ~5000)'%byp
assert abs(unb-500)<2, 'un-bypass did not re-apply (%.0f, expected ~500)'%unb
print('smoke: PASS — controller inversion (v1->%.0f) + bypass (frozen %.0f) + un-bypass (->%.0f)'%(invl,byp,unb))
" || { echo "smoke: controller inversion/bypass failed" >&2; exit 1; }
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
# LFO phase offset + unipolar (Wave 4 #9). A half-cycle phase shift on a synced saw LFO
# changes the render; phase + unipolar round-trip through ListModulations. (The unit-tested
# lfoUnit math in GloopyTests::Lfo covers the folding itself.)
g -d "{\"target\":\"track/$MT/synth/cutoff\",\"depth\":1400,\"center\":1500,\"shape\":2,\"sync_beats\":1,\"phase\":0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetModulation >/dev/null
g -d "{\"path\":\"$WORK/mod_ph0.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"target\":\"track/$MT/synth/cutoff\",\"depth\":1400,\"center\":1500,\"shape\":2,\"sync_beats\":1,\"phase\":0.5}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetModulation >/dev/null
g -d "{\"path\":\"$WORK/mod_ph05.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"target\":\"track/$MT/synth/cutoff\",\"depth\":1400,\"center\":1500,\"shape\":2,\"sync_beats\":1,\"phase\":0.25,\"unipolar\":true}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetModulation >/dev/null
MODPU=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/ListModulations | python3 -c "import json,sys;m=json.load(sys.stdin)['mods'][0];print(m.get('phase',0), m.get('unipolar',False))")
python3 -c "
import wave
def rd(p):
    w=wave.open(p);f=w.readframes(w.getnframes());return [int.from_bytes(f[i:i+3],'little',signed=True) for i in range(0,len(f),3)]
a=rd('$WORK/mod_ph0.wav');b=rd('$WORK/mod_ph05.wav');m=min(len(a),len(b))
d=sum(abs(a[i]-b[i]) for i in range(m))/m/(1<<23)
assert d>0.003, 'phase offset did not change the render (diff=%.5f)'%d
ph,uni='$MODPU'.split()
assert abs(float(ph)-0.25)<1e-4, 'phase not round-tripped: %s'%ph
assert uni=='True', 'unipolar not round-tripped: %s'%uni
print('smoke: PASS — LFO phase offset changes render (diff %.4f); phase 0.25 + unipolar round-trip'%d)
" || { echo 'smoke: modulation phase/unipolar wrong' >&2; exit 1; }
# LFO slew (Wave 4 #9): a one-pole smoothing on a fast square LFO softens its abrupt
# edges, so the slewed render differs from the un-slewed one; slew_ms round-trips.
g -d "{\"target\":\"track/$MT/synth/cutoff\",\"depth\":1400,\"center\":1500,\"shape\":3,\"sync_beats\":0.5,\"slew_ms\":0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetModulation >/dev/null
g -d "{\"path\":\"$WORK/mod_noslew.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"target\":\"track/$MT/synth/cutoff\",\"depth\":1400,\"center\":1500,\"shape\":3,\"sync_beats\":0.5,\"slew_ms\":40}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetModulation >/dev/null
g -d "{\"path\":\"$WORK/mod_slew.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
SLEW=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/ListModulations | python3 -c "import json,sys;print(json.load(sys.stdin)['mods'][0].get('slewMs',0))")
python3 -c "
import wave
def rd(p):
    w=wave.open(p);f=w.readframes(w.getnframes());return [int.from_bytes(f[i:i+3],'little',signed=True) for i in range(0,len(f),3)]
a=rd('$WORK/mod_noslew.wav');b=rd('$WORK/mod_slew.wav');m=min(len(a),len(b))
d=sum(abs(a[i]-b[i]) for i in range(m))/m/(1<<23)
assert d>0.003, 'slew did not change the render (diff=%.5f)'%d
assert abs(float('$SLEW')-40.0)<1e-3, 'slew_ms not round-tripped: %s'%'$SLEW'
print('smoke: PASS — LFO slew softens a square LFO (render diff %.4f), slew_ms=40 round-trips'%d)
" || { echo 'smoke: modulation slew wrong' >&2; exit 1; }
# Random / sample-and-hold LFO (shape 4, Wave 4 #9): stepped random cutoff changes the
# render vs static, and — because the step value is a deterministic hash of the cycle
# index — two renders of the same S&H mod are byte-identical (reproducible bounces).
g -d "{\"target\":\"track/$MT/synth/cutoff\",\"depth\":1400,\"center\":1500,\"shape\":4,\"sync_beats\":0.25}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetModulation >/dev/null
g -d "{\"path\":\"$WORK/mod_sh1.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"path\":\"$WORK/mod_sh2.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
SHSHAPE=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/ListModulations | python3 -c "import json,sys;print(json.load(sys.stdin)['mods'][0].get('shape',0))")
python3 -c "
import wave
def rd(p):
    w=wave.open(p);f=w.readframes(w.getnframes());return [int.from_bytes(f[i:i+3],'little',signed=True) for i in range(0,len(f),3)]
base=rd('$WORK/mod_base.wav');sh1=rd('$WORK/mod_sh1.wav');sh2=rd('$WORK/mod_sh2.wav')
m=min(len(base),len(sh1))
d=sum(abs(base[i]-sh1[i]) for i in range(m))/m/(1<<23)
assert d>0.003, 'S&H LFO did not change the render (diff=%.5f)'%d
assert sh1==sh2, 'S&H render not deterministic (hash should make bounces reproducible)'
assert int('$SHSHAPE')==4, 'shape 4 (random) not round-tripped: got $SHSHAPE'
print('smoke: PASS — random/S&H LFO changes render (diff %.4f), deterministic across renders, shape=4 round-trips'%d)
" || { echo 'smoke: modulation sample-and-hold wrong' >&2; exit 1; }
# Multiple modulation sources per target (Wave 4 #9): SetModulation is a canonical single
# set (one source on the target); AddModulation appends a SECOND source, and sources on the
# same target SUM (center + depth1*osc1 + depth2*osc2). A slow LFO alone, then a fast LFO
# stacked on top, must render differently (the 2nd source adds movement); ListModulations
# shows 2 sources on the target and both survive a composition round-trip.
g -d "{\"target\":\"track/$MT/synth/cutoff\",\"depth\":1000,\"center\":1500,\"shape\":0,\"sync_beats\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetModulation >/dev/null
g -d "{\"path\":\"$WORK/mod_one.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"target\":\"track/$MT/synth/cutoff\",\"depth\":600,\"center\":1500,\"shape\":2,\"sync_beats\":0.5}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddModulation >/dev/null
g -d "{\"path\":\"$WORK/mod_two.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
NMODS=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/ListModulations | python3 -c "import json,sys;m=json.load(sys.stdin).get('mods',[]);print(sum(1 for x in m if x.get('target')=='track/$MT/synth/cutoff'))")
g -d "{\"path\":\"$WORK/stack.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SaveComposition >/dev/null
g -d "{\"path\":\"$WORK/stack.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/LoadComposition >/dev/null
NMODS2=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/ListModulations | python3 -c "import json,sys;m=json.load(sys.stdin).get('mods',[]);print(sum(1 for x in m if x.get('target')=='track/$MT/synth/cutoff'))")
python3 -c "
import wave
def rd(p):
    w=wave.open(p);f=w.readframes(w.getnframes());return [int.from_bytes(f[i:i+3],'little',signed=True) for i in range(0,len(f),3)]
one=rd('$WORK/mod_one.wav');two=rd('$WORK/mod_two.wav');m=min(len(one),len(two))
d=sum(abs(one[i]-two[i]) for i in range(m))/m/(1<<23)
assert d>0.003, 'stacking a 2nd LFO did not change the render (diff=%.5f)'%d
assert int('$NMODS')==2, 'expected 2 sources on the target after AddModulation, got $NMODS'
assert int('$NMODS2')==2, 'the 2 stacked sources did not survive save/reload, got $NMODS2'
print('smoke: PASS — 2nd LFO stacks + sums (render diff %.4f), 2 sources on target, round-trips'%d)
" || { echo 'smoke: multiple modulation sources wrong' >&2; exit 1; }
g -d "{\"target\":\"track/$MT/synth/cutoff\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveModulation >/dev/null
NMODS3=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/ListModulations | python3 -c "import json,sys;m=json.load(sys.stdin).get('mods',[]);print(sum(1 for x in m if x.get('target')=='track/$MT/synth/cutoff'))")
[ "$NMODS3" = "0" ] || { echo "smoke: RemoveModulation should clear ALL sources on the target (got $NMODS3)" >&2; exit 1; }
g -d "{\"id\":$MT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null

# Sampler playback window (Wave 6 #18): reverse + start/end trim on a one-shot Sampler.
# Build an asymmetric sample (silent for 6000 frames, then a tone in the last quarter) so
# the tone's POSITION in the render reveals direction/window. Forward -> tone plays late;
# reverse -> tone plays early; start=0.75 forward -> the window is just the tone, so it
# also plays early. Controls round-trip through GetSamplerControls and a composition save.
python3 -c "
import wave, struct, math
N=8000; fr=[]
for i in range(N):
    v = 0.0 if i < 6000 else 0.8*math.sin(2*math.pi*440*(i-6000)/44100.0)
    fr.append(int(max(-1,min(1,v))*32767))
w=wave.open('$WORK/asym.wav','w'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(44100)
w.writeframes(b''.join(struct.pack('<h',s) for s in fr)); w.close()
"
ST=$(g -d "{\"name\":\"smp\",\"path\":\"$WORK/asym.wav\",\"root_note\":60}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSamplerTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$ST,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"notes\":[{\"pitch\":60,\"start_beat\":0,\"length_beats\":1,\"velocity\":1.0}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
g -d "{\"path\":\"$WORK/smp_fwd.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"track_id\":$ST,\"start\":0,\"end\":1,\"reverse\":true,\"root_note\":0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetSamplerControls >/dev/null
SCREV=$(g -d "{\"id\":$ST}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetSamplerControls | python3 -c "import json,sys;d=json.load(sys.stdin);print('%.3f %.3f %s'%(d.get('start',0),d.get('end',1),d.get('reverse',False)))")
g -d "{\"path\":\"$WORK/smp_rev.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"track_id\":$ST,\"start\":0.75,\"end\":1,\"reverse\":false,\"root_note\":0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetSamplerControls >/dev/null
g -d "{\"path\":\"$WORK/smp_trim.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
SC=$(g -d "{\"id\":$ST}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetSamplerControls | python3 -c "import json,sys;d=json.load(sys.stdin);print('%.3f %.3f %s'%(d.get('start',0),d.get('end',1),d.get('reverse',False)))")
g -d "{\"path\":\"$WORK/smp.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SaveComposition >/dev/null
g -d "{\"path\":\"$WORK/smp.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/LoadComposition >/dev/null
SC2=$(g -d "{\"id\":$ST}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetSamplerControls | python3 -c "import json,sys;d=json.load(sys.stdin);print('%.3f %.3f %s'%(d.get('start',0),d.get('end',1),d.get('reverse',False)))")
python3 -c "
import wave
def rd(p):
    w=wave.open(p);f=w.readframes(w.getnframes());return [abs(int.from_bytes(f[i:i+3],'little',signed=True)) for i in range(0,len(f),3)]
fwd=rd('$WORK/smp_fwd.wav');rev=rd('$WORK/smp_rev.wav');trim=rd('$WORK/smp_trim.wav')
def early(x): return sum(x[:1500])
def total(x): return sum(x)
assert total(fwd)>0 and total(rev)>0, 'sampler rendered silent'
# Reverse moves the late tone to the front: early energy jumps.
assert early(rev) > early(fwd)*5, 'reverse did not move the tone earlier (fwd_early=%d rev_early=%d)'%(early(fwd),early(rev))
# Trimming to the last quarter (forward) also puts the tone up front.
assert early(trim) > early(fwd)*5, 'start-trim did not move the tone earlier (fwd_early=%d trim_early=%d)'%(early(fwd),early(trim))
assert '$SCREV'=='0.000 1.000 True', 'GetSamplerControls reverse not read back: got [$SCREV]'
assert '$SC'=='0.750 1.000 False', 'GetSamplerControls start-trim not read back: got [$SC]'
assert '$SC2'=='0.750 1.000 False', 'sampler window did not survive composition round-trip: got [$SC2]'
print('smoke: PASS — sampler reverse + start-trim move the tone (fwd_early=%d rev_early=%d trim_early=%d), controls round-trip'%(early(fwd),early(rev),early(trim)))
" || { echo 'smoke: sampler playback window wrong' >&2; exit 1; }
g -d "{\"id\":$ST}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null
# Sampler fade-in (Wave 6 #18): a per-voice amplitude fade ramps the start up from zero.
# On a constant-amplitude tone, WITH a fade the peak near the start is far below the peak
# once the fade completes; WITHOUT a fade they're equal (flat). (The render WAV is stereo
# 24-bit interleaved, so read the left channel only: every other 3-byte sample.)
python3 -c "
import wave, struct, math
w=wave.open('$WORK/tone.wav','w'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(44100)
w.writeframes(b''.join(struct.pack('<h',int(0.7*math.sin(2*math.pi*330*i/44100)*32767)) for i in range(20000))); w.close()
"
FT=$(g -d "{\"name\":\"tone\",\"path\":\"$WORK/tone.wav\",\"root_note\":60}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSamplerTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$FT,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"notes\":[{\"pitch\":60,\"start_beat\":0,\"length_beats\":1,\"velocity\":1.0}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
g -d "{\"path\":\"$WORK/fade_off.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"track_id\":$FT,\"start\":0,\"end\":1,\"reverse\":false,\"root_note\":0,\"fade_in\":0.2,\"fade_out\":0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetSamplerControls >/dev/null
g -d "{\"path\":\"$WORK/fade_on.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
FADEIN=$(g -d "{\"id\":$FT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetSamplerControls | python3 -c "import json,sys;print('%.3f'%json.load(sys.stdin).get('fadeIn',0))")
python3 -c "
import wave
def rd(p):   # left channel only (stereo 24-bit interleaved -> stride 6 bytes)
    w=wave.open(p);f=w.readframes(w.getnframes());return [abs(int.from_bytes(f[i:i+3],'little',signed=True)) for i in range(0,len(f),6)]
off=rd('$WORK/fade_off.wav');on=rd('$WORK/fade_on.wav')
def pk(x,a,b): return max(x[a:b])
# 'late' window (~200 ms in) is past the 0.2 s fade; 'early' (~5 ms in) is deep in the ramp.
off_e,off_l=pk(off,100,300),pk(off,8600,8800)
on_e,on_l  =pk(on,100,300), pk(on,8600,8800)
assert off_l>0 and on_l>0, 'sampler rendered silent'
assert off_e > off_l*0.8, 'no-fade render is not flat at the start (early=%d late=%d)'%(off_e,off_l)
assert on_e  < on_l*0.4,  'fade-in did not ramp the start up (early=%d late=%d)'%(on_e,on_l)
assert abs(on_l-off_l) < off_l*0.15, 'fade changed the post-fade level (on=%d off=%d)'%(on_l,off_l)
assert abs(float('$FADEIN')-0.2)<1e-3, 'fade_in not read back: got $FADEIN'
print('smoke: PASS — sampler fade-in ramps the start (fade early/late peak %d/%d vs flat %d/%d), fade_in round-trips'%(on_e,on_l,off_e,off_l))
" || { echo 'smoke: sampler fade-in wrong' >&2; exit 1; }
g -d "{\"id\":$FT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null

# Sampler mono / choke: a LONG (2 s) tone under two hits at DIFFERENT pitches (60 then 67),
# a beat apart. Polyphonic, both voices ring together after the 2nd hit (two tones summed);
# mono chokes the 1st when the 2nd fires, so only one voice rings — lower RMS in the overlap
# region. GetSamplerControls reports mono; it round-trips.
python3 -c "
import wave, struct, math
w=wave.open('$WORK/longtone.wav','w'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(44100)
w.writeframes(b''.join(struct.pack('<h',int(0.6*math.sin(2*math.pi*220*i/44100)*32767)) for i in range(88200))); w.close()
"
MT=$(g -d "{\"name\":\"choketone\",\"path\":\"$WORK/longtone.wav\",\"root_note\":60}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSamplerTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$MT,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"notes\":[{\"pitch\":60,\"start_beat\":0,\"length_beats\":1,\"velocity\":1.0},{\"pitch\":67,\"start_beat\":1,\"length_beats\":1,\"velocity\":1.0}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
g -d "{\"path\":\"$WORK/choke_poly.wav\",\"tail_seconds\":0,\"end_beat\":4,\"track_id\":$MT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"track_id\":$MT,\"start\":0,\"end\":1,\"reverse\":false,\"root_note\":0,\"fade_in\":0,\"fade_out\":0,\"loop\":false,\"mono\":true}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetSamplerControls >/dev/null
MONO=$(g -d "{\"id\":$MT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetSamplerControls | python3 -c "import json,sys;print(json.load(sys.stdin).get('mono',False))")
g -d "{\"path\":\"$WORK/choke_mono.wav\",\"tail_seconds\":0,\"end_beat\":4,\"track_id\":$MT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
python3 -c "
import wave
def rd(p):   # left channel (stereo 24-bit interleaved -> stride 6)
    w=wave.open(p);f=w.readframes(w.getnframes());return [int.from_bytes(f[i:i+3],'little',signed=True) for i in range(0,len(f),6)]
def rms(x): return (sum(v*v for v in x)/max(1,len(x)))**0.5
poly=rd('$WORK/choke_poly.wav');mono=rd('$WORK/choke_mono.wav');n=min(len(poly),len(mono))
a,b=int(0.35*n),int(0.85*n)   # overlap window: after the 2nd hit, while both voices still ring
assert '$MONO'=='True','GetSamplerControls did not report mono'
rp,rm=rms(poly[a:b]),rms(mono[a:b])
assert rp > rm*1.2, 'mono choke did not cut the first voice (poly RMS %.0f not > mono %.0f)'%(rp,rm)
print('smoke: PASS — sampler mono choke: poly rings both voices (RMS %.0f) vs mono cuts the 1st (%.0f), mono round-trips'%(rp,rm))
" || { echo 'smoke: sampler mono choke wrong' >&2; exit 1; }
g -d "{\"id\":$MT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null

# Sampler loop mode (Wave 6 #18): a SHORT sample (~45 ms) under a 1-beat note. As a one-shot
# it plays once and is silent for the rest of the note; looped it sustains until the note-off,
# then stops. So mid-note (~200 ms) the looped render has signal where the one-shot is silent,
# and past the note-off (~600 ms) the looped render is silent again (note-off released it).
python3 -c "
import wave, struct, math
w=wave.open('$WORK/short.wav','w'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(44100)
w.writeframes(b''.join(struct.pack('<h',int(0.7*math.sin(2*math.pi*330*i/44100)*32767)) for i in range(2000))); w.close()
"
LT=$(g -d "{\"name\":\"loopsmp\",\"path\":\"$WORK/short.wav\",\"root_note\":60}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSamplerTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$LT,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"notes\":[{\"pitch\":60,\"start_beat\":0,\"length_beats\":1,\"velocity\":1.0}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
g -d "{\"path\":\"$WORK/loop_off.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"track_id\":$LT,\"start\":0,\"end\":1,\"reverse\":false,\"root_note\":0,\"fade_in\":0,\"fade_out\":0,\"loop\":true}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetSamplerControls >/dev/null
g -d "{\"path\":\"$WORK/loop_on.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
LOOPRB=$(g -d "{\"id\":$LT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetSamplerControls | python3 -c "import json,sys;print(json.load(sys.stdin).get('loop',False))")
g -d "{\"path\":\"$WORK/loop.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SaveComposition >/dev/null
g -d "{\"path\":\"$WORK/loop.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/LoadComposition >/dev/null
LOOPRB2=$(g -d "{\"id\":$LT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetSamplerControls | python3 -c "import json,sys;print(json.load(sys.stdin).get('loop',False))")
python3 -c "
import wave
def rdL(p):
    w=wave.open(p);f=w.readframes(w.getnframes());return [abs(int.from_bytes(f[i:i+3],'little',signed=True)) for i in range(0,len(f),6)]
off=rdL('$WORK/loop_off.wav');on=rdL('$WORK/loop_on.wav')
def e(x,a,b): return sum(x[a:b])
# ~200 ms in (past the 45 ms sample, inside the 1-beat note ~469 ms): looped sustains, one-shot silent.
mid_off,mid_on=e(off,8000,9000),e(on,8000,9000)
assert mid_on > mid_off*20, 'loop did not sustain past the one-shot length (off=%d on=%d)'%(mid_off,mid_on)
# ~600 ms in (past the note-off at ~469 ms): the loop released, so it is quiet again.
rel=e(on,26000,27000)
assert rel < mid_on*0.1, 'note-off did not stop the loop (sustaining=%d after-noteoff=%d)'%(mid_on,rel)
assert '$LOOPRB'=='True', 'loop flag not read back: got $LOOPRB'
assert '$LOOPRB2'=='True', 'loop flag did not survive composition round-trip: got $LOOPRB2'
print('smoke: PASS — sampler loop sustains a short sample (mid off=%d on=%d), note-off releases it (after=%d), loop round-trips'%(mid_off,mid_on,rel))
" || { echo 'smoke: sampler loop wrong' >&2; exit 1; }
g -d "{\"id\":$LT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null

# Notes copy/paste as JSON (Wave 2 #5): ExportNotesJSON emits a clip's notes as a JSON
# array; ImportNotesJSON builds a new clip from that JSON on another track. Round-trip: the
# pasted clip's notes match the source (pitch/start/length/velocity), verified via GetClipNotes.
SRC=$(g -d '{"name":"jsrc","wave":"SAW","attack":0.01,"decay":0.1,"sustain":0.9,"release":0.2,"gain":0.8}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$SRC,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"notes\":[{\"pitch\":60,\"start_beat\":0,\"length_beats\":1,\"velocity\":0.9},{\"pitch\":64,\"start_beat\":1,\"length_beats\":0.5,\"velocity\":0.7},{\"pitch\":67,\"start_beat\":2,\"length_beats\":0.25,\"velocity\":0.5}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
# Export the source clip's notes JSON to a file (avoids shell-quoting the JSON payload).
g -d "{\"track_id\":$SRC,\"index\":0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/ExportNotesJSON | python3 -c "import json,sys;open('$WORK/notes.json','w').write(json.load(sys.stdin).get('json',''))"
DST=$(g -d '{"name":"jdst","wave":"SAW","attack":0.01,"decay":0.1,"sustain":0.9,"release":0.2,"gain":0.8}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
# Build the ImportNotesJSON request in python (embeds the JSON string safely) and send via -d @file.
python3 -c "import json;open('$WORK/imp.json','w').write(json.dumps({'track_id':$DST,'start_beat':8,'json':open('$WORK/notes.json').read()}))"
IDX=$(g -d @ 127.0.0.1:$PORT gloopy.v1.Gloopy/ImportNotesJSON < "$WORK/imp.json" | python3 -c "import json,sys;print(json.load(sys.stdin).get('index',0))")   # proto3 omits index 0
g -d "{\"track_id\":$DST,\"index\":$IDX}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetClipNotes > "$WORK/pasted.json" 2>/dev/null
python3 -c "
import json
src=json.loads(open('$WORK/notes.json').read())
assert len(src)==3, 'ExportNotesJSON did not emit 3 notes: %r'%src
assert {n['pitch'] for n in src}=={60,64,67}, 'wrong pitches exported: %r'%src
assert int('$IDX')>=0, 'ImportNotesJSON returned no clip (idx=$IDX)'
notes=json.loads(open('$WORK/pasted.json').read()).get('notes',[])
assert len(notes)==3, 'pasted clip has %d notes, expected 3'%len(notes)
got=sorted((n['pitch'], round(n.get('startBeat',0),4), round(n.get('lengthBeats',0),4)) for n in notes)
exp=[(60,0.0,1.0),(64,1.0,0.5),(67,2.0,0.25)]
assert got==exp, 'pasted notes mismatch: %r vs %r'%(got,exp)
print('smoke: PASS — notes export->import JSON round-trips (3 notes, pitch/start/length preserved), new clip idx $IDX')
" || { echo 'smoke: notes JSON copy/paste wrong' >&2; exit 1; }
g -d "{\"id\":$SRC}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null
g -d "{\"id\":$DST}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null

# User templates (Wave 6 #16): save the current project as a template, confirm it lists
# alongside the built-ins, then seed a fresh project from it and check the tracks return.
# NewProject/NewFromTemplate are destructive to the session, so snapshot + restore around it.
g -d "{\"path\":\"$WORK/tpl_restore.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SaveProject >/dev/null
TPL_A=$(g -d '{"name":"tplA","wave":"SAW","gain":0.8}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
TPL_B=$(g -d '{"name":"tplB","wave":"SINE","gain":0.8}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
NBEFORE=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/ListTracks | python3 -c "import json,sys;print(len(json.load(sys.stdin).get('tracks',[])))")
g -d '{"name":"Smoke Template"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SaveAsTemplate >/dev/null
LISTED=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/ListTemplates | python3 -c "import json,sys;print('Smoke Template' in json.load(sys.stdin).get('names',[]))")
[ -f "$WORK/templates/Smoke Template.gloopy" ] || { echo "smoke: SaveAsTemplate did not write the template file" >&2; exit 1; }
g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/NewProject >/dev/null
NEMPTY=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/ListTracks | python3 -c "import json,sys;print(len(json.load(sys.stdin).get('tracks',[])))")
g -d '{"name":"Smoke Template"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/NewFromTemplate >/dev/null
NAFTER=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/ListTracks | python3 -c "import json,sys;print(len(json.load(sys.stdin).get('tracks',[])))")
python3 -c "
assert '$LISTED'=='True', 'saved template not in ListTemplates'
assert int('$NBEFORE')==int('$NAFTER'), 'seeded template track count %s != saved %s'%('$NAFTER','$NBEFORE')
assert int('$NEMPTY') < int('$NAFTER'), 'NewProject then NewFromTemplate did not restore tracks (empty=%s after=%s)'%('$NEMPTY','$NAFTER')
print('smoke: PASS — user template saves, lists, and seeds a fresh project (%s tracks) '%'$NAFTER')
" || { echo 'smoke: user template wrong' >&2; exit 1; }
g -d "{\"path\":\"$WORK/tpl_restore.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/LoadProject >/dev/null   # restore the session

# ParamModel keystone — automation-by-id: an automation lane addresses the SAME ParamModel
# id a controller/LFO uses (track/<id>/synth/cutoff), written through the shared
# applyParamValue. A ramp 300->8000 Hz over 0..4 beats brightens the render vs a static
# cutoff; the lane's param_id + points survive a composition round-trip.
AM=$(g -d '{"name":"autoid","wave":"SAW","attack":0.01,"decay":0.1,"sustain":0.9,"release":0.2,"gain":0.8}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$AM,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"notes\":[{\"pitch\":72,\"start_beat\":0,\"length_beats\":4,\"velocity\":0.9}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
g -d "{\"id\":\"track/$AM/synth/cutoff\",\"value\":300}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetParameter >/dev/null
g -d "{\"path\":\"$WORK/am_base.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"param_id\":\"track/$AM/synth/cutoff\",\"points\":[{\"beat\":0,\"value\":300},{\"beat\":4,\"value\":8000}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetAutomation >/dev/null
g -d "{\"path\":\"$WORK/am_auto.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
python3 -c "
import wave
def rd(p):
    w=wave.open(p);f=w.readframes(w.getnframes());return [int.from_bytes(f[i:i+3],'little',signed=True) for i in range(0,len(f),3)]
a=rd('$WORK/am_base.wav');b=rd('$WORK/am_auto.wav');m=min(len(a),len(b))
d=sum(abs(a[i]-b[i]) for i in range(m))/m/(1<<23)
assert d>0.003, 'id-addressed automation did not move the render (diff=%.5f)'%d
print('smoke: PASS — automation-by-id sweeps cutoff on track/$AM/synth/cutoff (render diff %.4f)'%d)
" || { echo 'smoke: automation-by-id had no effect' >&2; exit 1; }
# The lane is reported by GetAutomation with its param_id, and the AddAutomationPoint
# keyframe primitive appends to the same lane.
g -d "{\"param_id\":\"track/$AM/synth/cutoff\",\"beat\":2,\"value\":1000}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddAutomationPoint >/dev/null
g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetAutomation | python3 -c "
import json,sys
lanes=[l for l in json.load(sys.stdin)['lanes'] if l.get('paramId')=='track/$AM/synth/cutoff']
assert lanes, 'no id-addressed lane reported'
assert len(lanes[0]['points'])==3, 'AddAutomationPoint did not add a keyframe (%d points)'%len(lanes[0]['points'])
print('smoke: PASS — GetAutomation reports the id-addressed lane (3 keyframes)')
" || { echo 'smoke: automation-by-id lane not reported' >&2; exit 1; }
# Composition round-trip: the id-addressed lane survives SaveComposition -> LoadComposition.
g -d "{\"path\":\"$WORK/amcomp\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SaveComposition >/dev/null
grep -q 'target = "track/'"$AM"'/synth/cutoff"' "$WORK/amcomp/automation/lanes.toml" || { echo "smoke: lanes.toml missing the id-addressed target" >&2; exit 1; }
g -d "{\"path\":\"$WORK/amcomp\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/LoadComposition >/dev/null
g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetAutomation | python3 -c "
import json,sys
lanes=[l for l in json.load(sys.stdin)['lanes'] if l.get('paramId')=='track/$AM/synth/cutoff']
assert lanes and len(lanes[0]['points'])==3, 'id-addressed lane lost on composition reload'
print('smoke: PASS — automation-by-id lane survives composition round-trip')
" || { echo 'smoke: automation-by-id did not round-trip' >&2; exit 1; }
# Functional: the reloaded project must still SWEEP — proves the track id (track/$AM)
# survived the round-trip so the id-addressed lane is still live (not just the string).
g -d "{\"path\":\"$WORK/am_reload.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
python3 -c "
import wave
def rd(p):
    w=wave.open(p);f=w.readframes(w.getnframes());return [int.from_bytes(f[i:i+3],'little',signed=True) for i in range(0,len(f),3)]
base=rd('$WORK/am_base.wav');rl=rd('$WORK/am_reload.wav');m=min(len(base),len(rl))
d=sum(abs(base[i]-rl[i]) for i in range(m))/m/(1<<23)
assert d>0.003, 'reloaded project did not sweep — track id not preserved (diff=%.5f)'%d
print('smoke: PASS — reloaded project still sweeps (stable track id keeps the lane live, diff %.4f)'%d)
" || { echo 'smoke: track id not preserved across composition round-trip' >&2; exit 1; }
# Stepped automation: switching the same cutoff lane to step (hold) interpolation renders
# darker than the linear ramp (the cutoff holds at 300 Hz until beat 4 instead of sweeping),
# and the step flag survives a composition round-trip (byte-identical re-render).
g -d "{\"param_id\":\"track/$AM/synth/cutoff\",\"step\":true}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetAutomationStep >/dev/null
g -d "{\"path\":\"$WORK/am_step.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"path\":\"$WORK/amstep.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SaveComposition >/dev/null
g -d "{\"path\":\"$WORK/amstep.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/LoadComposition >/dev/null
g -d "{\"path\":\"$WORK/am_step2.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
python3 -c "
import wave
def rd(p):
    w=wave.open(p);f=w.readframes(w.getnframes());return [int.from_bytes(f[i:i+3],'little',signed=True) for i in range(0,len(f),3)]
def rms(x): return (sum(v*v for v in x)/max(1,len(x)))**0.5
lin=rd('$WORK/am_auto.wav');st=rd('$WORK/am_step.wav');st2=rd('$WORK/am_step2.wav')
m=min(len(lin),len(st),len(st2))
d=sum(abs(lin[i]-st[i]) for i in range(m))/m/(1<<23)
assert d>0.003, 'step vs linear automation rendered the same (diff=%.5f)'%d
assert rms(st[:m]) < rms(lin[:m])*0.85, 'stepped (held-low cutoff) should be darker than the linear sweep (step rms %.0f vs lin %.0f)'%(rms(st[:m]),rms(lin[:m]))
assert st==st2, 'step flag did not survive the composition round-trip (re-render differs)'
print('smoke: PASS — stepped automation differs from linear (diff %.4f), is darker, and round-trips'%d)
" || { echo 'smoke: stepped automation wrong' >&2; exit 1; }
# Curved automation: back to linear interpolation, then apply an ease-in curve (+1, slow
# start) to the same cutoff lane. Ease-in holds the cutoff low near the front of each
# segment, so the render is darker than the straight linear ramp yet still sweeps (differs
# from the flat/stepped render). The per-lane curve float survives a composition round-trip
# (byte-identical re-render), and Get reports it back.
g -d "{\"param_id\":\"track/$AM/synth/cutoff\",\"step\":false}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetAutomationStep >/dev/null
g -d "{\"param_id\":\"track/$AM/synth/cutoff\",\"curve\":1.0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetAutomationCurve >/dev/null
g -d "{\"path\":\"$WORK/am_curve.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"path\":\"$WORK/amcurve.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SaveComposition >/dev/null
grep -q 'curve' "$WORK/amcurve.gloopy/automation/lanes.toml" || { echo "smoke: lanes.toml missing the curve field" >&2; exit 1; }
g -d "{\"path\":\"$WORK/amcurve.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/LoadComposition >/dev/null
g -d "{\"path\":\"$WORK/am_curve2.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
python3 -c "
import wave
def rd(p):
    w=wave.open(p);f=w.readframes(w.getnframes());return [int.from_bytes(f[i:i+3],'little',signed=True) for i in range(0,len(f),3)]
def rms(x): return (sum(v*v for v in x)/max(1,len(x)))**0.5
lin=rd('$WORK/am_auto.wav');cv=rd('$WORK/am_curve.wav');cv2=rd('$WORK/am_curve2.wav')
m=min(len(lin),len(cv),len(cv2))
d=sum(abs(lin[i]-cv[i]) for i in range(m))/m/(1<<23)
assert d>0.003, 'ease-in curve rendered the same as linear (diff=%.5f)'%d
assert rms(cv[:m]) < rms(lin[:m])*0.9, 'ease-in (slow start, held-low cutoff) should be darker than the linear sweep (curve rms %.0f vs lin %.0f)'%(rms(cv[:m]),rms(lin[:m]))
assert cv==cv2, 'curve did not survive the composition round-trip (re-render differs)'
print('smoke: PASS — ease-in automation curve differs from linear (diff %.4f), is darker, and round-trips'%d)
" || { echo 'smoke: curved automation wrong' >&2; exit 1; }
AMID=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetState | python3 -c "import json,sys;print([t['id'] for t in json.load(sys.stdin)['tracks']][-1])")
g -d "{\"id\":$AMID}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null 2>&1 || true

# ParamModel keystone — plugin-param ids: a hosted instrument plugin's params must appear
# in ListParameters (track/<id>/plugin/<index>), be set/get through the model, and be
# automatable by that id. Plugin-agnostic + conditional (skipped if no instrument plugin
# is installed in this environment).
PLID=$(g -d '{"force":false}' 127.0.0.1:$PORT gloopy.v1.Gloopy/ScanPlugins | python3 -c "
import json,sys
insts=[p for p in json.load(sys.stdin).get('plugins',[]) if p.get('isInstrument')]
print(insts[0]['identifier'] if insts else '')" 2>/dev/null)
if [ -n "$PLID" ]; then
  PT=$(g -d "{\"identifier\":\"$PLID\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddPluginTrack | grep -o '[0-9]\+' | head -1)
  sleep 2
  NP=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/ListParameters | python3 -c "import json,sys;print(len([p for p in json.load(sys.stdin).get('params',[]) if p['id'].startswith('track/$PT/plugin/')]))")
  [ "${NP:-0}" -ge 1 ] || { echo "smoke: plugin instrument exposed no plugin params" >&2; exit 1; }
  # Set/Get round-trip through the model: two distinct values must read back monotonically
  # (robust to param quantisation).
  g -d "{\"id\":\"track/$PT/plugin/0\",\"value\":0.2}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetParameter >/dev/null
  LO=$(g -d "{\"id\":\"track/$PT/plugin/0\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetParameter | python3 -c "import json,sys;print(json.load(sys.stdin).get('value',0))")
  g -d "{\"id\":\"track/$PT/plugin/0\",\"value\":0.8}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetParameter >/dev/null
  HI=$(g -d "{\"id\":\"track/$PT/plugin/0\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetParameter | python3 -c "import json,sys;print(json.load(sys.stdin).get('value',0))")
  # Automation-by-id on a plugin param id is accepted and reported.
  g -d "{\"param_id\":\"track/$PT/plugin/0\",\"points\":[{\"beat\":0,\"value\":0},{\"beat\":4,\"value\":1}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetAutomation >/dev/null
  AOK=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetAutomation | python3 -c "import json,sys;print(any(l.get('paramId')=='track/$PT/plugin/0' for l in json.load(sys.stdin).get('lanes',[])))")
  python3 -c "
lo,hi=float('$LO'),float('$HI'); aok='$AOK'
assert hi>lo, 'plugin param set/get not monotonic (%.3f -> %.3f)'%(lo,hi)
assert aok=='True', 'automation-by-id not accepted on a plugin param'
print('smoke: PASS — plugin-param ids ($NP params; set/get %.2f->%.2f; automatable by id)'%(lo,hi))
" || { echo 'smoke: plugin-param id model wrong' >&2; exit 1; }
  g -d "{\"id\":$PT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null
else
  echo "smoke: (no instrument plugin installed, skipping plugin-param id test)"
fi

# Per-track microtuning: a +1200-cent detune raises a sine note one octave, so its
# zero-crossing rate (~2x fundamental) doubles. Proves the cents->frequency mapping and
# that detune rides the synth param model (SetParameter/GetParameter).
DT=$(g -d '{"name":"detune","wave":"SINE","attack":0.005,"decay":0.05,"sustain":0.95,"release":0.05,"gain":0.9}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$DT,\"start_beat\":0,\"length_beats\":4,\"notes\":[{\"pitch\":69,\"start_beat\":0,\"length_beats\":3.5,\"velocity\":0.9}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
zcr() { python3 -c "
import wave,sys
w=wave.open(sys.argv[1]);f=w.readframes(w.getnframes())
v=[int.from_bytes(f[i:i+3],'little',signed=True) for i in range(0,len(f),3)][0::2]
s=v[len(v)//4:len(v)*3//4]
print(sum(1 for i in range(1,len(s)) if (s[i-1]<0)!=(s[i]<0)))" "$1"; }
g -d "{\"id\":\"track/$DT/synth/detune\",\"value\":0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetParameter >/dev/null
g -d "{\"path\":\"$WORK/det0.wav\",\"tail_seconds\":0,\"end_beat\":3.5}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"id\":\"track/$DT/synth/detune\",\"value\":1200}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetParameter >/dev/null
g -d "{\"path\":\"$WORK/det12.wav\",\"tail_seconds\":0,\"end_beat\":3.5}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
DV=$(g -d "{\"id\":\"track/$DT/synth/detune\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetParameter | python3 -c "import json,sys;print(json.load(sys.stdin).get('value',0))")
python3 -c "
z0=$(zcr "$WORK/det0.wav"); z12=$(zcr "$WORK/det12.wav")
r=z12/max(1,z0)
assert 1.8 < r < 2.2, 'detune +1200 cents should double the frequency (ZCR ratio %.3f)'%r
assert abs(float('$DV')-1200)<1e-3, 'GetParameter detune not 1200 (got $DV)'
print('smoke: PASS — per-track detune +1200c doubles pitch (ZCR ratio %.3f), param round-trips'%r)
" || { echo "smoke: per-track detune did not shift pitch" >&2; exit 1; }
g -d "{\"id\":$DT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null

# Non-destructive per-clip transpose (Wave 4 / editing): a +12 clip transpose raises a sine
# note one octave at render time (zero-crossing rate doubles), the stored notes are unchanged,
# and the transpose survives a composition round-trip. Reuses the zcr() helper above.
CT=$(g -d '{"name":"ctr","wave":"SINE","attack":0.005,"decay":0.05,"sustain":0.95,"release":0.05,"gain":0.9}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$CT,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"notes\":[{\"pitch\":69,\"start_beat\":0,\"length_beats\":3.5,\"velocity\":0.9}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
g -d "{\"path\":\"$WORK/ctr0.wav\",\"tail_seconds\":0,\"end_beat\":3.5}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"track_id\":$CT,\"index\":0,\"semitones\":12}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetClipTranspose >/dev/null
g -d "{\"path\":\"$WORK/ctr12.wav\",\"tail_seconds\":0,\"end_beat\":3.5}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
# Stored notes untouched (still pitch 69, not 81).
STILL69=$(g -d "{\"track_id\":$CT,\"index\":0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetClipNotes | python3 -c "import json,sys;print(json.load(sys.stdin)['notes'][0]['pitch'])")
g -d "{\"path\":\"$WORK/ctr.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SaveComposition >/dev/null
g -d "{\"path\":\"$WORK/ctr.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/LoadComposition >/dev/null
g -d "{\"path\":\"$WORK/ctr12b.wav\",\"tail_seconds\":0,\"end_beat\":3.5}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
python3 -c "
z0=$(zcr "$WORK/ctr0.wav"); z12=$(zcr "$WORK/ctr12.wav"); z12b=$(zcr "$WORK/ctr12b.wav")
r=z12/max(1,z0); rb=z12b/max(1,z0)
assert 1.8 < r < 2.2, 'clip transpose +12 should double the pitch (ZCR ratio %.3f)'%r
assert int('$STILL69')==69, 'transpose must not edit the stored notes (pitch became $STILL69)'
assert 1.8 < rb < 2.2, 'clip transpose did not survive composition round-trip (ZCR ratio %.3f)'%rb
print('smoke: PASS — non-destructive clip transpose +12 doubles pitch (ZCR %.3f), notes untouched, round-trips (%.3f)'%(r,rb))
" || { echo 'smoke: clip transpose wrong' >&2; exit 1; }
# Non-destructive per-clip velocity scale: a 0.5x scale halves each note's velocity at render
# (quieter, since the synth maps velocity to amplitude), the stored velocities are untouched,
# and a 0x scale renders silent. Reuse the $CT sine track + its clip.
g -d "{\"track_id\":$CT,\"index\":0,\"semitones\":0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetClipTranspose >/dev/null
g -d "{\"track_id\":$CT,\"index\":0,\"scale\":1.0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetClipVelocity >/dev/null
g -d "{\"path\":\"$WORK/vs_full.wav\",\"tail_seconds\":0,\"end_beat\":3.5}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"track_id\":$CT,\"index\":0,\"scale\":0.5}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetClipVelocity >/dev/null
g -d "{\"path\":\"$WORK/vs_half.wav\",\"tail_seconds\":0,\"end_beat\":3.5}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
VVEL=$(g -d "{\"track_id\":$CT,\"index\":0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetClipNotes | python3 -c "import json,sys;print('%.2f'%json.load(sys.stdin)['notes'][0]['velocity'])")
g -d "{\"track_id\":$CT,\"index\":0,\"scale\":0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetClipVelocity >/dev/null
g -d "{\"path\":\"$WORK/vs_zero.wav\",\"tail_seconds\":0,\"end_beat\":3.5}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
python3 -c "
import wave
def rms(p):
    w=wave.open(p);f=w.readframes(w.getnframes());v=[int.from_bytes(f[i:i+3],'little',signed=True) for i in range(0,len(f),3)]
    return (sum(x*x for x in v)/max(1,len(v)))**0.5
full=rms('$WORK/vs_full.wav');half=rms('$WORK/vs_half.wav');zero=rms('$WORK/vs_zero.wav')
assert full>0, 'full-velocity render silent'
assert half < full*0.75, '0.5x velocity should be clearly quieter (full=%.0f half=%.0f)'%(full,half)
assert zero < full*0.05, '0x velocity should be ~silent (full=%.0f zero=%.0f)'%(full,zero)
assert abs(float('$VVEL')-0.9)<0.02, 'velocity scale must not edit the stored note velocity (got $VVEL, expected 0.90)'
print('smoke: PASS — clip velocity scale: 0.5x quieter (rms %.0f<%.0f), 0x silent (%.0f), stored velocity untouched (%s)'%(half,full,zero,'$VVEL'))
" || { echo 'smoke: clip velocity scale wrong' >&2; exit 1; }
g -d "{\"id\":$CT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null

# Microtuning (Wave 4 #11): a +1200-cent tuning on pitch class 0 raises a C one octave (ZCR
# doubles), it survives a composition round-trip, and a Scala .scl import maps degrees to the
# right cents-from-ET offsets. Reuses the zcr() helper from the detune block.
MT=$(g -d '{"name":"tune","wave":"SINE","attack":0.005,"decay":0.05,"sustain":0.95,"release":0.05,"gain":0.9}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$MT,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"notes\":[{\"pitch\":60,\"start_beat\":0,\"length_beats\":3.5,\"velocity\":0.9}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
g -d '{"cents":[0,0,0,0,0,0,0,0,0,0,0,0]}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetTuning >/dev/null
g -d "{\"path\":\"$WORK/tune0.wav\",\"tail_seconds\":0,\"end_beat\":3.5}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d '{"cents":[1200,0,0,0,0,0,0,0,0,0,0,0]}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetTuning >/dev/null
g -d "{\"path\":\"$WORK/tune12.wav\",\"tail_seconds\":0,\"end_beat\":3.5}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"path\":\"$WORK/tune.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SaveComposition >/dev/null
g -d "{\"path\":\"$WORK/tune.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/LoadComposition >/dev/null
TRB=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetTuning | python3 -c "import json,sys;print(json.load(sys.stdin).get('cents',[0])[0])")
# Scala import: a 12-note .scl with degree 1 at 150 cents (ET would be 100) -> class 1 offset +50.
printf '! test.scl\nTest\n 12\n!\n 150.0\n 200.0\n 300.0\n 400.0\n 500.0\n 600.0\n 700.0\n 800.0\n 900.0\n 1000.0\n 1100.0\n 2/1\n' > "$WORK/test.scl"
g -d "{\"path\":\"$WORK/test.scl\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/ImportScl >/dev/null
SCL1=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetTuning | python3 -c "import json,sys;c=json.load(sys.stdin).get('cents',[0]*12);print('%.1f %.1f'%(c[1],c[2]))")
python3 -c "
z0=$(zcr "$WORK/tune0.wav"); z12=$(zcr "$WORK/tune12.wav")
r=z12/max(1,z0)
assert 1.8 < r < 2.2, 'tuning +1200c on C should double the pitch (ZCR ratio %.3f)'%r
assert abs(float('$TRB')-1200)<1e-3, 'tuning did not survive composition round-trip (got $TRB)'
s1,s2='$SCL1'.split()
assert abs(float(s1)-50.0)<0.5, 'scl degree 1 (150c) should give class-1 offset +50, got %s'%s1
assert abs(float(s2)-0.0)<0.5, 'scl degree 2 (200c=ET) should give class-2 offset 0, got %s'%s2
print('smoke: PASS — microtuning +1200c doubles pitch (ZCR %.3f), round-trips, .scl import maps degrees (class1=%sc)'%(r,s1))
" || { echo 'smoke: microtuning wrong' >&2; exit 1; }
g -d '{"cents":[0,0,0,0,0,0,0,0,0,0,0,0]}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetTuning >/dev/null
g -d "{\"id\":$MT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null

# Note probability (generative): a 16-note clip. At probability 1.0 all notes fire; at 0.0
# none do (silent); at 0.5 roughly half fire — quieter than full, louder than silent — and
# the gate is DETERMINISTIC (two 0.5 renders are byte-identical) so it round-trips.
PT=$(g -d '{"name":"prob","wave":"SAW","attack":0.005,"decay":0.05,"sustain":0.9,"release":0.05,"gain":0.8}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
NOTES=$(python3 -c "import json;print(json.dumps([{'pitch':60,'start_beat':i*0.25,'length_beats':0.2,'velocity':0.9} for i in range(16)]))")
g -d "{\"track_id\":$PT,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"notes\":$NOTES}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
g -d "{\"track_id\":$PT,\"index\":0,\"scale\":1.0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetClipProbability >/dev/null
g -d "{\"path\":\"$WORK/pr_full.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"track_id\":$PT,\"index\":0,\"scale\":0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetClipProbability >/dev/null
g -d "{\"path\":\"$WORK/pr_zero.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"track_id\":$PT,\"index\":0,\"scale\":0.5}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetClipProbability >/dev/null
g -d "{\"path\":\"$WORK/pr_half.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"path\":\"$WORK/prob.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SaveComposition >/dev/null
g -d "{\"path\":\"$WORK/prob.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/LoadComposition >/dev/null
g -d "{\"path\":\"$WORK/pr_half2.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
python3 -c "
import wave
def rd(p):
    w=wave.open(p);f=w.readframes(w.getnframes());return [int.from_bytes(f[i:i+3],'little',signed=True) for i in range(0,len(f),3)]
def rms(x): return (sum(v*v for v in x)/max(1,len(x)))**0.5
full=rd('$WORK/pr_full.wav');zero=rd('$WORK/pr_zero.wav');half=rd('$WORK/pr_half.wav');half2=rd('$WORK/pr_half2.wav')
rf,rz,rh=rms(full),rms(zero),rms(half)
assert rf>0, 'probability 1.0 render silent'
assert rz < rf*0.02, 'probability 0.0 should be silent (full=%.0f zero=%.0f)'%(rf,rz)
assert rf*0.15 < rh < rf*0.85, 'probability 0.5 should be partial (full=%.0f half=%.0f)'%(rf,rh)
assert half==half2, 'probability gate is not deterministic / did not survive round-trip (renders differ)'
print('smoke: PASS — note probability: 1.0 full (rms %.0f), 0.0 silent (%.0f), 0.5 partial (%.0f) + deterministic round-trip'%(rf,rz,rh))
" || { echo 'smoke: note probability wrong' >&2; exit 1; }
g -d "{\"id\":$PT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null

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
# Arp probability (generative gate): probability 1.0 fires every generated step and must
# reproduce the default (unset) arp render byte-for-byte (proves the proto3 unset=full
# handling). 0.5 drops ~half the steps via the shared deterministic noteFires gate, so it
# renders quieter than 1.0 yet non-silent, is byte-identical across two renders
# (reproducible), and survives a project round-trip. GetTrackArp reports it back.
g -d "{\"track_id\":$AT,\"enabled\":true,\"rate\":0.25,\"octaves\":1,\"gate\":0.5,\"mode\":0,\"probability\":1.0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetTrackArp >/dev/null
g -d "{\"path\":\"$WORK/arp_p100.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"track_id\":$AT,\"enabled\":true,\"rate\":0.25,\"octaves\":1,\"gate\":0.5,\"mode\":0,\"probability\":0.5}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetTrackArp >/dev/null
ARPPROB=$(g -d "{\"track_id\":$AT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetTrackArp | python3 -c "import json,sys;print(round(json.load(sys.stdin).get('probability',0),3))")
g -d "{\"path\":\"$WORK/arp_p50.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"path\":\"$WORK/arp_prob.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SaveProject >/dev/null
g -d "{\"path\":\"$WORK/arp_prob.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/LoadProject >/dev/null
g -d "{\"path\":\"$WORK/arp_p50b.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
python3 -c "
import wave
def rd(p):
    w=wave.open(p);f=w.readframes(w.getnframes())
    return [int.from_bytes(f[i:i+3],'little',signed=True) for i in range(0,len(f),3)]
def rms(x): return (sum(v*v for v in x)/max(1,len(x)))**0.5
assert '$ARPPROB'=='0.5','GetTrackArp did not report probability 0.5 (got $ARPPROB)'
on=rd('$WORK/arp_on.wav');p100=rd('$WORK/arp_p100.wav');p50=rd('$WORK/arp_p50.wav');p50b=rd('$WORK/arp_p50b.wav')
assert p100==on,'probability 1.0 did not reproduce the default arp render (unset!=full)'
m=min(len(p100),len(p50))
assert p50[:m]!=p100[:m],'probability 0.5 rendered identically to 1.0 (no steps dropped)'
assert rms(p50[:m])>0,'probability 0.5 rendered silent (gate dropped everything)'
assert rms(p50[:m])<rms(p100[:m])*0.9,'probability 0.5 not quieter than 1.0 (p50 rms %.0f vs p100 %.0f)'%(rms(p50[:m]),rms(p100[:m]))
assert p50==p50b,'probability 0.5 not reproducible across a project round-trip (re-render differs)'
print('smoke: PASS — arp probability: 1.0=full(=default), 0.5 drops steps (quieter, non-silent), reproducible + round-trips')
" || { echo "smoke: arp probability wrong" >&2; exit 1; }
# Arp gate: the gate is each arp note's length as a fraction of the step. A legato gate (1.0)
# on a sustaining synth rings the full step; a staccato gate (0.25) plays 1/4 as long, so the
# render carries far less sustained energy. GetTrackArp reports the gate back.
g -d "{\"track_id\":$AT,\"enabled\":true,\"rate\":0.25,\"octaves\":1,\"gate\":1.0,\"mode\":0,\"probability\":1.0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetTrackArp >/dev/null
g -d "{\"path\":\"$WORK/arp_g100.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"track_id\":$AT,\"enabled\":true,\"rate\":0.25,\"octaves\":1,\"gate\":0.25,\"mode\":0,\"probability\":1.0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetTrackArp >/dev/null
ARPGATE=$(g -d "{\"track_id\":$AT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetTrackArp | python3 -c "import json,sys;print(round(json.load(sys.stdin).get('gate',0),3))")
g -d "{\"path\":\"$WORK/arp_g25.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
python3 -c "
import wave
def rd(p):
    w=wave.open(p);f=w.readframes(w.getnframes());return [int.from_bytes(f[i:i+3],'little',signed=True) for i in range(0,len(f),3)]
def rms(x): return (sum(v*v for v in x)/max(1,len(x)))**0.5
assert '$ARPGATE'=='0.25','GetTrackArp did not report gate 0.25 (got $ARPGATE)'
g100=rd('$WORK/arp_g100.wav');g25=rd('$WORK/arp_g25.wav');m=min(len(g100),len(g25))
assert rms(g100[:m]) > rms(g25[:m])*1.3, 'legato gate not louder than staccato (g100 %.0f vs g25 %.0f)'%(rms(g100[:m]),rms(g25[:m]))
print('smoke: PASS — arp gate: legato(1.0) rings fuller than staccato(0.25) (rms %.0f vs %.0f), gate round-trips'%(rms(g100[:m]),rms(g25[:m])))
" || { echo "smoke: arp gate wrong" >&2; exit 1; }
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
# CropClip: a fresh 4-beat clip with notes at 0/1/2/3, cropped to [1,3), keeps the
# middle two re-based to 0 and 1; an empty range is rejected.
CR=$(g -d '{"name":"croptest","wave":"SAW"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$CR,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"notes\":[{\"pitch\":60,\"start_beat\":0,\"length_beats\":0.5,\"velocity\":0.8},{\"pitch\":62,\"start_beat\":1,\"length_beats\":0.5,\"velocity\":0.8},{\"pitch\":64,\"start_beat\":2,\"length_beats\":0.5,\"velocity\":0.8},{\"pitch\":65,\"start_beat\":3,\"length_beats\":0.5,\"velocity\":0.8}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
g -d "{\"track_id\":$CR,\"index\":0,\"start_beat\":1,\"end_beat\":3}" 127.0.0.1:$PORT gloopy.v1.Gloopy/CropClip >/dev/null
g -d "{\"track_id\":$CR,\"index\":0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetClipNotes | python3 -c "
import json,sys
ns=sorted(((n.get('startBeat',0),n['pitch']) for n in json.load(sys.stdin)['notes']))
assert ns==[(0.0,62),(1.0,64)], 'crop kept wrong notes: %s'%ns
print('smoke: PASS — CropClip [1,3) kept notes 62@0, 64@1')
" || { echo "smoke: CropClip did not keep the right notes" >&2; exit 1; }
BADCROP=$(g -d "{\"track_id\":$CR,\"index\":0,\"start_beat\":2,\"end_beat\":2}" 127.0.0.1:$PORT gloopy.v1.Gloopy/CropClip | python3 -c "import json,sys;print(json.load(sys.stdin).get('ok',False))")
[ "$BADCROP" = "False" ] || { echo "smoke: CropClip accepted an empty range" >&2; exit 1; }
g -d "{\"id\":$CR}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null
# Audio-clip crop: render a sustained tone to a WAV, load it as an audio clip [0,4),
# then crop to [2,4). SaveProject shows the clip MOVED to start=2/len=2 AND that its
# sample buffer was actually cut to exactly the 2-beat window (~2*60/bpm*rate frames),
# proving the source samples were trimmed, not merely re-anchored. A clip named "src"
# (the WAV basename) isolates it in the save.
clipattr() {   # $1=saved .gloopy  $2=attr  -> value of that attr on the clip named "src"
  python3 -c "
import xml.etree.ElementTree as ET
for c in ET.parse('$1').getroot().iter('CLIP'):
    if c.get('name')=='src': print(c.get('$2')); break
"; }
BPM=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetTransport | python3 -c "import json,sys;print(json.load(sys.stdin).get('bpm',120))")
ACS=$(g -d '{"name":"cropsrc","wave":"SAW","attack":0.01,"decay":0.1,"sustain":0.8,"release":0.2,"gain":0.9}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$ACS,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"looped\":false,\"notes\":[{\"pitch\":48,\"start_beat\":0,\"length_beats\":4,\"velocity\":0.9}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
ASRC="$WORK/src.wav"
g -d "{\"path\":\"$ASRC\",\"tail_seconds\":0,\"start_beat\":0,\"end_beat\":4,\"track_id\":$ACS}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"id\":$ACS}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null   # only wanted its WAV
AUT=$(g -d '{"name":"cropaud"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddAudioTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$AUT,\"start_beat\":0,\"path\":\"$ASRC\",\"gain\":1.0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddAudioClip >/dev/null
g -d "{\"track_id\":$AUT,\"index\":0,\"start_beat\":2,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/CropClip >/dev/null
g -d "{\"path\":\"$WORK/acrop.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SaveProject >/dev/null
ASTART=$(clipattr "$WORK/acrop.gloopy" start); ALEN=$(clipattr "$WORK/acrop.gloopy" len)
ARATE=$(clipattr "$WORK/acrop.gloopy" arate);  AF1=$(clipattr "$WORK/acrop.gloopy" aframes)
python3 -c "
st,ln,rate,f1,bpm=float('$ASTART'),float('$ALEN'),float('$ARATE'),int('$AF1'),float('$BPM')
want=2.0*60.0/bpm*rate                      # the [2,4) window = 2 beats of source samples
assert abs(st-2.0)<1e-6, 'crop did not move clip to beat 2 (start=%g)'%st
assert abs(ln-2.0)<1e-6, 'crop did not resize clip to 2 beats (len=%g)'%ln
assert abs(f1-want) < 0.03*want, 'buffer not cut to the 2-beat window: %d frames, wanted ~%d'%(f1,want)
print('smoke: PASS — audio CropClip -> start=%g len=%g, buffer trimmed to %d frames (2 beats @ %g bpm)'%(st,ln,f1,bpm))
" || { echo 'smoke: audio CropClip did not trim/move correctly' >&2; exit 1; }
g -d "{\"id\":$AUT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null
# Fade curve shapes: an audio clip [0,4) with a 2-beat fade-in on a constant tone. Over the
# fade region the gain follows the shape, so RMS orders equal-power > linear > exponential
# (sin(t·π/2) > t > t²). Each render is reproducible and the shape survives a project
# round-trip. SaveProject records fadeshape on the clip.
FST=$(g -d '{"name":"fadeshape"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddAudioTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$FST,\"start_beat\":0,\"path\":\"$ASRC\",\"gain\":1.0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddAudioClip >/dev/null
g -d "{\"track_id\":$FST,\"index\":0,\"fade_in_beats\":2.0,\"fade_out_beats\":0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetClipFades >/dev/null
g -d "{\"track_id\":$FST,\"index\":0,\"shape\":0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetClipFadeShape >/dev/null
g -d "{\"path\":\"$WORK/fs_lin.wav\",\"tail_seconds\":0,\"end_beat\":4,\"track_id\":$FST}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"track_id\":$FST,\"index\":0,\"shape\":1}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetClipFadeShape >/dev/null
g -d "{\"path\":\"$WORK/fs_ep.wav\",\"tail_seconds\":0,\"end_beat\":4,\"track_id\":$FST}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"track_id\":$FST,\"index\":0,\"shape\":2}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetClipFadeShape >/dev/null
g -d "{\"path\":\"$WORK/fs_exp.wav\",\"tail_seconds\":0,\"end_beat\":4,\"track_id\":$FST}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"path\":\"$WORK/fs.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SaveProject >/dev/null
FSSHAPE=$(python3 -c "
import xml.etree.ElementTree as ET
for c in ET.parse('$WORK/fs.gloopy').getroot().iter('CLIP'):
    if c.get('name')=='src': print(c.get('fadeshape')); break
")
g -d "{\"path\":\"$WORK/fs.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/LoadProject >/dev/null
FST2=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetState | python3 -c "import json,sys;print([t['id'] for t in json.load(sys.stdin)['tracks']][-1])")
g -d "{\"path\":\"$WORK/fs_exp2.wav\",\"tail_seconds\":0,\"end_beat\":4,\"track_id\":$FST2}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
python3 -c "
import wave
def rd(p):
    w=wave.open(p);f=w.readframes(w.getnframes());return [int.from_bytes(f[i:i+3],'little',signed=True) for i in range(0,len(f),3)]
def rms(x): return (sum(v*v for v in x)/max(1,len(x)))**0.5
lin=rd('$WORK/fs_lin.wav');ep=rd('$WORK/fs_ep.wav');ex=rd('$WORK/fs_exp.wav');ex2=rd('$WORK/fs_exp2.wav')
n=min(len(lin),len(ep),len(ex),len(ex2)); h=n//2      # first half = the 2-beat fade-in region
assert '$FSSHAPE'=='2','SaveProject did not record fadeshape=2 (got $FSSHAPE)'
rl,re,rx=rms(lin[:h]),rms(ep[:h]),rms(ex[:h])
assert re>rl>rx, 'fade-region RMS not ordered equal-power>linear>exp (ep=%.0f lin=%.0f exp=%.0f)'%(re,rl,rx)
assert ex[:n]==ex2[:n], 'exponential fade not reproducible across a project round-trip (re-render differs)'
print('smoke: PASS — fade shapes: equal-power(%.0f)>linear(%.0f)>exp(%.0f) over the fade, and round-trips'%(re,rl,rx))
" || { echo 'smoke: fade shapes wrong' >&2; exit 1; }
g -d "{\"id\":$FST2}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null
# ConsolidateClip: a looped clip (2-beat content, notes at 0/1, tiled over 4 beats)
# flattens to explicit notes at 0/1/2/3 and un-loops (content becomes length).
CN=$(g -d '{"name":"contest","wave":"SAW"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$CN,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":2,\"looped\":true,\"notes\":[{\"pitch\":60,\"start_beat\":0,\"length_beats\":0.5,\"velocity\":0.8},{\"pitch\":62,\"start_beat\":1,\"length_beats\":0.5,\"velocity\":0.8}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
g -d "{\"track_id\":$CN,\"index\":0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/ConsolidateClip >/dev/null
g -d "{\"track_id\":$CN,\"index\":0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetClipNotes | python3 -c "
import json,sys
ns=sorted((round(n.get('startBeat',0),3),n['pitch']) for n in json.load(sys.stdin)['notes'])
assert ns==[(0.0,60),(1.0,62),(2.0,60),(3.0,62)], 'consolidate wrong: %s'%ns
print('smoke: PASS — ConsolidateClip flattened 2 reps to notes at 0/1/2/3')
" || { echo 'smoke: ConsolidateClip flattened wrong' >&2; exit 1; }
g -d "{\"path\":\"$WORK/consol.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SaveProject >/dev/null
python3 -c "
import xml.etree.ElementTree as ET
c=[c for c in ET.parse('$WORK/consol.gloopy').getroot().iter('CLIP') if len(list(c))==4][0]
assert c.get('looped')=='0' and abs(float(c.get('content'))-4.0)<1e-6, 'not un-looped: looped=%s content=%s'%(c.get('looped'),c.get('content'))
print('smoke: PASS — consolidated clip is un-looped (content=len=4)')
" || { echo 'smoke: ConsolidateClip did not un-loop' >&2; exit 1; }
g -d "{\"id\":$CN}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null
# BounceClip: freeze a MIDI clip to audio on a new "(bounce)" track (non-destructive).
# The new track is audio with one non-silent clip; the source track keeps its MIDI clip.
BT=$(g -d '{"name":"lead","wave":"SAW","attack":0.01,"decay":0.1,"sustain":0.8,"release":0.2,"gain":0.9}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$BT,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"looped\":false,\"notes\":[{\"pitch\":60,\"start_beat\":0,\"length_beats\":1,\"velocity\":0.9},{\"pitch\":64,\"start_beat\":2,\"length_beats\":1,\"velocity\":0.9}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
NT=$(g -d "{\"track_id\":$BT,\"index\":0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/BounceClip | grep -o '[0-9]\+' | head -1)
[ "${NT:-0}" -gt 0 ] || { echo "smoke: BounceClip returned no track id" >&2; exit 1; }
g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetState | python3 -c "
import json,sys
ts={t['id']:t for t in json.load(sys.stdin)['tracks']}
src,bnc=ts[$BT],ts[$NT]
assert src['type']=='instrument' and src.get('clips')==1, 'source track changed (non-destructive violated): %s'%src
assert bnc['type']=='audio' and bnc.get('clips')==1, 'bounce track not a 1-clip audio track: %s'%bnc
assert '(bounce)' in bnc.get('name',''), 'bounce track misnamed: %s'%bnc.get('name')
print('smoke: PASS — BounceClip made audio track \"%s\" (source MIDI clip intact)'%bnc['name'])
" || { echo 'smoke: BounceClip state wrong' >&2; exit 1; }
g -d "{\"path\":\"$WORK/bounce.wav\",\"tail_seconds\":0.5,\"start_beat\":0,\"end_beat\":4,\"track_id\":$NT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
BPK=$(g -d "{\"path\":\"$WORK/bounce.wav\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AnalyzeFile | python3 -c "import json,sys;print(json.load(sys.stdin).get('peakDbfs',-120))")
python3 -c "assert float('$BPK')>-40, 'bounced audio is silent (%.1f dBFS)'%float('$BPK'); print('smoke: PASS — bounced audio track renders non-silent (%.1f dBFS)'%float('$BPK'))" \
    || { echo 'smoke: bounced audio was silent' >&2; exit 1; }
g -d "{\"id\":$NT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null
g -d "{\"id\":$BT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null
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
# Knife / split-notes: a 2-beat note spanning beat 1 is cut into [0,1)+[1,2); a note
# starting AT the cut is untouched. Verified via GetClipNotes.
g -d "{\"track_id\":$CO,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"looped\":false,\"notes\":[{\"pitch\":60,\"start_beat\":0,\"length_beats\":2,\"velocity\":0.8},{\"pitch\":64,\"start_beat\":1,\"length_beats\":1,\"velocity\":0.8}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
KC=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetState | python3 -c "import json,sys;print(next(t['clips'] for t in json.load(sys.stdin)['tracks'] if t.get('name')=='clipops')-1)")
g -d "{\"track_id\":$CO,\"index\":$KC,\"beat\":1.0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SplitNotesAtBeat >/dev/null
g -d "{\"track_id\":$CO,\"index\":$KC}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetClipNotes | python3 -c "
import json,sys
ns=sorted((n['pitch'],round(n.get('startBeat',0),3),round(n['lengthBeats'],3)) for n in json.load(sys.stdin)['notes'])
assert ns==[(60,0.0,1.0),(60,1.0,1.0),(64,1.0,1.0)], 'knife split wrong: %s'%ns
print('smoke: PASS — SplitNotesAtBeat cut the spanning note into halves (60@0/60@1), left 64@1')
" || { echo 'smoke: knife split wrong' >&2; exit 1; }
# Legato: three notes at 0/2/4 (length 1 each) stretch so each reaches the next onset —
# [0,2)+[2,4) and the last note unchanged. Verified via GetClipNotes.
g -d "{\"track_id\":$CO,\"start_beat\":0,\"length_beats\":6,\"content_len_beats\":6,\"looped\":false,\"notes\":[{\"pitch\":60,\"start_beat\":0,\"length_beats\":1,\"velocity\":0.8},{\"pitch\":62,\"start_beat\":2,\"length_beats\":1,\"velocity\":0.8},{\"pitch\":64,\"start_beat\":4,\"length_beats\":1,\"velocity\":0.8}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
LC=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetState | python3 -c "import json,sys;print(next(t['clips'] for t in json.load(sys.stdin)['tracks'] if t.get('name')=='clipops')-1)")
g -d "{\"track_id\":$CO,\"index\":$LC,\"amount\":1.0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/LegatoClip >/dev/null
g -d "{\"track_id\":$CO,\"index\":$LC}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetClipNotes | python3 -c "
import json,sys
ns=sorted((n['pitch'],round(n.get('startBeat',0),3),round(n['lengthBeats'],3)) for n in json.load(sys.stdin)['notes'])
assert ns==[(60,0.0,2.0),(62,2.0,2.0),(64,4.0,1.0)], 'legato lengths wrong: %s'%ns
print('smoke: PASS — LegatoClip stretched notes to the next onset (60->2, 62->2, last 64 unchanged)')
" || { echo 'smoke: legato wrong' >&2; exit 1; }
# Velocity ramp (crescendo): notes at onsets 0/2/4 ramp 0.3 -> 1.0 by start position.
g -d "{\"track_id\":$CO,\"start_beat\":0,\"length_beats\":6,\"content_len_beats\":6,\"looped\":false,\"notes\":[{\"pitch\":60,\"start_beat\":0,\"length_beats\":1,\"velocity\":0.5},{\"pitch\":62,\"start_beat\":2,\"length_beats\":1,\"velocity\":0.5},{\"pitch\":64,\"start_beat\":4,\"length_beats\":1,\"velocity\":0.5}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
VR=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetState | python3 -c "import json,sys;print(next(t['clips'] for t in json.load(sys.stdin)['tracks'] if t.get('name')=='clipops')-1)")
g -d "{\"track_id\":$CO,\"index\":$VR,\"from\":0.3,\"to\":1.0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RampClipVelocity >/dev/null
g -d "{\"track_id\":$CO,\"index\":$VR}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetClipNotes | python3 -c "
import json,sys
ns=sorted((n['pitch'],round(n.get('velocity',0),3)) for n in json.load(sys.stdin)['notes'])
assert ns==[(60,0.3),(62,0.65),(64,1.0)], 'velocity ramp wrong: %s'%ns
print('smoke: PASS — RampClipVelocity crescendo -> velocities '+str([v for _,v in ns]))
" || { echo 'smoke: velocity ramp wrong' >&2; exit 1; }
# Time-scale (double-time): factor 0.5 halves every note's start+length AND the clip's
# content/slot length. Notes 0/2/4 (len 1) -> 0/1/2 (len 0.5); clip length 6 -> 3.
g -d "{\"track_id\":$CO,\"start_beat\":0,\"length_beats\":6,\"content_len_beats\":6,\"looped\":false,\"notes\":[{\"pitch\":60,\"start_beat\":0,\"length_beats\":1,\"velocity\":0.8},{\"pitch\":62,\"start_beat\":2,\"length_beats\":1,\"velocity\":0.8},{\"pitch\":64,\"start_beat\":4,\"length_beats\":1,\"velocity\":0.8}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
TSC=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetState | python3 -c "import json,sys;print(next(t['clips'] for t in json.load(sys.stdin)['tracks'] if t.get('name')=='clipops')-1)")
g -d "{\"track_id\":$CO,\"index\":$TSC,\"factor\":0.5}" 127.0.0.1:$PORT gloopy.v1.Gloopy/ScaleClipTime >/dev/null
g -d "{\"track_id\":$CO,\"index\":$TSC}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetClipNotes | python3 -c "
import json,sys
ns=sorted((n['pitch'],round(n.get('startBeat',0),3),round(n['lengthBeats'],3)) for n in json.load(sys.stdin)['notes'])
assert ns==[(60,0.0,0.5),(62,1.0,0.5),(64,2.0,0.5)], 'time-scale note times wrong: %s'%ns
print('smoke: PASS — ScaleClipTime 0.5 (double-time) halved note starts/lengths (0/1/2, len 0.5)')
" || { echo 'smoke: time-scale notes wrong' >&2; exit 1; }
g -d "{\"path\":\"$WORK/ts.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SaveProject >/dev/null
python3 -c "
import xml.etree.ElementTree as ET
cl=list(ET.parse('$WORK/ts.gloopy').getroot().iter('CLIP'))[-1]
assert abs(float(cl.get('len'))-3.0)<1e-6, 'clip slot length not halved: %s'%cl.get('len')
assert abs(float(cl.get('content'))-3.0)<1e-6, 'content length not halved: %s'%cl.get('content')
print('smoke: PASS — ScaleClipTime halved the clip slot + content length (6 -> 3)')
" || { echo 'smoke: time-scale clip length wrong' >&2; exit 1; }
# MIDI echo: one note (vel 0.8) + echo delay 0.5, 3 repeats, feedback 0.5 -> decaying copies
# at 0.5/1.0/1.5 with vel 0.4/0.2/0.1. The original is kept.
g -d "{\"track_id\":$CO,\"start_beat\":0,\"length_beats\":8,\"content_len_beats\":8,\"looped\":false,\"notes\":[{\"pitch\":60,\"start_beat\":0,\"length_beats\":0.5,\"velocity\":0.8}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
ECC=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetState | python3 -c "import json,sys;print(next(t['clips'] for t in json.load(sys.stdin)['tracks'] if t.get('name')=='clipops')-1)")
g -d "{\"track_id\":$CO,\"index\":$ECC,\"delay_beats\":0.5,\"repeats\":3,\"feedback\":0.5}" 127.0.0.1:$PORT gloopy.v1.Gloopy/EchoClip >/dev/null
g -d "{\"track_id\":$CO,\"index\":$ECC}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetClipNotes | python3 -c "
import json,sys
ns=sorted((round(n.get('startBeat',0),3),round(n.get('velocity',0),3)) for n in json.load(sys.stdin)['notes'])
assert ns==[(0.0,0.8),(0.5,0.4),(1.0,0.2),(1.5,0.1)], 'echo notes wrong: %s'%ns
print('smoke: PASS — EchoClip appended 3 decaying repeats (0.5/1.0/1.5 @ 0.4/0.2/0.1), original kept')
" || { echo 'smoke: MIDI echo wrong' >&2; exit 1; }
# Melodic inversion: pitches mirror around the earliest note (60) -> 60/56/53; timing kept.
g -d "{\"track_id\":$CO,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"looped\":false,\"notes\":[{\"pitch\":60,\"start_beat\":0,\"length_beats\":1,\"velocity\":0.8},{\"pitch\":64,\"start_beat\":1,\"length_beats\":1,\"velocity\":0.8},{\"pitch\":67,\"start_beat\":2,\"length_beats\":1,\"velocity\":0.8}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
IVC=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetState | python3 -c "import json,sys;print(next(t['clips'] for t in json.load(sys.stdin)['tracks'] if t.get('name')=='clipops')-1)")
g -d "{\"track_id\":$CO,\"index\":$IVC}" 127.0.0.1:$PORT gloopy.v1.Gloopy/InvertClip >/dev/null
g -d "{\"track_id\":$CO,\"index\":$IVC}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetClipNotes | python3 -c "
import json,sys
ns=sorted((round(n.get('startBeat',0),3),n['pitch']) for n in json.load(sys.stdin)['notes'])
assert ns==[(0.0,60),(1.0,56),(2.0,53)], 'inversion wrong: %s'%ns
print('smoke: PASS — InvertClip mirrored pitches around 60 -> 60/56/53 (timing kept)')
" || { echo 'smoke: melodic inversion wrong' >&2; exit 1; }
# Ratchet: a 1-beat note x4 -> 4 same-pitch hits of 0.25 at 0/0.25/0.5/0.75.
g -d "{\"track_id\":$CO,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"looped\":false,\"notes\":[{\"pitch\":60,\"start_beat\":0,\"length_beats\":1,\"velocity\":0.8}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
RTC=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetState | python3 -c "import json,sys;print(next(t['clips'] for t in json.load(sys.stdin)['tracks'] if t.get('name')=='clipops')-1)")
g -d "{\"track_id\":$CO,\"index\":$RTC,\"subdivisions\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RatchetClip >/dev/null
g -d "{\"track_id\":$CO,\"index\":$RTC}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetClipNotes | python3 -c "
import json,sys
ns=sorted((round(n.get('startBeat',0),3),n['pitch'],round(n['lengthBeats'],3)) for n in json.load(sys.stdin)['notes'])
assert ns==[(0.0,60,0.25),(0.25,60,0.25),(0.5,60,0.25),(0.75,60,0.25)], 'ratchet wrong: %s'%ns
print('smoke: PASS — RatchetClip x4 subdivided the note into 4 hits (0/0.25/0.5/0.75, len 0.25)')
" || { echo 'smoke: ratchet wrong' >&2; exit 1; }
# Harmonize: a chord {60,64} + a fifth (+7) -> {60,64,67,71} at beat 0 (originals kept).
g -d "{\"track_id\":$CO,\"start_beat\":0,\"length_beats\":1,\"content_len_beats\":1,\"looped\":false,\"notes\":[{\"pitch\":60,\"start_beat\":0,\"length_beats\":1,\"velocity\":0.8},{\"pitch\":64,\"start_beat\":0,\"length_beats\":1,\"velocity\":0.8}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
HMC=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetState | python3 -c "import json,sys;print(next(t['clips'] for t in json.load(sys.stdin)['tracks'] if t.get('name')=='clipops')-1)")
g -d "{\"track_id\":$CO,\"index\":$HMC,\"semitones\":7}" 127.0.0.1:$PORT gloopy.v1.Gloopy/HarmonizeClip >/dev/null
g -d "{\"track_id\":$CO,\"index\":$HMC}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetClipNotes | python3 -c "
import json,sys
ps=sorted(n['pitch'] for n in json.load(sys.stdin)['notes'])
assert ps==[60,64,67,71], 'harmonize wrong: %s'%ps
print('smoke: PASS — HarmonizeClip +7 added a parallel fifth to each note (60/64 -> 60/64/67/71)')
" || { echo 'smoke: harmonize wrong' >&2; exit 1; }
# Swing: notes at 0/0.5/1/1.5 with grid 0.5 amount 0.33 -> off-beats (0.5,1.5) shift +0.165.
g -d "{\"track_id\":$CO,\"start_beat\":0,\"length_beats\":2,\"content_len_beats\":2,\"looped\":false,\"notes\":[{\"pitch\":60,\"start_beat\":0,\"length_beats\":0.5,\"velocity\":0.8},{\"pitch\":62,\"start_beat\":0.5,\"length_beats\":0.5,\"velocity\":0.8},{\"pitch\":64,\"start_beat\":1.0,\"length_beats\":0.5,\"velocity\":0.8},{\"pitch\":65,\"start_beat\":1.5,\"length_beats\":0.5,\"velocity\":0.8}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
SWC=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetState | python3 -c "import json,sys;print(next(t['clips'] for t in json.load(sys.stdin)['tracks'] if t.get('name')=='clipops')-1)")
g -d "{\"track_id\":$CO,\"index\":$SWC,\"grid_beats\":0.5,\"amount\":0.33}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SwingClip >/dev/null
g -d "{\"track_id\":$CO,\"index\":$SWC}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetClipNotes | python3 -c "
import json,sys
ns=sorted((round(n.get('startBeat',0),4),n['pitch']) for n in json.load(sys.stdin)['notes'])
assert ns==[(0.0,60),(0.665,62),(1.0,64),(1.665,65)], 'swing wrong: %s'%ns
print('smoke: PASS — SwingClip 1/8 0.33 delayed off-beats (0.5/1.5 -> 0.665/1.665), on-beats kept')
" || { echo 'smoke: swing wrong' >&2; exit 1; }
g -d "{\"id\":$CO}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null   # isolate: drop the scratch track
# Split-at-named-marker: a marker at beat 2 splits a [0,4) clip (notes 0/1/2/3) into
# [0,2)+[2,4); the right clip's notes rebase to 0/1. Reuses the locations model.
SM=$(g -d '{"name":"markertk","wave":"SAW"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$SM,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"looped\":false,\"notes\":[{\"pitch\":60,\"start_beat\":0,\"length_beats\":0.5,\"velocity\":0.8},{\"pitch\":62,\"start_beat\":1,\"length_beats\":0.5,\"velocity\":0.8},{\"pitch\":64,\"start_beat\":2,\"length_beats\":0.5,\"velocity\":0.8},{\"pitch\":65,\"start_beat\":3,\"length_beats\":0.5,\"velocity\":0.8}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
g -d '{"name":"chorus","kind":"marker","start_beat":2,"end_beat":2}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddLocation >/dev/null
NEWIDX=$(g -d "{\"track_id\":$SM,\"index\":0,\"marker\":\"chorus\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SplitClipAtMarker | python3 -c "import json,sys;print(json.load(sys.stdin).get('index',-1))")
[ "$NEWIDX" = 1 ] || { echo "smoke: SplitClipAtMarker did not return the new clip (got $NEWIDX)" >&2; exit 1; }
NC=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetState | python3 -c "import json,sys;print(next(t['clips'] for t in json.load(sys.stdin)['tracks'] if t['id']==$SM))")
[ "$NC" = 2 ] || { echo "smoke: SplitClipAtMarker wrong clip count ($NC, expected 2)" >&2; exit 1; }
g -d "{\"track_id\":$SM,\"index\":1}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetClipNotes | python3 -c "
import json,sys
ns=sorted((round(n.get('startBeat',0),3),n['pitch']) for n in json.load(sys.stdin)['notes'])
assert ns==[(0.0,64),(1.0,65)], 'right clip notes wrong after marker split: %s'%ns
print('smoke: PASS — SplitClipAtMarker split [0,4) at marker \"chorus\"(2) -> 2 clips, right=64@0/65@1')
" || { echo 'smoke: SplitClipAtMarker notes wrong' >&2; exit 1; }
g -d '{"name":"chorus"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveLocation >/dev/null 2>&1 || true
g -d "{\"id\":$SM}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null
# Audio-clip split is transparent: splitting an audio clip must trim each half's buffer
# so the right half continues from the cut (not replay the buffer from its start). Render
# a tone with two DISTINCT halves to a WAV, load it, split at beat 2, and assert the whole
# render is unchanged by the split (a replay bug would make the second half wrong).
SP=$(g -d '{"name":"spsrc","wave":"SAW","attack":0.01,"decay":0.1,"sustain":0.8,"release":0.2,"gain":0.9}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$SP,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"notes\":[{\"pitch\":48,\"start_beat\":0,\"length_beats\":2,\"velocity\":0.9},{\"pitch\":72,\"start_beat\":2,\"length_beats\":2,\"velocity\":0.9}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
g -d "{\"path\":\"$WORK/spsrc.wav\",\"tail_seconds\":0,\"start_beat\":0,\"end_beat\":4,\"track_id\":$SP}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"id\":$SP}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null
SPT=$(g -d '{"name":"spaud"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddAudioTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$SPT,\"start_beat\":0,\"path\":\"$WORK/spsrc.wav\",\"gain\":1.0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddAudioClip >/dev/null
g -d "{\"path\":\"$WORK/sp_before.wav\",\"tail_seconds\":0,\"start_beat\":0,\"end_beat\":4,\"track_id\":$SPT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
SPNI=$(g -d "{\"track_id\":$SPT,\"index\":0,\"beat\":2}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SplitClip | python3 -c "import json,sys;print(json.load(sys.stdin).get('index',-1))")
[ "$SPNI" = 1 ] || { echo "smoke: audio SplitClip did not create the right clip (got $SPNI)" >&2; exit 1; }
g -d "{\"path\":\"$WORK/sp_after.wav\",\"tail_seconds\":0,\"start_beat\":0,\"end_beat\":4,\"track_id\":$SPT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
python3 -c "
import wave
def rd(p):
    w=wave.open(p);f=w.readframes(w.getnframes());return [int.from_bytes(f[i:i+3],'little',signed=True) for i in range(0,len(f),3)]
a=rd('$WORK/sp_before.wav');b=rd('$WORK/sp_after.wav');m=min(len(a),len(b))
d=sum(abs(a[i]-b[i]) for i in range(m))/m/(1<<23)
assert d < 0.001, 'audio split is not transparent (mean abs diff %.5f) — right half replays the buffer'%d
print('smoke: PASS — audio SplitClip is transparent (halves trimmed, diff %.6f)'%d)
" || { echo 'smoke: audio split not transparent' >&2; exit 1; }
g -d "{\"id\":$SPT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null
# Slice at transients: 4 staccato hits (short notes with silence between) rendered to a
# WAV and loaded as one audio clip should slice into ~4 clips at the detected onsets.
# (The onset math itself is unit-tested in GloopyTests::NoteEdits.)
TR=$(g -d '{"name":"trsrc","wave":"SAW","attack":0.001,"decay":0.05,"sustain":0,"release":0.02,"gain":0.9}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$TR,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"notes\":[{\"pitch\":48,\"start_beat\":0,\"length_beats\":0.15,\"velocity\":0.95},{\"pitch\":48,\"start_beat\":1,\"length_beats\":0.15,\"velocity\":0.95},{\"pitch\":48,\"start_beat\":2,\"length_beats\":0.15,\"velocity\":0.95},{\"pitch\":48,\"start_beat\":3,\"length_beats\":0.15,\"velocity\":0.95}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
g -d "{\"path\":\"$WORK/trsrc.wav\",\"tail_seconds\":0,\"start_beat\":0,\"end_beat\":4,\"track_id\":$TR}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"id\":$TR}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null
TRT=$(g -d '{"name":"traud"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddAudioTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$TRT,\"start_beat\":0,\"path\":\"$WORK/trsrc.wav\",\"gain\":1.0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddAudioClip >/dev/null
SLICES=$(g -d "{\"track_id\":$TRT,\"index\":0,\"sensitivity\":1.0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SliceAtTransients | python3 -c "import json,sys;print(json.load(sys.stdin).get('slices',0))")
NCL=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetState | python3 -c "import json,sys;print(next(t['clips'] for t in json.load(sys.stdin)['tracks'] if t['id']==$TRT))")
python3 -c "
s,n=$SLICES,$NCL
assert 3 <= s <= 5, 'expected ~4 slices from 4 hits, got %d'%s
assert n==s, 'clip count (%d) != reported slices (%d)'%(n,s)
print('smoke: PASS — SliceAtTransients cut 4 hits into %d slices'%s)
" || { echo 'smoke: slice-at-transients wrong' >&2; exit 1; }
g -d "{\"id\":$TRT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null
# Per-clip mute: a muted MIDI clip is skipped by the collector, so a soloed render goes
# silent; unmuting restores it. (MIDI collectClip previously ignored clip.muted.)
MU=$(g -d '{"name":"mutetk","wave":"SAW","attack":0.01,"decay":0.1,"sustain":0.8,"release":0.2,"gain":0.9}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$MU,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"notes\":[{\"pitch\":60,\"start_beat\":0,\"length_beats\":4,\"velocity\":0.9}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
mupk() { g -d "{\"path\":\"$1\",\"tail_seconds\":0,\"end_beat\":4,\"track_id\":$MU}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null; g -d "{\"path\":\"$1\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AnalyzeFile | python3 -c "import json,sys;print(json.load(sys.stdin).get('peakDbfs',-120))"; }
MON=$(mupk "$WORK/mu_on.wav")
g -d "{\"track_id\":$MU,\"index\":0,\"muted\":true}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetClipMuted >/dev/null
MMUTE=$(mupk "$WORK/mu_mute.wav")
g -d "{\"track_id\":$MU,\"index\":0,\"muted\":false}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetClipMuted >/dev/null
MBACK=$(mupk "$WORK/mu_back.wav")
python3 -c "
on,mute,back=float('$MON'),float('$MMUTE'),float('$MBACK')
assert on>-40, 'clip should sound before mute (%.1f dBFS)'%on
assert mute<-80, 'muted clip should be silent (%.1f dBFS)'%mute
assert back>-40, 'unmuted clip should sound again (%.1f dBFS)'%back
print('smoke: PASS — clip mute silences a MIDI clip (%.0f -> %.0f -> %.0f dBFS)'%(on,mute,back))
" || { echo 'smoke: clip mute wrong' >&2; exit 1; }
g -d "{\"id\":$MU}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null
# Repeat clip: tile a 2-beat clip with 3 butted copies -> 4 clips at beats 0/2/4/6.
RC=$(g -d '{"name":"reptk","wave":"SAW"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$RC,\"start_beat\":0,\"length_beats\":2,\"content_len_beats\":2,\"looped\":false,\"notes\":[{\"pitch\":60,\"start_beat\":0,\"length_beats\":0.5,\"velocity\":0.8}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
ADDED=$(g -d "{\"track_id\":$RC,\"index\":0,\"copies\":3}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RepeatClip | python3 -c "import json,sys;print(json.load(sys.stdin).get('slices',0))")
[ "$ADDED" = 3 ] || { echo "smoke: RepeatClip added wrong count ($ADDED, expected 3)" >&2; exit 1; }
g -d "{\"path\":\"$WORK/rep.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SaveProject >/dev/null
python3 -c "
import xml.etree.ElementTree as ET
r=ET.parse('$WORK/rep.gloopy').getroot()
tr=[t for t in r.iter('TRACK') if t.get('name')=='reptk'][0]
starts=sorted(round(float(c.get('start')),3) for c in tr.iter('CLIP'))
assert starts==[0.0,2.0,4.0,6.0], 'repeat tiled to wrong beats: %s'%starts
print('smoke: PASS — RepeatClip tiled a 2-beat clip to 4 (starts 0/2/4/6)')
" || { echo 'smoke: RepeatClip tiled wrong' >&2; exit 1; }
# Loop-to-clip: set the transport loop to a clip's [start,end); GetTransport now reports
# the loop region (new fields), so we can assert it matches the clip.
g -d "{\"track_id\":$RC,\"start_beat\":5,\"length_beats\":3,\"content_len_beats\":3,\"notes\":[]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
LCI=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetState | python3 -c "import json,sys;print(next(t['clips'] for t in json.load(sys.stdin)['tracks'] if t['id']==$RC)-1)")
g -d "{\"track_id\":$RC,\"index\":$LCI}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetLoopToClip >/dev/null
g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetTransport | python3 -c "
import json,sys
t=json.load(sys.stdin)
assert t.get('loopEnabled') is True, 'loop not enabled after SetLoopToClip: %s'%t
assert abs(t.get('loopStart',0)-5)<1e-6 and abs(t.get('loopEnd',0)-8)<1e-6, 'loop region wrong: %s'%t
print('smoke: PASS — SetLoopToClip set the transport loop to the clip [5,8)')
" || { echo 'smoke: SetLoopToClip wrong' >&2; exit 1; }
g -d '{"enabled":false,"start_beat":0,"end_beat":0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetLoop >/dev/null   # restore
g -d "{\"id\":$RC}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null

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
# Noise Gate (new effect): a percussive note (loud attack, quiet sustain) through a gate
# with the threshold set between them — the loud attack passes at unity while the quiet
# sustain tail is gated toward the Range floor. So vs the ungated render the HEAD window is
# ~unchanged and the TAIL window drops steeply.
NG=$(g -d '{"name":"gatetest","wave":"SAW","attack":0.003,"decay":0.12,"sustain":0.05,"release":0.05,"gain":0.9}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$NG,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"looped\":false,\"notes\":[{\"pitch\":57,\"start_beat\":0,\"length_beats\":4,\"velocity\":0.95}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
g -d "{\"path\":\"$WORK/gate_dry.wav\",\"tail_seconds\":0,\"end_beat\":4,\"track_id\":$NG}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d '{"insert":0,"type":"NOISE_GATE"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddEffect >/dev/null
g -d '{"insert":0,"slot":0,"name":"Thresh dB","value":-18}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d '{"insert":0,"slot":0,"name":"Range dB","value":-60}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d '{"insert":0,"slot":0,"name":"Attack ms","value":0.5}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d '{"insert":0,"slot":0,"name":"Release ms","value":30}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d "{\"path\":\"$WORK/gate_on.wav\",\"tail_seconds\":0,\"end_beat\":4,\"track_id\":$NG}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
python3 -c "
import wave,math
def rd(p):
    w=wave.open(p);f=w.readframes(w.getnframes());return [int.from_bytes(f[i:i+3],'little',signed=True) for i in range(0,len(f),3)]
def rms(x): return (sum(v*v for v in x)/max(1,len(x)))**0.5
def db(x): return 20*math.log10(max(1e-9,x))
dry=rd('$WORK/gate_dry.wav');on=rd('$WORK/gate_on.wav');n=min(len(dry),len(on))
head=slice(int(0.01*n),int(0.06*n))          # loud attack region (skip the first opening ramp)
tail=slice(int(0.6*n),n)                      # sustained quiet tail
hd=db(rms(on[head]))-db(rms(dry[head])); td=db(rms(on[tail]))-db(rms(dry[tail]))
assert hd > -4.0, 'gate attenuated the loud attack too much (head %.1f dB)'%hd
assert td < -12.0, 'gate did not close on the quiet tail (tail %.1f dB, wanted <-12)'%td
print('smoke: PASS — Noise Gate: loud attack passes (head %+.1f dB), quiet tail gated (tail %+.1f dB)'%(hd,td))
" || { echo 'smoke: noise gate wrong' >&2; exit 1; }
g -d '{"insert":0,"slot":0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveEffect >/dev/null
g -d "{\"id\":$NG}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null
# Auto-wah (new effect): an envelope-following low-pass. A saw's zero-crossing rate is
# level-INDEPENDENT, so a static filter would filter a loud and a quiet copy identically.
# The auto-wah opens its cutoff with the input envelope, so a LOUD tone renders brighter
# (higher ZCR) than a QUIET one — a direct proof the envelope drives the filter. Two synth
# tracks at different gains, auto-wah on master; also reproducible (reset()).
AWL=$(g -d '{"name":"awloud","wave":"SAW","attack":0.005,"decay":0.05,"sustain":0.95,"release":0.05,"gain":0.95}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$AWL,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"notes\":[{\"pitch\":45,\"start_beat\":0,\"length_beats\":4,\"velocity\":0.95}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
AWQ=$(g -d '{"name":"awquiet","wave":"SAW","attack":0.005,"decay":0.05,"sustain":0.95,"release":0.05,"gain":0.12}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$AWQ,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"notes\":[{\"pitch\":45,\"start_beat\":0,\"length_beats\":4,\"velocity\":0.95}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
g -d '{"insert":0,"type":"AUTOWAH"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddEffect >/dev/null
g -d '{"insert":0,"slot":0,"name":"Base Hz","value":250}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d '{"insert":0,"slot":0,"name":"Range oct","value":4}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d "{\"path\":\"$WORK/aw_loud.wav\",\"tail_seconds\":0,\"end_beat\":4,\"track_id\":$AWL}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"path\":\"$WORK/aw_loud2.wav\",\"tail_seconds\":0,\"end_beat\":4,\"track_id\":$AWL}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"path\":\"$WORK/aw_quiet.wav\",\"tail_seconds\":0,\"end_beat\":4,\"track_id\":$AWQ}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
ZL=$(zcr "$WORK/aw_loud.wav"); ZQ=$(zcr "$WORK/aw_quiet.wav")
python3 -c "
import wave
def raw(p):
    w=wave.open(p);return w.readframes(w.getnframes())
zl,zq=int('$ZL'),int('$ZQ')
assert zl > zq*1.3, 'auto-wah did not follow the envelope (loud ZCR %d not > quiet %d)'%(zl,zq)
assert raw('$WORK/aw_loud.wav')==raw('$WORK/aw_loud2.wav'), 'auto-wah not reproducible (re-render differs)'
print('smoke: PASS — Auto-wah follows the envelope: loud opens the filter brighter (ZCR %d vs quiet %d), reproducible'%(zl,zq))
" || { echo 'smoke: auto-wah wrong' >&2; exit 1; }
g -d '{"insert":0,"slot":0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveEffect >/dev/null
g -d "{\"id\":$AWL}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null
g -d "{\"id\":$AWQ}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null
# Ring mod (new effect): a LOW sine (~98 Hz) multiplied by a HIGH carrier (1500 Hz) shifts
# energy up to ~1400/1600 Hz, so the zero-crossing rate jumps far above the dry sine. Mix 0
# is a true identity; the render is reproducible.
RM=$(g -d '{"name":"rmtone","wave":"SINE","attack":0.005,"decay":0.05,"sustain":0.95,"release":0.05,"gain":0.9}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$RM,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"notes\":[{\"pitch\":43,\"start_beat\":0,\"length_beats\":4,\"velocity\":0.95}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
g -d "{\"path\":\"$WORK/rm_dry.wav\",\"tail_seconds\":0,\"end_beat\":4,\"track_id\":$RM}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d '{"insert":0,"type":"RINGMOD"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddEffect >/dev/null
g -d '{"insert":0,"slot":0,"name":"Freq","value":1500}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d '{"insert":0,"slot":0,"name":"Mix","value":0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d "{\"path\":\"$WORK/rm_mix0.wav\",\"tail_seconds\":0,\"end_beat\":4,\"track_id\":$RM}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d '{"insert":0,"slot":0,"name":"Mix","value":1.0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d "{\"path\":\"$WORK/rm_wet.wav\",\"tail_seconds\":0,\"end_beat\":4,\"track_id\":$RM}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"path\":\"$WORK/rm_wet2.wav\",\"tail_seconds\":0,\"end_beat\":4,\"track_id\":$RM}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
ZDRY=$(zcr "$WORK/rm_dry.wav"); ZWET=$(zcr "$WORK/rm_wet.wav")
python3 -c "
import wave
def raw(p):
    w=wave.open(p);return w.readframes(w.getnframes())
zd,zw=int('$ZDRY'),int('$ZWET')
assert raw('$WORK/rm_mix0.wav')==raw('$WORK/rm_dry.wav'), 'ring mod Mix 0 is not identity'
assert zw > zd*2, 'ring mod did not shift energy up (wet ZCR %d not > 2x dry %d)'%(zw,zd)
assert raw('$WORK/rm_wet.wav')==raw('$WORK/rm_wet2.wav'), 'ring mod not reproducible (re-render differs)'
print('smoke: PASS — Ring Mod: Mix0=identity, a 1500 Hz carrier shifts a low sine up (ZCR %d vs dry %d), reproducible'%(zw,zd))
" || { echo 'smoke: ring mod wrong' >&2; exit 1; }
g -d '{"insert":0,"slot":0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveEffect >/dev/null
g -d "{\"id\":$RM}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null
# Tremolo (new effect): a sine LFO scales master gain. Depth 0 is a true identity; depth 1
# on a sustained tone makes the amplitude oscillate (trough near silence, so windowed RMS
# swings hugely); and a tempo-synced 1-beat cycle matches a free LFO at bpm/60 Hz exactly.
TRB=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetTransport | python3 -c "import json,sys;print(json.load(sys.stdin).get('bpm',120))")
TRT=$(g -d '{"name":"tremtone","wave":"SAW","attack":0.005,"decay":0.05,"sustain":0.95,"release":0.05,"gain":0.8}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$TRT,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"looped\":false,\"notes\":[{\"pitch\":57,\"start_beat\":0,\"length_beats\":4,\"velocity\":0.9}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
g -d "{\"path\":\"$WORK/trem_base.wav\",\"tail_seconds\":0,\"end_beat\":4,\"track_id\":$TRT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d '{"insert":0,"type":"TREMOLO"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddEffect >/dev/null
g -d '{"insert":0,"slot":0,"name":"Depth","value":0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d "{\"path\":\"$WORK/trem_d0.wav\",\"tail_seconds\":0,\"end_beat\":4,\"track_id\":$TRT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d '{"insert":0,"slot":0,"name":"Depth","value":1.0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d '{"insert":0,"slot":0,"name":"Rate","value":0.5}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null   # ~1 cycle over the render -> windows resolve peak vs near-silent trough
g -d "{\"path\":\"$WORK/trem_d1.wav\",\"tail_seconds\":0,\"end_beat\":4,\"track_id\":$TRT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
# tempo-sync exactness: 1-beat cycle == free LFO at bpm/60 Hz.
FREERATE=$(python3 -c "print(float('$TRB')/60.0)")
g -d "{\"insert\":0,\"slot\":0,\"name\":\"Sync bt\",\"value\":1.0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d "{\"path\":\"$WORK/trem_sync.wav\",\"tail_seconds\":0,\"end_beat\":4,\"track_id\":$TRT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d '{"insert":0,"slot":0,"name":"Sync bt","value":0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d "{\"insert\":0,\"slot\":0,\"name\":\"Rate\",\"value\":$FREERATE}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d "{\"path\":\"$WORK/trem_free.wav\",\"tail_seconds\":0,\"end_beat\":4,\"track_id\":$TRT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
python3 -c "
import wave
def rd(p):
    w=wave.open(p);f=w.readframes(w.getnframes());return [int.from_bytes(f[i:i+3],'little',signed=True) for i in range(0,len(f),3)]
def rms(x): return (sum(v*v for v in x)/max(1,len(x)))**0.5
base=rd('$WORK/trem_base.wav');d0=rd('$WORK/trem_d0.wav');d1=rd('$WORK/trem_d1.wav')
sync=rd('$WORK/trem_sync.wav');free=rd('$WORK/trem_free.wav')
assert d0==base, 'Tremolo depth 0 is not identity (differs from the dry render)'
n=min(len(base),len(d1)); W=16; w=n//W
wr=[rms(d1[k*w:(k+1)*w]) for k in range(W)]
assert max(wr) > 4*max(1.0,min(wr)), 'depth-1 tremolo did not swing the amplitude (max/min window RMS %.0f/%.0f)'%(max(wr),min(wr))
m=min(len(sync),len(free))
assert sync[:m]==free[:m], 'tempo-synced 1-beat != free bpm/60 Hz (sync law mismatch)'
print('smoke: PASS — Tremolo: depth0=identity, depth1 swings RMS %.0f..%.0f, sync(1bt)==free(%.3fHz)'%(min(wr),max(wr),float('$FREERATE')))
" || { echo 'smoke: tremolo wrong' >&2; exit 1; }
g -d '{"insert":0,"slot":0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveEffect >/dev/null
# Auto-pan (new effect): a sine LFO drives L/R in antiphase. Depth 0 is identity; depth 1
# on a centre-panned tone (L==R) makes the two channels diverge — across the render there
# is a window where L hard-pans (L >> R) AND one where R hard-pans (R >> L). A tempo-synced
# 1-beat cycle matches a free LFO at bpm/60 Hz exactly. Reuses the $TRT tone track.
g -d "{\"path\":\"$WORK/ap_base.wav\",\"tail_seconds\":0,\"end_beat\":4,\"track_id\":$TRT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d '{"insert":0,"type":"AUTOPAN"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddEffect >/dev/null
g -d '{"insert":0,"slot":0,"name":"Depth","value":0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d "{\"path\":\"$WORK/ap_d0.wav\",\"tail_seconds\":0,\"end_beat\":4,\"track_id\":$TRT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d '{"insert":0,"slot":0,"name":"Depth","value":1.0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d '{"insert":0,"slot":0,"name":"Rate","value":0.5}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d "{\"path\":\"$WORK/ap_d1.wav\",\"tail_seconds\":0,\"end_beat\":4,\"track_id\":$TRT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d '{"insert":0,"slot":0,"name":"Sync bt","value":1.0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d "{\"path\":\"$WORK/ap_sync.wav\",\"tail_seconds\":0,\"end_beat\":4,\"track_id\":$TRT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d '{"insert":0,"slot":0,"name":"Sync bt","value":0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d "{\"insert\":0,\"slot\":0,\"name\":\"Rate\",\"value\":$FREERATE}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d "{\"path\":\"$WORK/ap_free.wav\",\"tail_seconds\":0,\"end_beat\":4,\"track_id\":$TRT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
python3 -c "
import wave
def chans(p):
    w=wave.open(p);f=w.readframes(w.getnframes())
    L=[int.from_bytes(f[i:i+3],'little',signed=True) for i in range(0,len(f),6)]
    R=[int.from_bytes(f[i+3:i+6],'little',signed=True) for i in range(0,len(f),6)]
    return L,R
def raw(p):
    w=wave.open(p);return w.readframes(w.getnframes())
def rms(x): return (sum(v*v for v in x)/max(1,len(x)))**0.5
assert raw('$WORK/ap_d0.wav')==raw('$WORK/ap_base.wav'), 'Auto-pan depth 0 is not identity'
L,R=chans('$WORK/ap_d1.wav'); n=min(len(L),len(R)); W=16; w=n//W
lgtr=rgtl=False
for k in range(W):
    rl=rms(L[k*w:(k+1)*w]); rr=rms(R[k*w:(k+1)*w])
    if rl>3*max(1.0,rr): lgtr=True       # a window where L hard-pans
    if rr>3*max(1.0,rl): rgtl=True       # a window where R hard-pans
assert lgtr and rgtl, 'auto-pan did not sweep L<->R (L-dominant window=%s, R-dominant window=%s)'%(lgtr,rgtl)
assert raw('$WORK/ap_sync.wav')==raw('$WORK/ap_free.wav'), 'auto-pan synced(1bt) != free(bpm/60) — sync law mismatch'
print('smoke: PASS — Auto-pan: depth0=identity, depth1 sweeps L<->R (both hard-pan extremes), sync(1bt)==free(%.3fHz)'%float('$FREERATE'))
" || { echo 'smoke: auto-pan wrong' >&2; exit 1; }
g -d '{"insert":0,"slot":0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveEffect >/dev/null
g -d "{\"id\":$TRT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null
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
# 3-band EQ shelves: with the mid band flat, a low-shelf boost vs cut and a high-shelf boost
# vs cut each shift RMS. Add a bright saw track (high note + open cutoff) so the master has
# both low and high content for the shelves to act on; remove it afterwards.
BR=$(g -d '{"name":"bright","wave":"SAW","attack":0.01,"decay":0.1,"sustain":0.9,"release":0.2,"gain":0.7}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"id\":\"track/$BR/synth/cutoff\",\"value\":16000}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetParameter >/dev/null
g -d "{\"track_id\":$BR,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"notes\":[{\"pitch\":93,\"start_beat\":0,\"length_beats\":4,\"velocity\":0.9}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
g -d '{"insert":0,"slot":0,"name":"Gain dB","value":0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d '{"insert":0,"slot":0,"name":"Low Freq","value":250}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d '{"insert":0,"slot":0,"name":"Low dB","value":18}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d "{\"path\":\"$WORK/eq_lb.wav\",\"tail_seconds\":0.5,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
LB=$(arms "$WORK/eq_lb.wav")
g -d '{"insert":0,"slot":0,"name":"Low dB","value":-18}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d "{\"path\":\"$WORK/eq_lc.wav\",\"tail_seconds\":0.5,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
LC=$(arms "$WORK/eq_lc.wav")
g -d '{"insert":0,"slot":0,"name":"Low dB","value":0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d '{"insert":0,"slot":0,"name":"High Freq","value":5000}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d '{"insert":0,"slot":0,"name":"High dB","value":18}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d "{\"path\":\"$WORK/eq_hb.wav\",\"tail_seconds\":0.5,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
HB=$(arms "$WORK/eq_hb.wav")
g -d '{"insert":0,"slot":0,"name":"High dB","value":-18}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d "{\"path\":\"$WORK/eq_hc.wav\",\"tail_seconds\":0.5,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
HC=$(arms "$WORK/eq_hc.wav")
python3 -c "
assert float('$LB') > float('$LC')+2, 'low shelf boost/cut did not shift RMS (%s vs %s)'%('$LB','$LC')
assert float('$HB') > float('$HC')+2, 'high shelf boost/cut did not shift RMS (%s vs %s)'%('$HB','$HC')
print('smoke: PASS — 3-band EQ shelves shift RMS (low %s vs %s, high %s vs %s dBFS)'%('$LB','$LC','$HB','$HC'))
" || { echo 'smoke: EQ shelves wrong' >&2; exit 1; }
g -d '{"insert":0,"slot":0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveEffect >/dev/null
g -d "{\"id\":$BR}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null
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
# Phaser (PHASER enum -> factory -> params): the swept allpass notches change the waveform
# vs dry, while staying roughly level-matched (allpass magnitude preservation + mix).
g -d "{\"path\":\"$WORK/ph_dry.wav\",\"tail_seconds\":0.5,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d '{"insert":0,"type":"PHASER"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddEffect >/dev/null
g -d '{"insert":0,"slot":0,"name":"Mix","value":1.0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d '{"insert":0,"slot":0,"name":"Rate","value":1.0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d '{"insert":0,"slot":0,"name":"Feedbk","value":0.6}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d "{\"path\":\"$WORK/ph_wet.wav\",\"tail_seconds\":0.5,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
PHDRY=$(arms "$WORK/ph_dry.wav"); PHWET=$(arms "$WORK/ph_wet.wav")
python3 -c "
import wave
def rd(p):
    w=wave.open(p);f=w.readframes(w.getnframes());return [int.from_bytes(f[i:i+3],'little',signed=True) for i in range(0,len(f),3)]
a=rd('$WORK/ph_dry.wav');b=rd('$WORK/ph_wet.wav');m=min(len(a),len(b))
assert m>0 and any(a) and any(b), 'phaser render silent'
d=sum(abs(a[i]-b[i]) for i in range(m))/m/(1<<23)
assert d>0.003, 'phaser did not change the waveform vs dry (diff=%.5f)'%d
assert abs(float('$PHWET')-float('$PHDRY'))<6.0, 'phaser is not roughly level-matched (dry %s vs wet %s dBFS)'%('$PHDRY','$PHWET')
print('smoke: PASS — phaser sweeps notches (waveform diff %.4f, dry %s vs wet %s dBFS)'%(d,'$PHDRY','$PHWET'))
" || { echo 'smoke: phaser wrong' >&2; exit 1; }
g -d '{"insert":0,"slot":0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveEffect >/dev/null
# Tempo-synced delay: a synced 1/4-note delay (Sync=1) at 120 BPM must match a free 500 ms
# delay, and changing the tempo to 240 BPM must change it (proving effects receive the tempo).
# Re-add a fresh delay per render so the delay line starts empty (feedback tails otherwise
# bleed across renders and make the comparison state-dependent).
render_delay() {  # $1=out  $2=extra-param-json (Time ms or Sync bt)
    g -d '{"insert":0,"type":"DELAY"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddEffect >/dev/null
    g -d '{"insert":0,"slot":0,"name":"Feedbk","value":0.4}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
    g -d '{"insert":0,"slot":0,"name":"Mix","value":0.5}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
    g -d "$2" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
    g -d "{\"path\":\"$1\",\"tail_seconds\":1.0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
    g -d '{"insert":0,"slot":0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveEffect >/dev/null
}
g -d '{"bpm":120}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetTempo >/dev/null
render_delay "$WORK/dly_free.wav" '{"insert":0,"slot":0,"name":"Time ms","value":500}'
render_delay "$WORK/dly_s120.wav" '{"insert":0,"slot":0,"name":"Sync bt","value":1}'
g -d '{"bpm":240}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetTempo >/dev/null
render_delay "$WORK/dly_s240.wav" '{"insert":0,"slot":0,"name":"Sync bt","value":1}'
g -d '{"bpm":120}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetTempo >/dev/null
python3 -c "
import wave
def rd(p):
    w=wave.open(p);f=w.readframes(w.getnframes());return [int.from_bytes(f[i:i+3],'little',signed=True) for i in range(0,len(f),3)]
free=rd('$WORK/dly_free.wav');s120=rd('$WORK/dly_s120.wav');s240=rd('$WORK/dly_s240.wav')
m=min(len(free),len(s120),len(s240))
def diff(a,b): return sum(abs(a[i]-b[i]) for i in range(m))/m/(1<<23)
assert any(free[:m]), 'delay render silent'
dsync=diff(free,s120); dtempo=diff(s120,s240)
assert dsync < 0.0005, 'synced 1/4 @120 did not match free 500ms (diff=%.5f)'%dsync
assert dtempo > 0.003, 'changing tempo did not change the synced delay (diff=%.5f)'%dtempo
print('smoke: PASS — tempo-synced delay: 1/4@120 == free 500ms (diff %.5f), tempo change alters it (diff %.4f)'%(dsync,dtempo))
" || { echo 'smoke: tempo-synced delay wrong' >&2; exit 1; }
# Tempo-synced modulation effects (Chorus/Flanger/Phaser). Now that effects are reset before
# each offline bounce (below), modulation-effect renders are bit-reproducible, so we prove
# tempo-sync exactly: at 120 BPM a synced Chorus (Sync bt = 1 -> one cycle per beat = 2 Hz)
# matches a free Chorus at Rate 2 Hz, and a re-render is byte-identical. Effect stays in
# place; each render resets it. The sync->rate math is also unit-tested (GloopyTests::EffectSync).
g -d '{"bpm":120}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetTempo >/dev/null
g -d '{"insert":0,"type":"CHORUS"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddEffect >/dev/null
g -d '{"insert":0,"slot":0,"name":"Mix","value":1.0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d '{"insert":0,"slot":0,"name":"Rate","value":2.0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d '{"insert":0,"slot":0,"name":"Sync bt","value":0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d "{\"path\":\"$WORK/ch_free.wav\",\"tail_seconds\":0.5,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d '{"insert":0,"slot":0,"name":"Sync bt","value":1}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d "{\"path\":\"$WORK/ch_sync.wav\",\"tail_seconds\":0.5,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"path\":\"$WORK/ch_sync2.wav\",\"tail_seconds\":0.5,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d '{"insert":0,"slot":0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveEffect >/dev/null
python3 -c "
import wave
def rd(p):
    w=wave.open(p);f=w.readframes(w.getnframes());return [int.from_bytes(f[i:i+3],'little',signed=True) for i in range(0,len(f),3)]
free=rd('$WORK/ch_free.wav');sync=rd('$WORK/ch_sync.wav');sync2=rd('$WORK/ch_sync2.wav')
m=min(len(free),len(sync),len(sync2))
def diff(a,b): return sum(abs(a[i]-b[i]) for i in range(m))/m/(1<<23)
assert any(free[:m]), 'chorus render silent'
assert sync==sync2, 'modulation-effect bounce is not reproducible (effects not reset before render)'
dm=diff(free,sync)
assert dm < 0.0005, 'synced 1-beat chorus @120 did not match free 2 Hz (diff=%.5f)'%dm
print('smoke: PASS — synced chorus Sync 1bt@120 == free 2Hz (diff %.5f) AND bounce is reproducible'%dm)
" || { echo 'smoke: tempo-synced modulation effect wrong' >&2; exit 1; }

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

# Flanger: same contract (Mix=0 bit-exact passthrough, Mix>0 active). Proves the
# FLANGER enum -> factory + Mix/Feedbk params (a resonant swept comb, unlike chorus).
g -d '{"insert":0,"type":"FLANGER"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddEffect >/dev/null
g -d '{"insert":0,"slot":0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetEffectParams | grep -q '"Feedbk"' \
    || { echo "smoke: Flanger has no Feedbk param" >&2; exit 1; }
g -d '{"insert":0,"slot":0,"name":"Mix","value":0}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d "{\"path\":\"$WORK/fl0.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d '{"insert":0,"slot":0,"name":"Mix","value":0.8}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetEffectParam >/dev/null
g -d "{\"path\":\"$WORK/flw.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
python3 - "$WORK/chdry.wav" "$WORK/fl0.wav" "$WORK/flw.wav" <<'PY'
import sys, wave
def s(p):
    w=wave.open(p); f=w.readframes(w.getnframes())
    return [int.from_bytes(f[i:i+3],'little',signed=True) for i in range(0,len(f),3)]
dry,f0,fw=map(s,sys.argv[1:4])
n=min(len(dry),len(f0),len(fw))
d0=sum(abs(dry[i]-f0[i]) for i in range(n))/n/(1<<23)
dw=sum(abs(dry[i]-fw[i]) for i in range(n))/n/(1<<23)
assert d0 < 1e-6, 'flanger Mix=0 is not a passthrough (mean|diff|=%.2e)'%d0
assert dw > 1e-3, 'flanger Mix=0.8 did not change the signal (mean|diff|=%.2e)'%dw
print('smoke: PASS — Flanger (Mix=0 passthrough %.1e, Mix=0.8 active %.3f)'%(d0,dw))
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
# ParamModel snapshot manifest: params.toml records non-plugin param id->value so external
# clients can discover the model from the repo. It must exist and list real param ids.
[ -f "$COMP/params.toml" ] || { echo "smoke: composition has no params.toml snapshot" >&2; exit 1; }
grep -qE 'id = "(track|insert)/[0-9]+/(volume|pan)"' "$COMP/params.toml" || { echo "smoke: params.toml snapshot has no recognisable param ids" >&2; exit 1; }
echo "smoke: PASS — composition param snapshot params.toml ($(grep -c '^\[\[params\]\]' "$COMP/params.toml") params)"
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
# CLI render + export-stems: headless bounce of the composition (prints the wav path) and
# one stem per instrument track (JSON list). Both must produce non-silent audio.
"$BIN" render "$COMP" "$WORK/cli_mix.wav" >/dev/null 2>&1
python3 -c "import wave;w=wave.open('$WORK/cli_mix.wav');f=w.readframes(w.getnframes());pk=max((abs(int.from_bytes(f[i:i+3],'little',signed=True)) for i in range(0,len(f),3)),default=0)/(1<<23);assert pk>0.02,'CLI render silent (%.3f)'%pk;print('smoke: PASS — CLI render bounced the mix (peak %.2f)'%pk)" \
    || { echo "smoke: CLI render produced no/silent audio" >&2; exit 1; }
"$BIN" export-stems "$COMP" "$WORK/cli_stems" 2>/dev/null | python3 -c "
import json,sys,wave
d=json.load(sys.stdin); stems=d.get('stems',[])
assert len(stems)>=1, 'export-stems emitted no stems'
for p in stems:
    f=wave.open(p).readframes(10**7); pk=max((abs(int.from_bytes(f[i:i+3],'little',signed=True)) for i in range(0,len(f),3)),default=0)/(1<<23)
    assert pk>0.02, 'stem %s is silent'%p
print('smoke: PASS — CLI export-stems wrote %d non-silent stem(s)'%len(stems))
" || { echo "smoke: CLI export-stems failed" >&2; exit 1; }
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
# MIDI export loop-expansion: a LOOPED clip (2-beat content, notes at 0/1, tiled over a 4-beat
# clip) must export its TILED notes (0/1/2/3), not just one content window, so the .mid matches
# playback. Fresh project (isolate) -> export -> fresh project -> import -> the 4 tiled notes.
g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/NewProject >/dev/null
LXT=$(g -d '{"name":"looptk","wave":"SAW"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$LXT,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":2,\"looped\":true,\"notes\":[{\"pitch\":60,\"start_beat\":0,\"length_beats\":0.5,\"velocity\":0.9},{\"pitch\":62,\"start_beat\":1,\"length_beats\":0.5,\"velocity\":0.9}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
g -d "{\"path\":\"$WORK/loop.mid\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/ExportMidi >/dev/null
g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/NewProject >/dev/null
g -d "{\"path\":\"$WORK/loop.mid\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/ImportMidi >/dev/null
LXTID=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetState | python3 -c "import json,sys;print([t['id'] for t in json.load(sys.stdin)['tracks']][0])")
g -d "{\"track_id\":$LXTID,\"index\":0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/GetClipNotes | python3 -c "
import json,sys
ns=sorted((round(n.get('startBeat',0),3),n['pitch']) for n in json.load(sys.stdin)['notes'])
assert ns==[(0.0,60),(1.0,62),(2.0,60),(3.0,62)], 'exported loop not tiled: %s'%ns
print('smoke: PASS — MIDI export tiled the looped clip (2-beat content over 4 beats -> notes 0/1/2/3)')
" || { echo 'smoke: MIDI export loop-expansion wrong' >&2; exit 1; }

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

# Momentary / short-term LUFS + LRA. Needs a >=3s render for the 3s short-term window:
# a sustained sine over 12 beats @120bpm = 6s. Steady loudness -> max momentary >=
# integrated, short-term ~ integrated, and a bounded LRA. Solo-rendered, cleaned up.
BPM0=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetState | python3 -c "import json,sys;print(json.load(sys.stdin).get('bpm',120))")
g -d '{"bpm":120}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetTempo >/dev/null
LT=$(g -d '{"name":"loudtest","wave":"SINE","attack":0.01,"decay":0.05,"sustain":0.95,"release":0.05,"gain":0.7}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$LT,\"start_beat\":0,\"length_beats\":12,\"notes\":[{\"pitch\":57,\"start_beat\":0,\"length_beats\":11.8,\"velocity\":0.9}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
g -d "{\"path\":\"$WORK/steady.wav\",\"tail_seconds\":0,\"end_beat\":12,\"track_id\":$LT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"path\":\"$WORK/steady.wav\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AnalyzeFile | python3 -c "
import json,sys
d=json.load(sys.stdin)
lu=d.get('lufs',0); mo=d.get('momentaryLufs',-144); st=d.get('shortTermLufs',-144); lra=d.get('lra',-1)
assert mo > lu - 1.0, 'max momentary (%.1f) should be >= integrated (%.1f)'%(mo,lu)
assert abs(st-lu) < 3.0, 'short-term max (%.1f) should be near integrated (%.1f)'%(st,lu)
assert 0.0 <= lra < 12.0, 'LRA (%.2f) out of range for a steady note'%lra
print('smoke: PASS — momentary %.1f / short-term %.1f / LRA %.1f LU (integrated %.1f)'%(mo,st,lra,lu))
" || { echo "smoke: momentary/short-term/LRA out of range" >&2; exit 1; }
g -d "{\"bpm\":$BPM0}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetTempo >/dev/null
g -d "{\"id\":$LT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null

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
# Pre/post-fader sends: a MUTED source's PRE-fader send still routes its full signal to a bus
# (a pre-fader aux ignores mute), so the bus reaches master; the same send set POST-fader is
# silenced when the channel is muted, so the bus is empty. The two full-mix renders therefore
# differ exactly by the bus signal. The post flag survives a project round-trip.
g -d "{\"path\":\"$WORK/pp_session.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SaveProject >/dev/null   # snapshot to restore after the reload test
PFT=$(g -d '{"name":"prepost","wave":"SAW","attack":0.01,"decay":0.1,"sustain":0.9,"release":0.1,"gain":0.9}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$PFT,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"notes\":[{\"pitch\":57,\"start_beat\":0,\"length_beats\":4,\"velocity\":0.95}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
g -d "{\"path\":\"$WORK/ppre.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SaveProject >/dev/null
PFI=$(python3 -c "
import xml.etree.ElementTree as ET
t=[t for t in ET.parse('$WORK/ppre.gloopy').getroot().iter('TRACK') if t.get('name')=='prepost'][0]
print(t.get('mixerTrack'))")
PFB=$(g -d '{"name":"AuxBus"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddBus | python3 -c "import json,sys;print(json.load(sys.stdin).get('id',0))")
g -d "{\"index\":$PFI,\"volume\":0.8,\"mute\":true}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetInsertParams >/dev/null   # mute the direct output
g -d "{\"insert\":$PFI,\"bus\":$PFB,\"level\":1.0,\"post_fader\":false}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetSend >/dev/null
g -d "{\"path\":\"$WORK/pp_pre.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"insert\":$PFI,\"bus\":$PFB,\"level\":1.0,\"post_fader\":true}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SetSend >/dev/null
g -d "{\"path\":\"$WORK/pp_post.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d "{\"path\":\"$WORK/pp.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SaveProject >/dev/null
g -d "{\"path\":\"$WORK/pp.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/LoadProject >/dev/null
g -d "{\"path\":\"$WORK/pp_post2.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
python3 -c "
import wave,xml.etree.ElementTree as ET
def rd(p):
    w=wave.open(p);f=w.readframes(w.getnframes());return [int.from_bytes(f[i:i+3],'little',signed=True) for i in range(0,len(f),3)]
def rms(x): return (sum(v*v for v in x)/max(1,len(x)))**0.5
pre=rd('$WORK/pp_pre.wav');post=rd('$WORK/pp_post.wav');post2=rd('$WORK/pp_post2.wav');n=min(len(pre),len(post))
# a muted source: pre-fader still feeds the bus (loud), post-fader is silenced (empty)
assert rms(pre[:n]) > rms(post[:n]) + 20000, 'pre-fader send did not route a muted source (pre %.0f vs post %.0f)'%(rms(pre[:n]),rms(post[:n]))
d=sum(abs(pre[i]-post[i]) for i in range(n))/n
assert d > 5000, 'pre vs post-fader renders barely differ (%.0f)'%d
snd=[s for s in ET.parse('$WORK/pp.gloopy').getroot().iter('SEND') if s.get('post')=='1']
assert snd, 'post-fader flag not saved on the SEND'
m2=min(len(post),len(post2)); assert post[:m2]==post2[:m2], 'post-fader send not preserved across a project round-trip'
print('smoke: PASS — pre-fader routes a muted source to the bus, post-fader silences it (pre %.0f vs post %.0f); post round-trips'%(rms(pre[:n]),rms(post[:n])))
" || { echo 'smoke: pre/post-fader send wrong' >&2; exit 1; }
g -d "{\"path\":\"$WORK/pp_session.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/LoadProject >/dev/null   # restore the pre-test session

# Control groups (VCA-lite): a group fader SCALES its member insert. Assign a fresh
# synth track's insert to a group, set gain 0.5 -> its soloed render drops ~6 dB; group
# mute -> silent; and the group + membership survive a project round-trip.
CG=$(g -d '{"name":"vcatk","wave":"SAW","attack":0.01,"decay":0.1,"sustain":0.8,"release":0.2,"gain":0.9}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$CG,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"notes\":[{\"pitch\":57,\"start_beat\":0,\"length_beats\":4,\"velocity\":0.9}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
g -d "{\"path\":\"$WORK/cgpre.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SaveProject >/dev/null
CGI=$(python3 -c "
import xml.etree.ElementTree as ET
t=[t for t in ET.parse('$WORK/cgpre.gloopy').getroot().iter('TRACK') if t.get('name')=='vcatk'][0]
print(t.get('mixerTrack'))")   # the track's routed insert index
cgpk() { g -d "{\"path\":\"$1\",\"tail_seconds\":0.3,\"start_beat\":0,\"end_beat\":4,\"track_id\":$CG}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null; g -d "{\"path\":\"$1\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AnalyzeFile | python3 -c "import json,sys;print(json.load(sys.stdin).get('peakDbfs',-120))"; }
CGBASE=$(cgpk "$WORK/cg0.wav")
g -d "{\"insert\":$CGI,\"group\":\"VCA\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AssignInsertToGroup >/dev/null
g -d '{"name":"VCA","gain":0.5}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetControlGroupGain >/dev/null
CGHALF=$(cgpk "$WORK/cg1.wav")
g -d '{"name":"VCA","mute":true}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetControlGroupMute >/dev/null
CGMUTE=$(cgpk "$WORK/cg2.wav")
python3 -c "
base,half,mute=float('$CGBASE'),float('$CGHALF'),float('$CGMUTE')
assert abs((base-half)-6.02) < 1.0, 'group gain 0.5 should drop ~6 dB, got %.2f dB'%(base-half)
assert mute < -60, 'muted group should be silent, got %.1f dBFS'%mute
print('smoke: PASS — VCA group: gain 0.5 drops %.1f dB, mute silences (%.0f dBFS)'%(base-half,mute))
" || { echo 'smoke: control-group scaling wrong' >&2; exit 1; }
g -d '{"name":"VCA","mute":false}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetControlGroupMute >/dev/null
g -d "{\"path\":\"$WORK/cg.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SaveProject >/dev/null
python3 -c "
import xml.etree.ElementTree as ET
r=ET.parse('$WORK/cg.gloopy').getroot()
grp=[x for x in r.iter('GROUP') if x.get('name')=='VCA']
assert grp and abs(float(grp[0].get('gain'))-0.5)<1e-6, 'group gain not saved: %s'%[ (x.get('name'),x.get('gain')) for x in r.iter('GROUP')]
mem=[m for m in r.iter('MTRACK') if m.get('group')=='VCA']
assert mem, 'no insert saved with group=VCA'
print('smoke: PASS — control group + membership round-trip (gain 0.5, %d member)'%len(mem))
" || { echo 'smoke: control group did not round-trip' >&2; exit 1; }
# Composition (TOML repo-format) round-trip: the group + membership must survive
# SaveComposition -> LoadComposition, not just the .gloopy SaveProject above.
g -d "{\"path\":\"$WORK/cgcomp\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SaveComposition >/dev/null
grep -q 'name = "VCA"' "$WORK/cgcomp/groups.toml" || { echo "smoke: groups.toml missing the VCA group" >&2; exit 1; }
g -d "{\"path\":\"$WORK/cgcomp\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/LoadComposition >/dev/null
g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/ListControlGroups | python3 -c "
import json,sys
gs={g['name']:g for g in json.load(sys.stdin).get('groups',[])}
assert 'VCA' in gs, 'VCA group lost on composition reload: %s'%list(gs)
assert abs(gs['VCA'].get('gain',0)-0.5)<1e-6, 'group gain wrong after reload: %s'%gs['VCA']
assert gs['VCA'].get('members',0)>=1, 'group membership lost after reload: %s'%gs['VCA']
print('smoke: PASS — control group survived composition round-trip (gain 0.5, %d member)'%gs['VCA']['members'])
" || { echo 'smoke: control group did not survive composition round-trip' >&2; exit 1; }
# Group solo (VCA solo): soloing a control group makes ONLY its members audible in the full
# mix. Put T1 in group "gs" (T2 stays ungrouped); with gs soloed, the full-mix render must
# isolate T1 — i.e. equal a T1-only render and drop T2. The solo flag round-trips.
GS1=$(g -d '{"name":"gsA","wave":"SAW","attack":0.01,"decay":0.1,"sustain":0.8,"release":0.1,"gain":0.9}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$GS1,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"notes\":[{\"pitch\":57,\"start_beat\":0,\"length_beats\":4,\"velocity\":0.9}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
GS2=$(g -d '{"name":"gsB","wave":"SAW","attack":0.01,"decay":0.1,"sustain":0.8,"release":0.1,"gain":0.9}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$GS2,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"notes\":[{\"pitch\":64,\"start_beat\":0,\"length_beats\":4,\"velocity\":0.9}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
g -d "{\"path\":\"$WORK/gspre.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SaveProject >/dev/null
GSI=$(python3 -c "
import xml.etree.ElementTree as ET
t=[t for t in ET.parse('$WORK/gspre.gloopy').getroot().iter('TRACK') if t.get('name')=='gsA'][0]
print(t.get('mixerTrack'))")   # gsA's routed insert index
g -d "{\"insert\":$GSI,\"group\":\"gs\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AssignInsertToGroup >/dev/null
g -d "{\"path\":\"$WORK/gs_t1.wav\",\"tail_seconds\":0,\"end_beat\":4,\"track_id\":$GS1}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null   # T1 alone
g -d "{\"path\":\"$WORK/gs_full.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null                    # full mix
g -d '{"name":"gs","solo":true}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetControlGroupSolo >/dev/null
g -d "{\"path\":\"$WORK/gs_solo.wav\",\"tail_seconds\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null                    # full mix, gs soloed
GSSOLO=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/ListControlGroups | python3 -c "import json,sys;print(next(g.get('solo',False) for g in json.load(sys.stdin)['groups'] if g['name']=='gs'))")
python3 -c "
import wave
def raw(p):
    w=wave.open(p);return w.readframes(w.getnframes())
def rms(p):
    f=raw(p);v=[int.from_bytes(f[i:i+3],'little',signed=True) for i in range(0,len(f),3)];return (sum(x*x for x in v)/max(1,len(v)))**0.5
assert '$GSSOLO'=='True','ListControlGroups did not report gs soloed'
assert rms('$WORK/gs_full.wav') > rms('$WORK/gs_t1.wav')*1.1, 'full mix should be louder than T1 alone (T2 present)'
assert raw('$WORK/gs_solo.wav')==raw('$WORK/gs_t1.wav'), 'group-solo full mix did not isolate T1 (differs from a T1-only render)'
print('smoke: PASS — group solo isolates its member (soloed full mix == T1 alone, T2 dropped)')
" || { echo 'smoke: group solo wrong' >&2; exit 1; }
# The solo flag survives a project round-trip.
g -d "{\"path\":\"$WORK/gssolo.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/SaveProject >/dev/null
g -d "{\"path\":\"$WORK/gssolo.gloopy\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/LoadProject >/dev/null
g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/ListControlGroups | python3 -c "
import json,sys
g=[x for x in json.load(sys.stdin)['groups'] if x['name']=='gs']
assert g and g[0].get('solo',False)==True, 'group solo lost on reload: %s'%g
print('smoke: PASS — group solo flag survives a project round-trip')
" || { echo 'smoke: group solo did not round-trip' >&2; exit 1; }
g -d '{"name":"gs","solo":false}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetControlGroupSolo >/dev/null
g -d '{"name":"gs"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveControlGroup >/dev/null 2>&1 || true
g -d "{\"id\":$GS1}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null
g -d "{\"id\":$GS2}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null

# Swing groove: swing shifts every other 1/8 note later, so a straight-8ths clip renders
# differently swung vs straight; GetTransport reports the amount. Solo-rendered, cleaned up.
SWT=$(g -d '{"name":"swingtk","wave":"SAW","attack":0.001,"decay":0.05,"sustain":0,"release":0.02,"gain":0.9}' 127.0.0.1:$PORT gloopy.v1.Gloopy/AddSynthTrack | grep -o '[0-9]\+' | head -1)
g -d "{\"track_id\":$SWT,\"start_beat\":0,\"length_beats\":4,\"content_len_beats\":4,\"notes\":[{\"pitch\":60,\"start_beat\":0,\"length_beats\":0.25,\"velocity\":0.9},{\"pitch\":60,\"start_beat\":0.5,\"length_beats\":0.25,\"velocity\":0.9},{\"pitch\":60,\"start_beat\":1,\"length_beats\":0.25,\"velocity\":0.9},{\"pitch\":60,\"start_beat\":1.5,\"length_beats\":0.25,\"velocity\":0.9}]}" 127.0.0.1:$PORT gloopy.v1.Gloopy/AddClip >/dev/null
g -d '{"amount":0.5}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetSwing >/dev/null
g -d "{\"path\":\"$WORK/sw_straight.wav\",\"tail_seconds\":0,\"start_beat\":0,\"end_beat\":4,\"track_id\":$SWT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d '{"amount":0.667}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetSwing >/dev/null
g -d "{\"path\":\"$WORK/sw_swung.wav\",\"tail_seconds\":0,\"start_beat\":0,\"end_beat\":4,\"track_id\":$SWT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
SWG=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetTransport | python3 -c "import json,sys;print(json.load(sys.stdin).get('swing'))")
python3 -c "
import wave
def rd(p):
    w=wave.open(p);f=w.readframes(w.getnframes());return [int.from_bytes(f[i:i+3],'little',signed=True) for i in range(0,len(f),3)]
a=rd('$WORK/sw_straight.wav');b=rd('$WORK/sw_swung.wav');m=min(len(a),len(b))
d=sum(abs(a[i]-b[i]) for i in range(m))/m/(1<<23)
assert d>0.0008, 'swing did not change the render (diff=%.5f)'%d   # off-beat shift is subtle but real
assert abs(float('$SWG')-0.667)<1e-3, 'GetTransport swing wrong: %s'%'$SWG'
print('smoke: PASS — swing shifts off-beat 8ths (render diff %.4f), GetTransport swing=%.3f'%(d,float('$SWG')))
" || { echo 'smoke: swing wrong' >&2; exit 1; }
g -d '{"amount":0.5}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetSwing >/dev/null   # restore straight
g -d "{\"id\":$SWT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null

# Metronome (last — clears the project): on an empty project a 4-beat render is silent
# with the click off and emits a click per beat with it on.
g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/NewProject >/dev/null
g -d "{\"path\":\"$WORK/metro_off.wav\",\"tail_seconds\":0,\"start_beat\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
g -d '{"enabled":true}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetMetronome >/dev/null
g -d "{\"path\":\"$WORK/metro_on.wav\",\"tail_seconds\":0,\"start_beat\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
METFLAG=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetTransport | python3 -c "import json,sys;print(json.load(sys.stdin).get('metronome'))")
python3 -c "
import wave
def rd(p):
    w=wave.open(p);f=w.readframes(w.getnframes());return [abs(int.from_bytes(f[i:i+3],'little',signed=True))/(1<<23) for i in range(0,len(f),3)]
off,on=rd('$WORK/metro_off.wav'),rd('$WORK/metro_on.wav')
poff=max(off) if off else 0; pon=max(on) if on else 0
step=int(0.05*44100)   # count 50 ms windows above threshold (each beat click spans ~1-2)
bursts=sum(1 for i in range(0,len(on)-step,step) if max(on[i:i+step])>0.05)
assert poff<0.01, 'empty project should be silent with the metronome off (peak %.4f)'%poff
assert pon>0.1, 'metronome on should click (peak %.4f)'%pon
assert bursts>=3, 'expected several beat clicks, got %d bursts'%bursts
assert '$METFLAG'=='True', 'GetTransport did not report metronome on'
print('smoke: PASS — metronome clicks each beat (off silent %.3f, on peak %.2f, %d bursts)'%(poff,pon,bursts))
" || { echo 'smoke: metronome wrong' >&2; exit 1; }
# Metronome level: the click volume scales linearly, so at 0.5 the click peak is ~half the
# full-level (metro_on) click. GetMetronomeLevel reports it back.
g -d '{"level":0.5}' 127.0.0.1:$PORT gloopy.v1.Gloopy/SetMetronomeLevel >/dev/null
MLVL=$(g -d '{}' 127.0.0.1:$PORT gloopy.v1.Gloopy/GetMetronomeLevel | python3 -c "import json,sys;print(round(json.load(sys.stdin).get('level',0),3))")
g -d "{\"path\":\"$WORK/metro_half.wav\",\"tail_seconds\":0,\"start_beat\":0,\"end_beat\":4}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RenderToFile >/dev/null
python3 -c "
import wave
def rd(p):
    w=wave.open(p);f=w.readframes(w.getnframes());return [abs(int.from_bytes(f[i:i+3],'little',signed=True))/(1<<23) for i in range(0,len(f),3)]
full=max(rd('$WORK/metro_on.wav'));half=max(rd('$WORK/metro_half.wav'))
assert '$MLVL'=='0.5','GetMetronomeLevel did not report 0.5 (got $MLVL)'
assert abs(half - full*0.5) < full*0.1, 'metronome level 0.5 did not halve the click (full %.3f, half %.3f)'%(full,half)
print('smoke: PASS — metronome level scales the click (full %.2f -> half %.2f), round-trips'%(full,half))
" || { echo 'smoke: metronome level wrong' >&2; exit 1; }

echo "smoke: OK"
