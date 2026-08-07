# Sync (and jam) with Ableton Link

**Task:** lock Gloopy's tempo and beat to Ableton Live, other DAWs, iOS music
apps, and hardware on the same network with **[Ableton Link](https://www.ableton.com/en/link/)**
— and, between Link apps that support it, stream audio across the network in time.
Assumes you've done [Your first track](../tutorials/first-track.md).

## Turn Link on

1. Click **LINK** in the toolbar. Gloopy joins the Link session on your local
   network; the button shows the **peer count** (`LINK 2` = two other apps found).
2. That's it — tempo and the beat grid are now shared with every Link peer. Start
   another Link app on the network and it lines up to the same beat.

Click **LINK** again to leave the session.

## Share audio across the network

Beyond tempo, Gloopy can send and receive **audio channels** over Link, beat-aligned
— useful for jamming across two machines. This works between apps that support Link
audio; tempo sync still works with every Link app.

**Send** — while **LINK** is on, Gloopy publishes your **master** as a channel that
other peers can pick up. Nothing to configure.

**Receive** a peer's channel onto a track:

1. Make sure **LINK** is on (so peers and their channels are visible).
2. Click **+ Track** and open the **Link Audio** submenu — it lists the audio
   channels other peers are publishing.
3. Pick one. A new track receives that peer's audio, playing it through the track's
   fader and **[insert effects](effects-and-mixing.md)** like any other source.

Joining and leaving a channel are click-free (a short fade), and if a peer drops
off and comes back the track picks it up again automatically.

## Notes

- **Same network.** Link finds peers on the local network automatically — no
  addresses or setup. Some networks block the discovery traffic; a normal home/LAN
  or a direct connection works.
- **Latency.** Received network audio carries a small buffering delay, so Link
  audio is for jamming and layering, not tight monitoring.
- **Scriptable.** Turn Link on/off and add a receiver track
  [from a script](../../control-scripting/index.md) (`set_link_enabled`,
  `add_link_audio_receiver`).
