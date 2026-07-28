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
    // Discards everything — api* methods print [osc]/[loc]/[composition] chatter to std::cout,
    // which would corrupt the JSON-RPC stream; we redirect std::cout here for the whole session.
    struct NullBuf : std::streambuf { int overflow (int c) override { return c; } };

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
    juce::var prop (const char* type, const char* desc) { return obj ({ { "type", type }, { "description", desc } }); }
    juce::Array<juce::var> reqList (std::initializer_list<const char*> names) {
        juce::Array<juce::var> a; for (auto n : names) a.add (juce::String (n)); return a;
    }
    // A JSON-Schema object with the given properties and (optionally) a required list.
    juce::var objSchema (juce::var properties, juce::Array<juce::var> required = {}) {
        auto s = obj ({ { "type", "object" }, { "properties", properties } });
        if (! required.isEmpty()) s.getDynamicObject()->setProperty ("required", juce::var (required));
        return s;
    }
    juce::var toolDef (const char* name, const char* desc, juce::var schema) {
        return obj ({ { "name", name }, { "description", desc }, { "inputSchema", std::move (schema) } });
    }
    juce::var resourceDef (const char* uri, const char* name, const char* desc, const char* mime) {
        return obj ({ { "uri", uri }, { "name", name }, { "description", desc }, { "mimeType", mime } });
    }
    // The domain-model doc, so an agent can read what tracks/clips/inserts mean before driving.
    juce::String modelDoc() {
        juce::File f = juce::File::getCurrentWorkingDirectory().getChildFile ("docs/control-scripting/concepts/model.md");
        if (! f.existsAsFile())
            f = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                    .getParentDirectory().getChildFile ("docs/control-scripting/concepts/model.md");
        if (f.existsAsFile()) return f.loadFileAsString();
        return "# The Gloopy model\n\nA project has tracks; each track holds clips (MIDI notes or "
               "audio). Tracks feed mixer inserts (volume/pan/effects) that sum to master. Positions "
               "are in beats. See the manual for the full model.";
    }
}

