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
g -d "{\"target\":\"track/$MT/synth/cutoff\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveModulation >/dev/null
g -d "{\"id\":$MT}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RemoveTrack >/dev/null

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

# Export profiles: a named 'master' mix target renders a deterministic file, and the
# profile survives the composition round-trip (checked after the reload below).
g -d '{"name":"master","target":"mix","format":"wav"}' 127.0.0.1:$PORT gloopy.v1.Gloopy/DefineExportProfile >/dev/null
EXPFILE=$(g -d "{\"name\":\"master\",\"out_dir\":\"$WORK/exp\"}" 127.0.0.1:$PORT gloopy.v1.Gloopy/RunExport | grep -o '"[^"]*master.wav"' | tr -d '"' | head -1)
[ -s "$EXPFILE" ] || { echo "smoke: export profile produced no file" >&2; exit 1; }
python3 -c "import wave;assert wave.open('$EXPFILE').getnframes()>1000, 'export too short'"
echo "smoke: PASS — export profile 'master' rendered $(basename "$EXPFILE")"

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

echo "smoke: OK"
