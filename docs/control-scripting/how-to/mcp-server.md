# Let an AI agent drive Gloopy (MCP)

Gloopy speaks the **Model Context Protocol (MCP)** — the open standard AI assistants use to
call tools. Point an MCP-capable agent (Claude Desktop, Claude Code, or any MCP client) at
Gloopy and it can inspect and control your project directly, with no custom glue.

The server is built in: run

```sh
gloopy mcp [project]
```

and Gloopy speaks MCP over its standard input/output. Give it a project folder or `.gloopy`
file to start from that song; omit it to start empty. Under the hood every tool call maps to
the *same* actions the app and the scripting API expose, so an agent and a person are driving
the one shared model.

## Registering the server with a client

`gloopy mcp` is the entry point — a self-contained stdio server built into the Gloopy binary,
with no separate package to install and no Python runtime. MCP clients launch it as a
subprocess. In a Claude Desktop / Claude Code MCP config, add:

```json
{
  "mcpServers": {
    "gloopy": { "command": "gloopy", "args": ["mcp"] }
  }
}
```

`command: "gloopy"` works once Gloopy is installed on your `PATH`. If it isn't, give the full
path to the binary — and optionally a project to open on startup:

```json
{
  "mcpServers": {
    "gloopy": { "command": "/path/to/gloopy", "args": ["mcp", "/path/to/my-song"] }
  }
}
```

The client then lists Gloopy's tools automatically and the agent can call them.

### Verify it's registered

A client registers by performing the MCP handshake — `initialize`, then `tools/list`. You can
run the same handshake yourself:

```sh
printf '%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' \
  | gloopy mcp
```

You'll get the server info back, then the full tool list.

## Available tools

The tool surface grows with each release. Today it includes:

Read the project:

- **session/get_info** — the open project's tempo, transport position, and track count.
- **tracks/list** — the project's tracks (id, name, type, clip count).

Build the project (each is undoable, just like the same action in the app):

- **track/add** — add a synth track; returns its id.
- **clip/add** — add a MIDI clip to a track from a JSON note list
  (`[{pitch, start, length, velocity}, …]`, in beats); returns the clip index.
- **notes/export_json** — read a clip's notes back out as that same JSON array (the read half of
  the note I/O pair — round-trips with clip/add).
- **clip/move** — move a clip to a new start beat, optionally to another track.
- **markers/add_range** — add a named timeline range (a section marker).
- **transport/set_tempo** — set the project tempo in beats per minute.
- **project/save** — write the project to disk (composition folder).

Render to audio:

- **render/mix** — bounce the project to a WAV/FLAC file (the whole song, a named range, or a
  single track as a stem); returns the path and a loudness report (peak, true-peak, RMS, LUFS).
- **render/preset** — run a named export profile defined in the project; returns the written paths.

So an agent can build a song from scratch — add tracks, drop in note clips, mark sections,
set the tempo, and save — then hand it to you to open in Gloopy.

## Resources

Beyond tools, the server exposes two **MCP resources** an agent can read for context:

- **gloopy://composition** — the open project's structure (title, tempo, tracks, inserts,
  locations, exports) as JSON, so an agent can see what it's working with.
- **gloopy://model** — Gloopy's domain model (what tracks, clips, inserts, and time mean),
  so an agent knows the vocabulary before it starts driving.

## Trying it by hand

MCP is line-delimited JSON-RPC, so you can exercise the server from a shell:

```sh
printf '%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' \
  '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"session/get_info","arguments":{}}}' \
  | gloopy mcp my-song
```

Each line in, one JSON-RPC reply out.
