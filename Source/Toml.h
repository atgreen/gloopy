// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>
#include <map>
#include <vector>

/** A deliberately small TOML subset for Gloopy's "composition as code" files:
    root key/values, single `[tables]`, arrays-of-tables `[[arrays]]`, flat
    `key = value` lines, and scalar arrays. Enough for the composition manifests,
    tolerant of comments, surrounding whitespace and key reordering — the things a
    human editing the files will do. Not a full TOML implementation (no nested
    tables, inline tables, or dates); Phase 2 can swap in a complete library. */
namespace gloopy::toml
{

inline juce::String stripInlineComment (const juce::String& line)
{
    bool inStr = false;
    for (int i = 0; i < line.length(); ++i)
    {
        const auto c = line[i];
        if (c == '"' && (i == 0 || line[i - 1] != '\\')) inStr = ! inStr;
        else if (c == '#' && ! inStr) return line.substring (0, i);
    }
    return line;
}

inline juce::String unquote (juce::String v)
{
    v = v.trim();
    if (v.startsWithChar ('"') && v.endsWithChar ('"') && v.length() >= 2)
        v = v.substring (1, v.length() - 1).replace ("\\\"", "\"").replace ("\\\\", "\\");
    return v;
}

struct Table
{
    std::map<juce::String, juce::String> raw;   // key -> raw (trimmed) value text

    bool         has       (const char* k) const { return raw.find (k) != raw.end(); }
    juce::String getString (const char* k, const juce::String& def = {}) const
    {
        auto it = raw.find (k); return it == raw.end() ? def : unquote (it->second);
    }
    double getDouble (const char* k, double def = 0.0) const
    {
        auto it = raw.find (k); return it == raw.end() ? def : it->second.trim().getDoubleValue();
    }
    int  getInt  (const char* k, int def = 0) const
    {
        auto it = raw.find (k); return it == raw.end() ? def : it->second.trim().getIntValue();
    }
    bool getBool (const char* k, bool def = false) const
    {
        auto it = raw.find (k); return it == raw.end() ? def : it->second.trim() == "true";
    }
    juce::StringArray getStringArray (const char* k) const
    {
        juce::StringArray out;
        auto it = raw.find (k);
        if (it == raw.end()) return out;
        auto s = it->second.trim();
        if (s.startsWithChar ('[')) s = s.substring (1);
        if (s.endsWithChar (']'))   s = s.dropLastCharacters (1);
        for (auto& part : juce::StringArray::fromTokens (s, ",", "\""))
            if (part.trim().isNotEmpty()) out.add (unquote (part));
        return out;
    }
};

struct Document
{
    Table root;
    std::map<juce::String, std::vector<Table>> arrays;   // "tracks" -> elements
    std::map<juce::String, Table>              tables;   // "generator" -> table

    const std::vector<Table>* array (const char* name) const
    {
        auto it = arrays.find (name); return it == arrays.end() ? nullptr : &it->second;
    }
    const Table* table (const char* name) const
    {
        auto it = tables.find (name); return it == tables.end() ? nullptr : &it->second;
    }
};

inline Document parse (const juce::String& text)
{
    Document doc;
    Table* cur = &doc.root;
    for (auto& rawLine : juce::StringArray::fromLines (text))
    {
        const auto line = stripInlineComment (rawLine).trim();
        if (line.isEmpty()) continue;

        if (line.startsWith ("[[") && line.endsWith ("]]"))
        {
            const auto name = line.substring (2, line.length() - 2).trim();
            doc.arrays[name].push_back ({});
            cur = &doc.arrays[name].back();
        }
        else if (line.startsWithChar ('[') && line.endsWithChar (']'))
        {
            const auto name = line.substring (1, line.length() - 1).trim();
            cur = &doc.tables[name];
        }
        else
        {
            const int eq = line.indexOfChar ('=');
            if (eq <= 0) continue;
            cur->raw[unquote (line.substring (0, eq).trim())] = line.substring (eq + 1).trim();
        }
    }
    return doc;
}

/** Accumulates composition TOML with lossless number formatting. */
class Writer
{
public:
    Writer& comment (const juce::String& c) { out << "# " << c << "\n"; return *this; }
    Writer& blank()                         { out << "\n"; return *this; }
    Writer& table  (const juce::String& n)  { out << "[" << n << "]\n"; return *this; }
    Writer& arrayItem (const juce::String& n) { out << "[[" << n << "]]\n"; return *this; }

    Writer& str  (const juce::String& k, const juce::String& v) { return line (k, quote (v)); }
    Writer& boolean (const juce::String& k, bool v)             { return line (k, v ? "true" : "false"); }
    Writer& integer (const juce::String& k, juce::int64 v)      { return line (k, juce::String (v)); }
    Writer& number  (const juce::String& k, double v)          { return line (k, num (v)); }

    // Format a juce::var with its natural type (keeps ints looking like ints).
    Writer& value (const juce::String& k, const juce::var& v)
    {
        if      (v.isBool())              return boolean (k, (bool) v);
        else if (v.isInt() || v.isInt64())return integer (k, (juce::int64) v);
        else if (v.isDouble())            return number  (k, (double) v);
        else                              return str     (k, v.toString());
    }

    Writer& strArray (const juce::String& k, const juce::StringArray& vals)
    {
        juce::StringArray q; for (auto& v : vals) q.add (quote (v));
        return line (k, "[ " + q.joinIntoString (", ") + " ]");
    }

    juce::String str() const { return out; }

    static juce::String quote (const juce::String& v)
    {
        return "\"" + v.replace ("\\", "\\\\").replace ("\"", "\\\"") + "\"";
    }
    static juce::String num (double v)   // %.17g round-trips IEEE-754 doubles exactly
    {
        char buf[40]; std::snprintf (buf, sizeof buf, "%.17g", v); return juce::String (buf);
    }

private:
    Writer& line (const juce::String& k, const juce::String& v)
    {
        const bool needQuoteKey = k.containsAnyOf (" .\t");
        out << (needQuoteKey ? quote (k) : k) << " = " << v << "\n";
        return *this;
    }
    juce::String out;
};

} // namespace gloopy::toml
