# Add and control effects from a script

**Goal:** put an effect on a track (or the master), find its parameters, set one,
bypass it, and — when a built-in isn't enough — host a VST3/LV2 plugin effect.
All over the [gRPC lane](../concepts/model.md#the-two-control-lanes), so it scripts
and repeats.

Effects live in a [mixer insert](../concepts/model.md#mixer-track-bus-send)'s
chain, addressed by a `(insert, slot)` pair:

- **Insert** is a mixer strip. **Insert `0` is the master**; each track has its
  own insert (its strip carries the track's name).
- **Slot** is the position in that insert's effect chain — `add_effect` returns
  the slot it landed in.

## Find the insert for a track

`add_synth_track` gives you a *track* id; effects attach to that track's *insert*.
List the inserts and match by name (or just target the master, insert 0):

=== "Python"

    ```python
    from gloopy import Gloopy

    with Gloopy() as g:
        lead = g.add_synth_track("Lead", wave="SAW")
        insert = next(i["index"] for i in g.list_inserts() if i["name"] == "Lead")
    ```

=== "Common Lisp"

    ```lisp
    (let* ((lead   (add-synth-track "Lead" :wave :saw))
           (insert (getf (find "Lead" (list-inserts)
                                :key (lambda (i) (getf i :name)) :test #'string=)
                         :index)))
      …)
    ```

## Add an effect and set a parameter

Add a built-in **reverb**, ask what parameters it exposes, then turn the wet level
up. Every effect reports its parameters (with ranges) — don't guess, query:

=== "Python"

    ```python
    ins, slot = g.add_effect(insert, "REVERB")
    print(g.effect_params(ins, slot))
    # [{'name': 'Room', 'value': 0.5, 'min': 0.0, 'max': 1.0},
    #  {'name': 'Damp', 'value': 0.5, ...}, {'name': 'Wet', 'value': 0.33, ...}]
    g.set_effect_param(ins, slot, "Wet", 0.5)
    ```

=== "Common Lisp"

    ```lisp
    (let ((slot (add-effect insert :reverb)))
      (effect-params insert slot)
      ;; => ((:NAME "Room" :VALUE 0.5 :MIN 0.0 :MAX 1.0) (:NAME "Damp" …) (:NAME "Wet" …))
      (set-effect-param insert slot "Wet" 0.5))
    ```

=== "grpcurl"

    ```sh
    G="grpcurl -plaintext -proto proto/gloopy.proto -import-path proto"
    # add reverb to insert 1 -> returns {"insert":1,"slot":0}
    $G -d '{"insert":1,"type":3}' 127.0.0.1:50051 gloopy.v1.Gloopy/AddEffect
    $G -d '{"insert":1,"slot":0}' 127.0.0.1:50051 gloopy.v1.Gloopy/GetEffectParams
    $G -d '{"insert":1,"slot":0,"name":"Wet","value":0.5}' \
        127.0.0.1:50051 gloopy.v1.Gloopy/SetEffectParam
    ```

The built-in effect **type** is a name in the clients (`"REVERB"`, `:reverb`) and
an integer on the wire (`3`). The full set — gain, filter, delay, reverb, limiter,
bitcrusher, compressor, EQ, waveshaper, and more — is listed under
[Effect](../concepts/model.md#effect). (In Lisp, the keyword shorthands cover
`:gain :filter :delay :reverb`; pass the integer for the rest.)

!!! warning "Setting a parameter to exactly 0"
    `SetEffectParam` goes over gRPC, and proto3 drops zero-valued fields on the
    wire — so you **can't** set a parameter to exactly `0.0` this way. Use a tiny
    value, or the [OSC lane](../reference/osc/index.md) (`fx-param`), for a true
    zero. Same reason as [the model's control-lane split](../concepts/model.md#the-two-control-lanes).

## Bypass or remove

=== "Python"

    ```python
    g.set_effect_bypass(ins, slot, True)   # audition without it
    g.remove_effect(ins, slot)             # take it out of the chain
    ```

=== "Common Lisp"

    ```lisp
    (set-effect-bypass insert slot t)
    (remove-effect insert slot)
    ```

## Host a plugin effect

When a built-in won't do, host a VST3/LV2 effect. Find its `identifier` in the
plugin list, then add it exactly like a built-in — it lands in a slot and its
parameters come back from `effect_params` the same way:

=== "Python"

    ```python
    fx = [p for p in g.list_plugins() if not p["is_instrument"]]
    ins, slot = g.add_plugin_effect(insert, fx[0]["identifier"])
    ```

=== "Common Lisp"

    ```lisp
    (let* ((fx (remove-if (lambda (p) (getf p :instrument)) (list-plugins)))
           (slot (add-plugin-effect insert (getf (first fx) :identifier))))
      (effect-params insert slot))
    ```

## See also

- [The Effect model](../concepts/model.md#effect) — the built-in set and the
  analyzer effects (scope, spectrum, vectorscope).
- [Common Lisp client reference](../reference/lisp/index.md#mixer-effects) ·
  [Python client reference](../reference/python/index.md).
