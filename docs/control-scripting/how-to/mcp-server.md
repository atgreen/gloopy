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

- **session/get_info** — the open project's tempo, transport position, and track count.
- **tracks/list** — the project's tracks (id, name, type, clip count).
- **transport/set_tempo** — set the project tempo in beats per minute.

More tools — adding tracks and clips, importing/exporting note data, and rendering — are on
the way, each a thin, undoable wrapper over an existing Gloopy action.

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
