// SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
// SPDX-License-Identifier: AGPL-3.0-only

// Git project management — source control over the composition-as-repo. We shell
// out to the *system* git (juce::ChildProcess) rather than link a git library: it
// reuses the user's identity, SSH keys, and credential helpers, adds no dependency,
// and keeps a clean process boundary. Git state lives in .git, out of band — nothing
// here touches the composition format or the audio thread. This first slice is
// read-only: is git available, and what's the repo status of the project's dir.

#include "MainComponent.h"

namespace
{
    // Run `git <args>` and capture stdout (and stderr when `alsoStderr`, so write-ops
    // can surface the failure message). Returns the exit code, or -1 if git could not be
    // launched (not installed / not on PATH). Blocking, so callers keep it off the audio
    // thread; the ops here (version/status/init) are fast and network-free.
    int runGit (const juce::StringArray& args, juce::String& out, bool alsoStderr = false)
    {
        juce::StringArray argv;
        argv.add ("git");
        argv.addArray (args);

        int flags = juce::ChildProcess::wantStdOut;
        if (alsoStderr) flags |= juce::ChildProcess::wantStdErr;

        juce::ChildProcess proc;
        if (! proc.start (argv, flags))
            return -1;                                  // git missing / failed to launch

        out = proc.readAllProcessOutput();              // reads to EOF (process exits)
        proc.waitForProcessToFinish (10000);            // 10s cap — status is quick
        return proc.getExitCode();
    }

    // Pull "ahead N" / "behind M" out of a porcelain `## ...[ahead 1, behind 2]` header.
    int parseCount (const juce::String& header, const juce::String& key)
    {
        const int at = header.indexOf (key + " ");
        if (at < 0) return 0;
        return header.substring (at + key.length() + 1).getIntValue();
    }
}

bool MainComponent::apiGitAvailable (juce::String& version)
{
    juce::String out;
    if (runGit ({ "--version" }, out) != 0)
        return false;
    version = out.trim();                               // "git version 2.43.0"
    return true;
}

MainComponent::GitStatusSnap MainComponent::apiGitStatus (const juce::String& dirOverride)
{
    GitStatusSnap s;

    juce::String version;
    s.available = apiGitAvailable (version);
    if (! s.available)
        return s;

    // Resolve the working dir: an explicit override, else the open project's folder
    // (a composition's gloopy.toml parent, or a .gloopy file's parent). Reading
    // currentProjectFile is a message-thread concern; the git subprocess then runs on
    // the caller's thread so it never stalls the message thread when driven over gRPC.
    juce::File dir;
    if (dirOverride.isNotEmpty())
        dir = juce::File (dirOverride);
    else
        dir = callOnMessageThread ([&]() -> juce::File
        {
            if (currentProjectFile.getFileName() == "gloopy.toml") return currentProjectFile.getParentDirectory();
            if (currentProjectFile.existsAsFile())                 return currentProjectFile.getParentDirectory();
            return {};
        });

    if (dir == juce::File())
        return s;                                       // untitled project — no dir to inspect
    s.dir = dir.getFullPathName();
    const juce::String d = s.dir;

    juce::String out;
    if (runGit ({ "-C", d, "rev-parse", "--is-inside-work-tree" }, out) != 0 || out.trim() != "true")
        return s;                                       // not a git repo
    s.isRepo = true;

    // Branch: `branch --show-current` prints the name (empty when detached), and works
    // even before the first commit. Detached -> show the short hash instead.
    if (runGit ({ "-C", d, "branch", "--show-current" }, out) == 0)
        s.branch = out.trim();
    if (s.branch.isEmpty())
    {
        s.detached = true;
        juce::String hash;
        if (runGit ({ "-C", d, "rev-parse", "--short", "HEAD" }, hash) == 0 && hash.trim().isNotEmpty())
            s.branch = "(" + hash.trim() + ")";
    }

    // Porcelain status: header line carries ahead/behind, the rest are changed files.
    if (runGit ({ "-C", d, "status", "--porcelain=v1", "--branch" }, out) == 0)
    {
        auto lines = juce::StringArray::fromLines (out);
        for (auto& line : lines)
        {
            if (line.isEmpty())
                continue;
            if (line.startsWith ("## "))
            {
                s.ahead  = parseCount (line, "ahead");
                s.behind = parseCount (line, "behind");
                continue;
            }
            // "XY path" — the first two chars are the status code, then a space.
            GitFileChange c;
            c.xy   = line.substring (0, 2);
            c.path = line.substring (3);
            s.changes.push_back (std::move (c));
        }
    }

    return s;
}

MainComponent::GitResult MainComponent::apiGitInit (const juce::String& dirStr)
{
    GitResult r;

    juce::String version;
    if (! apiGitAvailable (version)) { r.error = "git is not installed or not on PATH"; return r; }
    if (dirStr.isEmpty())            { r.error = "no directory given"; return r; }

    juce::File dir (dirStr);
    if (! dir.exists() && ! dir.createDirectory().wasOk())
    {
        r.error = "could not create " + dirStr;
        return r;
    }

    // `git init` is safe to run on an already-initialised repo (idempotent), so this
    // doubles as "Enable Git" for an existing folder. The .gitignore is owned by the
    // composition writer (kGitignore), so we don't write one here.
    juce::String out;
    const int code = runGit ({ "-C", dir.getFullPathName(), "init" }, out, /*alsoStderr*/ true);
    if (code != 0)
    {
        r.error = out.trim().isNotEmpty() ? out.trim() : "git init failed";
        return r;
    }
    r.ok = true;
    return r;
}

// Format a status snapshot as the human-readable text the Source Control window shows.
// (openSourceControl itself lives in MainComponent.cpp, where the shared window helper
// is in scope.)
juce::String MainComponent::gitStatusReport()
{
    juce::String version;
    if (! apiGitAvailable (version))
        return "git is not installed or not on PATH.\n\n"
               "Install git to version-control your compositions.";

    auto s = apiGitStatus();
    juce::String text;
    text << version << "\n\n";
    if (s.dir.isEmpty())
        return text + "No saved project — save as a Composition folder to track it in git.\n";

    text << "Folder:  " << s.dir << "\n";
    if (! s.isRepo)
        return text + "Status:  not a git repository\n"
                      "         (\"Enable Git\" / \"New Git Project\" arrive in the next slice).\n";

    text << "Branch:  " << s.branch << (s.detached ? "  (detached HEAD)" : "") << "\n";
    if (s.ahead > 0 || s.behind > 0)
        text << "Sync:    ahead " << s.ahead << ", behind " << s.behind << "\n";
    text << "\nChanges (" << (int) s.changes.size() << "):\n";
    if (s.changes.empty())
        text << "  (working tree clean)\n";
    else
        for (auto& c : s.changes)
            text << "  " << c.xy << "  " << c.path << "\n";
    return text;
}
