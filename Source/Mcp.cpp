// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only
//
// MCP (Model Context Protocol) stdio server — Wave 11 #33. A thin adapter so AI agents
// (Claude Desktop / Claude Code and any MCP client) drive Gloopy directly over stdio, with
// no custom client glue. Transport = newline-delimited JSON-RPC 2.0 over stdin/stdout (the
// MCP stdio contract). Each MCP tool call maps to the SAME api* method the GUI/gRPC use, run
// in-process against a headless MainComponent — no gRPC hop, no Python runtime.
//
// Slice 1: initialize + tools/list + tools/call for a read-only handful
// (session/get_info, tracks/list, transport/set_tempo). Mutating/generative tools follow.
#include "MainComponent.h"
#include <iostream>
#include <string>

namespace {
    juce::var obj (std::initializer_list<std::pair<const char*, juce::var>> kv) {
        auto* o = new juce::DynamicObject();
        for (auto& p : kv) o->setProperty (p.first, p.second);
        return juce::var (o);
    }
    juce::var jrpcResult (const juce::var& id, juce::var result) {
        return obj ({ { "jsonrpc", "2.0" }, { "id", id }, { "result", std::move (result) } });
    }
    juce::var jrpcError (const juce::var& id, int code, const juce::String& message) {
        return obj ({ { "jsonrpc", "2.0" }, { "id", id },
                      { "error", obj ({ { "code", code }, { "message", message } }) } });
    }
    // MCP tool result payload: { content: [ { type:"text", text: <str> } ] }.
    juce::var toolText (const juce::String& text) {
        juce::Array<juce::var> content;
        content.add (obj ({ { "type", "text" }, { "text", text } }));
        return obj ({ { "content", juce::var (content) } });
    }
    juce::var noArgsSchema() {
        return obj ({ { "type", "object" }, { "properties", juce::var (new juce::DynamicObject()) } });
    }
    juce::var toolDef (const char* name, const char* desc, juce::var schema) {
        return obj ({ { "name", name }, { "description", desc }, { "inputSchema", std::move (schema) } });
    }
}

void MainComponent::runMcpStdio() {
    // One JSON-RPC message per line. Requests carry an "id"; notifications don't (no reply).
    // stdout is the protocol stream — only emit() writes to it (load chatter was muted by the
    // caller during construction/open).
    auto emit = [] (const juce::var& v) { std::cout << juce::JSON::toString (v, true) << std::endl; };

    std::string raw;
    while (std::getline (std::cin, raw)) {
        if (raw.empty()) continue;
        const auto msg = juce::JSON::parse (juce::String (raw));
        auto* dyn = msg.getDynamicObject();
        if (dyn == nullptr) continue;
        const auto method = msg["method"].toString();
        const juce::var id = dyn->hasProperty ("id") ? msg["id"] : juce::var();
        const bool isRequest = dyn->hasProperty ("id");

        if (method == "initialize") {
            emit (jrpcResult (id, obj ({
                { "protocolVersion", "2024-11-05" },
                { "capabilities", obj ({ { "tools", juce::var (new juce::DynamicObject()) } }) },
                { "serverInfo", obj ({ { "name", "gloopy" }, { "version", "0.1.0" } }) } })));
        }
        else if (! isRequest) {
            // A notification (e.g. notifications/initialized) — acknowledge silently.
        }
        else if (method == "tools/list") {
            juce::Array<juce::var> tools;
            tools.add (toolDef ("session/get_info",
                "Get the open project's tempo, transport position, and track count.", noArgsSchema()));
            tools.add (toolDef ("tracks/list",
                "List the project's tracks (id, name, type, clip count).", noArgsSchema()));
            juce::Array<juce::var> req; req.add ("bpm");
            tools.add (toolDef ("transport/set_tempo", "Set the project tempo in beats per minute.",
                obj ({ { "type", "object" },
                       { "properties", obj ({ { "bpm", obj ({ { "type", "number" },
                                                               { "description", "beats per minute" } }) } }) },
                       { "required", juce::var (req) } })));
            emit (jrpcResult (id, obj ({ { "tools", juce::var (tools) } })));
        }
        else if (method == "tools/call") {
            const auto name = msg["params"]["name"].toString();
            const auto arguments = msg["params"]["arguments"];

            if (name == "session/get_info") {
                const auto t = apiGetTransport();
                emit (jrpcResult (id, toolText (juce::JSON::toString (obj ({
                    { "bpm", t.bpm }, { "playing", t.playing },
                    { "position_beats", t.positionBeats }, { "tracks", (int) apiListTracks().size() } }), true))));
            }
            else if (name == "tracks/list") {
                juce::Array<juce::var> arr;
                for (auto& tr : apiListTracks())
                    arr.add (obj ({ { "id", tr.id }, { "name", tr.name }, { "type", tr.type }, { "clips", tr.clips } }));
                emit (jrpcResult (id, toolText (juce::JSON::toString (juce::var (arr), true))));
            }
            else if (name == "transport/set_tempo") {
                apiSetTempo ((double) arguments["bpm"]);
                emit (jrpcResult (id, toolText ("tempo set to " + juce::String (apiGetTransport().bpm) + " bpm")));
            }
            else emit (jrpcError (id, -32602, "unknown tool: " + name));
        }
        else emit (jrpcError (id, -32601, "method not found: " + method));
    }
}
