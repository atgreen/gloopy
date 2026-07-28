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

MCP clients launch the server as a subprocess. In a Claude Desktop / Claude Code MCP config,
add an entry that runs the command — for example:

```json
{
  "mcpServers": {
    "gloopy": { "command": "gloopy", "args": ["mcp"] }
  }
}
```

The client then lists Gloopy's tools automatically and the agent can call them.

## Available tools

The tool surface grows with each release. Today it includes:

Read the project:

- **session/get_info** — the open project's tempo, transport position, and track count.
- **tracks/list** — the project's tracks (id, name, type, clip count).

Build the project (each is undoable, just like the same action in the app):

- **track/add** — add a synth track; returns its id.
- **clip/add** — add a MIDI clip to a track from a JSON note list
  (`[{pitch, start, length, velocity}, …]`, in beats); returns the clip index.
- **clip/move** — move a clip to a new start beat, optionally to another track.
- **markers/add_range** — add a named timeline range (a section marker).
- **transport/set_tempo** — set the project tempo in beats per minute.
- **project/save** — write the project to disk (composition folder).

So an agent can build a song from scratch — add tracks, drop in note clips, mark sections,
set the tempo, and save — then hand it to you to open in Gloopy. More tools (importing and
exporting note data in bulk, rendering to audio) are on the way.

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