void MainComponent::runMcpStdio() {
    // One JSON-RPC message per line. Requests carry an "id"; notifications don't (no reply).
    // Capture the REAL stdout for the protocol stream, then redirect std::cout to a null sink so
    // api* chatter can't interleave with responses. emit() is the only thing that reaches stdout.
    std::ostream protoOut (std::cout.rdbuf());
    NullBuf nullBuf;
    std::cout.rdbuf (&nullBuf);
    struct RestoreCout { std::ostream& o; ~RestoreCout() { std::cout.rdbuf (o.rdbuf()); } } restore { protoOut };
    auto emit = [&protoOut] (const juce::var& v) { protoOut << juce::JSON::toString (v, true) << std::endl; };

    // The render tools bounce offline; the headless build skips the audio device, so the
    // generators need one prepareToPlay before the first render (the CLI render path does the same).
    bool prepared = false;
    auto ensurePrepared = [&] () { if (! prepared) { prepareToPlay (512, 44100.0); prepared = true; } };

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
                { "capabilities", obj ({ { "tools", juce::var (new juce::DynamicObject()) },
                                         { "resources", juce::var (new juce::DynamicObject()) } }) },
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
                objSchema (obj ({ { "bpm", prop ("number", "beats per minute") } }), reqList ({ "bpm" }))));
            (void) req;
            // Mutating tools (slice 2) — each is a thin wrapper over the same undoable api* op.
            tools.add (toolDef ("track/add", "Add a new synth track. Returns its numeric id.",
                objSchema (obj ({ { "name", prop ("string", "track name (optional)") } }))));
            tools.add (toolDef ("clip/add", "Add a MIDI clip to a track from a JSON note list "
                "([{pitch,start,length,velocity}, ...] in beats). Returns the new clip index.",
                objSchema (obj ({ { "track_id",   prop ("integer", "target track id") },
                                  { "start_beat",  prop ("number",  "clip start in beats (default 0)") },
                                  { "notes",       obj ({ { "type", "array" },
                                                          { "description", "notes: pitch, start, length (beats), velocity (0..1)" },
                                                          { "items", obj ({ { "type", "object" } }) } }) } }),
                           reqList ({ "track_id", "notes" }))));
            tools.add (toolDef ("clip/move", "Move a clip to a new start beat, optionally to another track.",
                objSchema (obj ({ { "track_id",     prop ("integer", "the clip's current track id") },
                                  { "index",        prop ("integer", "the clip's index on that track") },
                                  { "start_beat",   prop ("number",  "new start position in beats") },
                                  { "to_track_id",  prop ("integer", "move to this track id (optional)") } }),
                           reqList ({ "track_id", "index", "start_beat" }))));
            tools.add (toolDef ("markers/add_range", "Add a named timeline range (section marker).",
                objSchema (obj ({ { "name",       prop ("string", "range name") },
                                  { "start_beat", prop ("number", "range start in beats") },
                                  { "end_beat",   prop ("number", "range end in beats") } }),
                           reqList ({ "name", "start_beat", "end_beat" }))));
            tools.add (toolDef ("notes/export_json", "Read a clip's notes back as a JSON array "
                "([{pitch,start,length,velocity}, ...] in beats) — the read half of the note I/O pair "
                "(clip/add is the write half).",
                objSchema (obj ({ { "track_id", prop ("integer", "the clip's track id") },
                                  { "index",    prop ("integer", "the clip's index on that track") } }),
                           reqList ({ "track_id", "index" }))));
            tools.add (toolDef ("project/save", "Save the project to disk (composition folder). "
                "Defaults to the open project's folder.",
                objSchema (obj ({ { "path", prop ("string", "destination folder (optional)") } }))));
            tools.add (toolDef ("render/mix", "Render the project to a WAV/FLAC file (offline bounce). "
                "Renders the whole song, a named range, or a single track (stem). Returns the path and a "
                "loudness report.",
                objSchema (obj ({ { "path",     prop ("string",  "output file path (.wav or .flac)") },
                                  { "range",    prop ("string",  "render only this named range (optional)") },
                                  { "track_id", prop ("integer", "render only this track as a stem (optional)") } }),
                           reqList ({ "path" }))));
            tools.add (toolDef ("render/preset", "Run a named export profile (defined in the project). "
                "Returns the written file paths.",
                objSchema (obj ({ { "name",    prop ("string", "export profile name") },
                                  { "out_dir", prop ("string", "output directory override (optional)") } }),
                           reqList ({ "name" }))));
            emit (jrpcResult (id, obj ({ { "tools", juce::var (tools) } })));
        }
        else if (method == "resources/list") {
            juce::Array<juce::var> resources;
            resources.add (resourceDef ("gloopy://composition", "Current composition",
                "The open project's structure (title, tempo, tracks, inserts, locations, exports) as JSON.",
                "application/json"));
            resources.add (resourceDef ("gloopy://model", "Gloopy domain model",
                "How Gloopy is organised — tracks, clips, inserts, time — so an agent knows what it's driving.",
                "text/markdown"));
            emit (jrpcResult (id, obj ({ { "resources", juce::var (resources) } })));
        }
        else if (method == "resources/read") {
            const auto uri = msg["params"]["uri"].toString();
            juce::String text, mime;
            if (uri == "gloopy://composition") { text = apiInspectJson(); mime = "application/json"; }
            else if (uri == "gloopy://model")  { text = modelDoc();       mime = "text/markdown"; }
            if (mime.isEmpty()) emit (jrpcError (id, -32602, "unknown resource: " + uri));
            else {
                juce::Array<juce::var> contents;
                contents.add (obj ({ { "uri", uri }, { "mimeType", mime }, { "text", text } }));
                emit (jrpcResult (id, obj ({ { "contents", juce::var (contents) } })));
            }
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
            else if (name == "track/add") {
                auto tname = arguments["name"].toString();
                if (tname.isEmpty()) tname = "Track";
                const int tid = apiAddSynthTrack (tname, 1, 0.01f, 0.1f, 0.8f, 0.2f, 0.8f);
                emit (jrpcResult (id, toolText (juce::JSON::toString (obj ({ { "id", tid } }), true))));
            }
            else if (name == "clip/add") {
                const int    tid   = (int) arguments["track_id"];
                const double start = (double) arguments["start_beat"];
                const auto   notesJson = juce::JSON::toString (arguments["notes"], true);
                const int    idx = apiImportClipNotesJson (tid, start, notesJson);
                if (idx < 0) emit (jrpcError (id, -32602, "clip/add: no usable notes or unknown track"));
                else emit (jrpcResult (id, toolText (juce::JSON::toString (obj ({ { "index", idx } }), true))));
            }
            else if (name == "clip/move") {
                const int    tid   = (int) arguments["track_id"];
                const int    idx   = (int) arguments["index"];
                const double start = (double) arguments["start_beat"];
                const bool   hasTo = arguments.getDynamicObject() != nullptr
                                     && arguments.getDynamicObject()->hasProperty ("to_track_id");
                const int    toTid = (int) arguments["to_track_id"];
                const bool ok = apiMoveClip (tid, idx, start, hasTo, toTid);
                if (ok) emit (jrpcResult (id, toolText ("clip moved")));
                else emit (jrpcError (id, -32602, "clip/move: unknown track or clip index"));
            }
            else if (name == "markers/add_range") {
                const bool ok = apiAddLocation (arguments["name"].toString(), "range",
                                                (double) arguments["start_beat"], (double) arguments["end_beat"]);
                if (ok) emit (jrpcResult (id, toolText ("range added")));
                else emit (jrpcError (id, -32602, "markers/add_range failed"));
            }
            else if (name == "notes/export_json") {
                const auto json = apiExportClipNotesJson ((int) arguments["track_id"], (int) arguments["index"]);
                emit (jrpcResult (id, toolText (json)));
            }
            else if (name == "render/mix") {
                const auto path = arguments["path"].toString();
                double startBeat = 0.0, endBeat = 0.0;
                const auto range = arguments["range"].toString();
                if (range.isNotEmpty() && ! apiResolveRange (range, startBeat, endBeat)) {
                    emit (jrpcError (id, -32602, "render/mix: unknown range '" + range + "'"));
                }
                else {
                    const bool hasTrack = arguments.getDynamicObject() != nullptr
                                          && arguments.getDynamicObject()->hasProperty ("track_id");
                    ensurePrepared();
                    const bool ok = apiRenderToFile (path, 2.0, startBeat, endBeat, hasTrack, (int) arguments["track_id"]);
                    if (! ok) emit (jrpcError (id, -32603, "render/mix: render failed"));
                    else {
                        LoudnessReport r {};
                        const bool measured = apiAnalyzeFile (path, r);
                        auto result = obj ({ { "path", path } });
                        if (measured)
                            result.getDynamicObject()->setProperty ("loudness", obj ({
                                { "peak_dbfs", r.peakDbfs }, { "true_peak_dbtp", r.truePeakDbtp },
                                { "rms_dbfs", r.rmsDbfs }, { "lufs", r.lufs } }));
                        emit (jrpcResult (id, toolText (juce::JSON::toString (result, true))));
                    }
                }
            }
            else if (name == "render/preset") {
                ensurePrepared();
                std::vector<juce::String> files;
                const bool ok = apiRunExport (arguments["name"].toString(), arguments["out_dir"].toString(), files);
                if (! ok) emit (jrpcError (id, -32602, "render/preset: unknown profile or export failed"));
                else {
                    juce::Array<juce::var> arr;
                    for (auto& f : files) arr.add (f);
                    emit (jrpcResult (id, toolText (juce::JSON::toString (obj ({ { "files", juce::var (arr) } }), true))));
                }
            }
            else if (name == "project/save") {
                juce::String path = arguments["path"].toString();
                if (path.isEmpty())
                    path = currentProjectFile.getFileName() == "gloopy.toml"
                             ? currentProjectFile.getParentDirectory().getFullPathName()
                             : currentProjectFile.getFullPathName();
                if (path.isEmpty()) { emit (jrpcError (id, -32602, "project/save: no path and no open project")); }
                else {
                    const bool ok = apiSaveComposition (path);
                    if (ok) emit (jrpcResult (id, toolText ("saved to " + path)));
                    else emit (jrpcError (id, -32603, "project/save failed"));
                }
            }
            else emit (jrpcError (id, -32602, "unknown tool: " + name));
        }
        else emit (jrpcError (id, -32601, "method not found: " + method));
    }
}
