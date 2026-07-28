# Gain staging

Why Gloopy's faders read **0 to 1** instead of decibels, and what that means for
keeping a mix clean.

## Faders are normalized, not decibels

A track fader, a mixer fader, and a track's volume are all a single number from
**0.0 to 1.0**, applied as a **linear amplitude** straight onto the signal. There's
no decibel scale and no hidden taper curve — the number *is* the multiplier:

| Fader | Effect | ≈ decibels |
|-------|--------|-----------|
| **1.0** | Unity — the signal passes at its own level | 0 dB |
| **0.5** | Half the amplitude | −6 dB |
| **0.25** | A quarter | −12 dB |
| **0.0** | Silence | −∞ |

**1.0 is unity gain**: the track plays at exactly the level its instrument or audio
produced. Faders don't boost above unity — they attenuate from it.

## Why normalized

One number, shared everywhere:

- The **fader**, an **automation** point, a **MIDI-learn** mapping, and the
  **control API** all speak the same 0–1 value — a script setting a level and a
  hand moving the fader do the identical thing.
- It's **predictable**: no dB-taper maths to reverse-engineer, no surprise where
  the knob "feels" linear but isn't. Halfway is half the amplitude.

The trade-off is that you do your own gain staging — which is the next point.

## There's no automatic level control

Gloopy does **not** limit or normalize the mix for you. Tracks **sum**, and if
their combined peak crosses **0 dBFS** the master clips — a harsh digital
distortion, not a gentle one. So:

- **Leave headroom.** Aim for peaks a few dB below the top of the meter, not
  pinned to it.
- **Pull down, don't push up.** If the master clips, lower individual faders rather
  than raising everything — there's no room above unity to fix it later.
- **Add a limiter deliberately.** If you want a hard ceiling on the master, drop a
  **Limiter** as the last effect on the master strip. It's a normal insert, off by
  default, because a surprise limiter changes your sound.

The meters (post-pan, with a clip LED you click to reset) are how you watch this —
see [Add insert effects and balance a mix](../how-to/effects-and-mixing.md).

## Panning keeps the loudness

Pan uses a **constant-power** law: a centred track sends about −3 dB to each
speaker, and sweeping left↔right holds the *perceived* loudness roughly steady
rather than getting louder in the middle. So panning is a placement decision, not a
level one — you won't have to re-balance a track just because you moved it in the
stereo field.

## Groups scale, they don't re-route

A **control group** (VCA-lite) fader *scales* its members' faders — it's control
math layered on top, not an extra gain stage in the signal path. Pulling a drum
group down multiplies every drum fader by the same factor, so their internal
balance is preserved. (A **bus**, by contrast, is real routing — see
[Route to a bus](../how-to/bus-and-reverb-send.md).)

## See also

- [Add insert effects and balance a mix](../how-to/effects-and-mixing.md) — doing
  it, hands-on.
- [Mixer track (bus, send)](../../control-scripting/concepts/model.md#mixer-track-bus-send)
  and [Parameter](../../control-scripting/concepts/model.md#parameter-the-universal-parameter-model) —
  the models behind faders and the one-number scheme.
