# Toolbar and transport controls

The strip across the top of the window, left to right. (Keyboard equivalents are
in [Keyboard shortcuts](keyboard-shortcuts.md).)

## Transport and project

| Control | What it does |
|---------|--------------|
| **☰** (browser) | Toggle the left **Browser** sidebar — Templates, Demos, Plugins, Samples, Presets, Favorites |
| **File** | Open the [File menu](file-menu.md) |
| **Play** | Start / pause playback **in place** (swaps to a Pause icon while playing) |
| **Stop** | Stop and return the playhead to the start; also finalizes an active recording or session capture |
| **Record** | In **Arrange**: record MIDI into the selected instrument track (and any armed audio tracks). In **Session**: capture the launched clips onto the arrangement |
| **BPM** | The tempo field — type a value or use the arrows (40–260 BPM) |
| **Position** | The playhead readout, as **bar . beat . tick** |

## Add a track

Each button adds a track; they differ only in the sound source.

| Control | What it does |
|---------|--------------|
| **+ Synth** | New instrument track — choose **Surge XT** (full editor) or the **Basic synth** |
| **+ Sample** | Load a `.wav` / `.aif` / `.flac` into a **sampler** track |
| **+ SFZ** | Load an `.sfz` multisampled instrument (native sfizz) |
| **+ Audio** | Import an audio file as a new **audio** track |
| **+ Plugin** | Host a VST3/LV2 **instrument** (or **Rescan plugins**) |

See [What to use for what](../what-to-use-for-what.md#synth-vs-sample-vs-sfz-vs-plugin)
to choose.

## Musical and utility

| Control | What it does |
|---------|--------------|
| **Scale root** / **Scale name** | Set the project scale (root + type); drives snap-to-scale and piano-roll highlighting |
| **Metro** | Toggle the metronome click (accented downbeat) |
| **Loop** | Toggle transport looping over the loop region |
| **Panic** | All-notes-off on every track (also **Ctrl+.**) |
| **Maps** | Open the **Mappings** window — list, bypass, re-range, or remove MIDI/OSC maps and LFO routes |
| **Mixer** | Show / hide the embedded **Mixer** view |

## The status bar

The 22-pixel strip along the **bottom** shows, left to right: the project name (or
*Untitled*), an **unsaved** dot when there are unsaved edits, the git **branch**
and uncommitted/ahead/behind state once the project is a repo, and the **Gloopy
version** on the right. Full details in
[Version your project with git](../how-to/version-with-git.md#the-status-bar).
