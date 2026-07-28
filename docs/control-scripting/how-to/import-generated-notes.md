# Bulk-import a generated MIDI sequence

**Goal:** you generated a melody, drum pattern, or whole part in code (an
algorithm, an ML model, a script) and want it as a clip in Gloopy. Instead of
firing hundreds of note-adds, hand Gloopy the notes as **JSON** in one call.

## The note format

A sequence is a JSON **array of notes**, each a small object:

```json
[
  {"pitch": 60, "start": 0.0, "length": 1.0, "velocity": 0.8},
  {"pitch": 64, "start": 1.0, "length": 1.0, "velocity": 0.7},
  {"pitch": 67, "start": 2.0, "length": 2.0, "velocity": 0.9}
]
```

| Field | Meaning |
|-------|---------|
| `pitch` | MIDI note, 0–127 |
| `start` | Start in **beats**, relative to the clip |
| `length` | Length in beats |
| `velocity` | 0.0–1.0 |

`probability` (0–1) is optional. This is exactly the shape `export_notes_json`
emits, so a clip round-trips out and back in.

## Import it

`import_notes_json` builds a **new clip** on a track from that JSON and returns the
clip index (or `-1` if there were no usable notes).

=== "Python"

    ```python
    import json
    from gloopy import Gloopy

    # generate however you like — here, a rising scale
    notes = [{"pitch": 60 + i, "start": i * 0.5, "length": 0.5, "velocity": 0.8}
             for i in range(8)]

    with Gloopy() as g:
        lead = g.add_synth_track("Lead", wave="SAW")
        index = g.import_notes_json(lead, json.dumps(notes), start_beat=0.0)
        print("new clip", index)
    ```

=== "grpcurl"

    ```sh
    NOTES='[{"pitch":60,"start":0,"length":1,"velocity":0.8},
            {"pitch":67,"start":1,"length":1,"velocity":0.8}]'
    grpcurl -plaintext -proto proto/gloopy.proto -import-path proto \
        -d "{\"track_id\": 5, \"start_beat\": 0, \"json\": $(printf '%s' "$NOTES" | jq -Rs .)} " \
        127.0.0.1:50051 gloopy.v1.Gloopy/ImportNotesJSON
    ```

## Tidy it after import

Generated timing is often loose. The clip is a normal clip, so the
[destructive note ops](../reference/grpc/index.md) apply — e.g. `quantize_clip`
to snap starts to a grid:

```python
g.quantize_clip(lead, index, grid=0.25, strength=1.0)   # snap to 16ths
```

## See also

- [Note](../concepts/model.md#note) — the note model these fields map to.
- `export_notes_json` (round-trip the other way) and the destructive note ops in
  the [gRPC reference](../reference/grpc/index.md).
