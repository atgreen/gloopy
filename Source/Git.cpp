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

    // `git init` is safe to run on an already-initialised repo (idempotent), so saving a
    // composition folder can call this unconditionally to auto-init it. The .gitignore is
    // owned by the composition writer (kGitignore), so we don't write one here.
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

MainComponent::GitResult MainComponent::apiGitAdd (const juce::String& dir, const juce::StringArray& paths)
{
    GitResult r;
    juce::String version;
    if (! apiGitAvailable (version)) { r.error = "git is not installed or not on PATH"; return r; }
    if (dir.isEmpty())               { r.error = "no directory given"; return r; }

    juce::StringArray args { "-C", dir, "add" };
    if (paths.isEmpty()) args.add ("-A");                 // stage everything (Stage-all)
    else                 args.addArray (paths);

    juce::String out;
    if (runGit (args, out, /*alsoStderr*/ true) != 0)
    {
        r.error = out.trim().isNotEmpty() ? out.trim() : "git add failed";
        return r;
    }
    r.ok = true;
    return r;
}

MainComponent::GitResult MainComponent::apiGitCommit (const juce::String& dir, const juce::String& message, bool amend)
{
    GitResult r;
    juce::String version;
    if (! apiGitAvailable (version))          { r.error = "git is not installed or not on PATH"; return r; }
    if (dir.isEmpty())                        { r.error = "no directory given"; return r; }
    if (message.trim().isEmpty() && ! amend)  { r.error = "a commit message is required"; return r; }

    // `git commit` needs an author identity. Reuse the user's own (global/repo) config;
    // only if none is set do we supply a neutral fallback for this one commit, so a
    // never-configured machine (or CI) can still commit without us overwriting a real id.
    juce::StringArray args;
    juce::String cfg;
    if (runGit ({ "-C", dir, "config", "user.email" }, cfg) != 0 || cfg.trim().isEmpty())
    {
        args.add ("-c"); args.add ("user.email=gloopy@localhost");
        args.add ("-c"); args.add ("user.name=Gloopy");
    }
    args.add ("-C"); args.add (dir); args.add ("commit");
    if (amend) args.add ("--amend");
    if (amend && message.trim().isEmpty())
        args.add ("--no-edit");                           // amend without a new message → keep the old one
    else
    {
        args.add ("-m"); args.add (message);
    }

    juce::String out;
    if (runGit (args, out, /*alsoStderr*/ true) != 0)
    {
        r.error = out.trim().isNotEmpty() ? out.trim() : "git commit failed";
        return r;
    }
    r.ok = true;
    return r;
}

std::vector<MainComponent::GitCommitInfo> MainComponent::apiGitLog (const juce::String& dir, int maxCount)
{
    std::vector<GitCommitInfo> out;
    juce::String version;
    if (! apiGitAvailable (version) || dir.isEmpty())
        return out;

    const int n = maxCount > 0 ? maxCount : 50;
    // One commit per line; fields separated by the unit-separator byte (0x1f) so subjects
    // etc. can't collide with the delimiter. %h hash · %p parents · %D refs · %an author ·
    // %ad date · %s subject.
    const juce::String US = juce::String::charToString ((juce::juce_wchar) 0x1f);
    const juce::String fmt = "%h" + US + "%p" + US + "%D" + US + "%an" + US + "%ad" + US + "%s";

    juce::String res;
    if (runGit ({ "-C", dir, "log", "-n", juce::String (n), "--date=short",
                  "--pretty=format:" + fmt }, res) != 0)
        return out;                                     // not a repo / no commits yet

    for (auto& line : juce::StringArray::fromLines (res))
    {
        if (line.isEmpty()) continue;
        auto f = juce::StringArray::fromTokens (line, US, {});
        if (f.size() < 6) continue;
        GitCommitInfo c;
        c.hash    = f[0];
        c.parents = juce::StringArray::fromTokens (f[1], " ", {});
        c.parents.removeEmptyStrings();
        c.refs    = f[2];
        c.author  = f[3];
        c.date    = f[4];
        c.subject = f[5];
        out.push_back (std::move (c));
    }
    return out;
}

MainComponent::GitBranches MainComponent::apiGitBranches (const juce::String& dir)
{
    GitBranches b;
    juce::String version;
    if (! apiGitAvailable (version) || dir.isEmpty())
        return b;

    juce::String cur;
    if (runGit ({ "-C", dir, "branch", "--show-current" }, cur) == 0)
        b.current = cur.trim();

    juce::String list;
    if (runGit ({ "-C", dir, "for-each-ref", "--format=%(refname:short)", "refs/heads" }, list) == 0)
    {
        b.branches = juce::StringArray::fromLines (list);
        b.branches.removeEmptyStrings();
        b.branches.trim();
    }
    return b;
}

// Small helper for the branch write-ops: check git + a non-empty dir, then run the args.
static MainComponent::GitResult gitWrite (MainComponent& m, const juce::StringArray& args, const juce::String& dir)
{
    MainComponent::GitResult r;
    juce::String version;
    if (! m.apiGitAvailable (version)) { r.error = "git is not installed or not on PATH"; return r; }
    if (dir.isEmpty())                 { r.error = "no directory given"; return r; }
    // Defined in Git.cpp's anonymous namespace above; re-declare via a lambda-free call:
    juce::StringArray argv; argv.add ("git"); argv.addArray (args);
    juce::ChildProcess proc;
    if (! proc.start (argv, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    { r.error = "could not launch git"; return r; }
    const auto out = proc.readAllProcessOutput();
    proc.waitForProcessToFinish (10000);
    if (proc.getExitCode() != 0) { r.error = out.trim().isNotEmpty() ? out.trim() : "git command failed"; return r; }
    r.ok = true;
    return r;
}

MainComponent::GitResult MainComponent::apiGitBranchCreate (const juce::String& dir, const juce::String& name, const juce::String& startPoint)
{
    if (name.trim().isEmpty()) { GitResult r; r.error = "a branch name is required"; return r; }
    juce::StringArray args { "-C", dir, "branch", name.trim() };
    if (startPoint.trim().isNotEmpty()) args.add (startPoint.trim());
    return gitWrite (*this, args, dir);
}

MainComponent::GitResult MainComponent::apiGitCheckout (const juce::String& dir, const juce::String& ref)
{
    if (ref.trim().isEmpty()) { GitResult r; r.error = "a ref is required"; return r; }
    return gitWrite (*this, { "-C", dir, "checkout", ref.trim() }, dir);
}

MainComponent::GitResult MainComponent::apiGitMerge (const juce::String& dir, const juce::String& name)
{
    if (name.trim().isEmpty()) { GitResult r; r.error = "a branch to merge is required"; return r; }
    return gitWrite (*this, { "-C", dir, "merge", "--no-edit", name.trim() }, dir);
}

MainComponent::GitResult MainComponent::apiGitBranchDelete (const juce::String& dir, const juce::String& name, bool force)
{
    if (name.trim().isEmpty()) { GitResult r; r.error = "a branch name is required"; return r; }
    return gitWrite (*this, { "-C", dir, "branch", force ? "-D" : "-d", name.trim() }, dir);
}

MainComponent::GitResult MainComponent::apiGitBranchRename (const juce::String& dir, const juce::String& oldName, const juce::String& newName)
{
    if (oldName.trim().isEmpty() || newName.trim().isEmpty()) { GitResult r; r.error = "both names are required"; return r; }
    return gitWrite (*this, { "-C", dir, "branch", "-m", oldName.trim(), newName.trim() }, dir);
}

juce::StringArray MainComponent::apiGitTags (const juce::String& dir)
{
    juce::StringArray tags;
    juce::String version;
    if (! apiGitAvailable (version) || dir.isEmpty())
        return tags;
    juce::String out;
    // newest-created first, so recent milestone mixes sit at the top of the list
    if (runGit ({ "-C", dir, "for-each-ref", "--sort=-creatordate", "--format=%(refname:short)", "refs/tags" }, out) == 0)
    {
        tags = juce::StringArray::fromLines (out);
        tags.removeEmptyStrings();
        tags.trim();
    }
    return tags;
}

MainComponent::GitResult MainComponent::apiGitTagCreate (const juce::String& dir, const juce::String& name, const juce::String& message)
{
    GitResult r;
    juce::String version;
    if (! apiGitAvailable (version)) { r.error = "git is not installed or not on PATH"; return r; }
    if (dir.isEmpty())               { r.error = "no directory given"; return r; }
    if (name.trim().isEmpty())       { r.error = "a tag name is required"; return r; }

    juce::StringArray args;
    // An annotated tag (with a message) records a tagger identity, so reuse the same
    // fallback as commit when the user has none configured. A lightweight tag needs none.
    if (message.trim().isNotEmpty())
    {
        juce::String cfg;
        if (runGit ({ "-C", dir, "config", "user.email" }, cfg) != 0 || cfg.trim().isEmpty())
        {
            args.add ("-c"); args.add ("user.email=gloopy@localhost");
            args.add ("-c"); args.add ("user.name=Gloopy");
        }
        args.add ("-C"); args.add (dir); args.add ("tag"); args.add ("-a"); args.add (name.trim());
        args.add ("-m"); args.add (message);
    }
    else
    {
        args.add ("-C"); args.add (dir); args.add ("tag"); args.add (name.trim());
    }

    juce::String out;
    if (runGit (args, out, /*alsoStderr*/ true) != 0)
    {
        r.error = out.trim().isNotEmpty() ? out.trim() : "git tag failed";
        return r;
    }
    r.ok = true;
    return r;
}

MainComponent::GitResult MainComponent::apiGitTagDelete (const juce::String& dir, const juce::String& name)
{
    if (name.trim().isEmpty()) { GitResult r; r.error = "a tag name is required"; return r; }
    return gitWrite (*this, { "-C", dir, "tag", "-d", name.trim() }, dir);
}

MainComponent::GitDiff MainComponent::apiGitDiff (const juce::String& dir, const juce::String& pathspec,
                                                  const juce::String& revA, const juce::String& revB)
{
    GitDiff d;
    juce::String version;
    if (! apiGitAvailable (version)) { d.error = "git is not installed or not on PATH"; return d; }
    if (dir.isEmpty())               { d.error = "no directory given"; return d; }

    // The revision selector:
    //   both revs -> "revA revB"  (between two revisions)
    //   revA only -> "revA"       (revA .. working tree)
    //   neither   -> "HEAD"       (working tree vs the last commit — "what have I changed?")
    juce::StringArray range;
    const auto a = revA.trim(), b = revB.trim();
    if (a.isNotEmpty() && b.isNotEmpty()) { range.add (a); range.add (b); }
    else if (a.isNotEmpty())              { range.add (a); }
    else                                   { range.add ("HEAD"); }

    const auto ps = pathspec.trim();

    // The changed-file list (name-status) — the composition-text-aware summary: in a
    // Composition folder each path IS a track / clip / setting, so this names what changed.
    {
        juce::StringArray args { "-C", dir, "diff", "--name-status" };
        args.addArray (range);
        if (ps.isNotEmpty()) { args.add ("--"); args.add (ps); }
        juce::String out;
        if (runGit (args, out) == 0)
            for (auto& line : juce::StringArray::fromLines (out))
            {
                const auto t = line.trim();
                const auto tab = t.indexOfChar ('\t');
                if (t.isEmpty() || tab < 0) continue;
                GitDiffFile f;
                f.status = t.substring (0, tab).trim();
                f.path   = t.substring (tab + 1).trim();
                d.files.push_back (std::move (f));
            }
    }

    // The unified diff text.
    {
        juce::StringArray args { "-C", dir, "diff" };
        args.addArray (range);
        if (ps.isNotEmpty()) { args.add ("--"); args.add (ps); }
        juce::String out;
        if (runGit (args, out) != 0) { d.error = "git diff failed"; return d; }
        d.diff = out;
    }
    d.ok = true;
    return d;
}

MainComponent::GitResult MainComponent::apiGitDiscard (const juce::String& dir, const juce::StringArray& paths)
{
    // Throw away uncommitted (unstaged tracked) edits — VS Code's "Discard Changes".
    // `git checkout -- <paths|.>` restores tracked files to HEAD; untracked files are left
    // alone (discarding those would be silent data loss). Destructive — the desktop confirms.
    juce::StringArray args { "-C", dir, "checkout", "--" };
    if (paths.isEmpty()) args.add (".");
    else                 args.addArray (paths);
    return gitWrite (*this, args, dir);
}

MainComponent::GitResult MainComponent::apiGitStash (const juce::String& dir, const juce::String& message)
{
    juce::StringArray args { "-C", dir, "stash", "push" };
    if (message.trim().isNotEmpty()) { args.add ("-m"); args.add (message.trim()); }
    return gitWrite (*this, args, dir);
}

MainComponent::GitResult MainComponent::apiGitStashPop (const juce::String& dir)
{
    return gitWrite (*this, { "-C", dir, "stash", "pop" }, dir);
}

juce::StringArray MainComponent::apiGitStashList (const juce::String& dir)
{
    juce::StringArray list;
    juce::String version;
    if (! apiGitAvailable (version) || dir.isEmpty())
        return list;
    juce::String out;
    if (runGit ({ "-C", dir, "stash", "list" }, out) == 0)
    {
        list = juce::StringArray::fromLines (out);
        list.removeEmptyStrings();
    }
    return list;
}

MainComponent::GitResult MainComponent::apiGitRevert (const juce::String& dir, const juce::String& commit)
{
    GitResult r;
    juce::String version;
    if (! apiGitAvailable (version))   { r.error = "git is not installed or not on PATH"; return r; }
    if (dir.isEmpty())                 { r.error = "no directory given"; return r; }
    if (commit.trim().isEmpty())       { r.error = "a commit is required"; return r; }

    // revert creates a new commit, so it needs an author identity — same fallback as commit.
    juce::StringArray args;
    juce::String cfg;
    if (runGit ({ "-C", dir, "config", "user.email" }, cfg) != 0 || cfg.trim().isEmpty())
    {
        args.add ("-c"); args.add ("user.email=gloopy@localhost");
        args.add ("-c"); args.add ("user.name=Gloopy");
    }
    args.add ("-C"); args.add (dir); args.add ("revert"); args.add ("--no-edit"); args.add (commit.trim());

    juce::String out;
    if (runGit (args, out, /*alsoStderr*/ true) != 0)
    {
        r.error = out.trim().isNotEmpty() ? out.trim() : "git revert failed";
        return r;
    }
    r.ok = true;
    return r;
}

MainComponent::GitResult MainComponent::apiGitReset (const juce::String& dir, const juce::String& mode, const juce::String& ref)
{
    const auto m = mode.trim().isEmpty() ? juce::String ("mixed") : mode.trim();
    if (m != "soft" && m != "mixed" && m != "hard")
    { GitResult r; r.error = "reset mode must be soft, mixed, or hard"; return r; }
    const auto target = ref.trim().isEmpty() ? juce::String ("HEAD") : ref.trim();
    return gitWrite (*this, { "-C", dir, "reset", "--" + m, target }, dir);
}

MainComponent::GitResult MainComponent::apiGitAddRemote (const juce::String& dir, const juce::String& name, const juce::String& url)
{
    if (name.trim().isEmpty() || url.trim().isEmpty())
    { GitResult r; r.error = "a remote name and URL are required"; return r; }
    return gitWrite (*this, { "-C", dir, "remote", "add", name.trim(), url.trim() }, dir);
}

std::vector<MainComponent::GitRemote> MainComponent::apiGitListRemotes (const juce::String& dir)
{
    std::vector<GitRemote> out;
    juce::String version;
    if (! apiGitAvailable (version) || dir.isEmpty())
        return out;
    juce::String res;
    // `git remote -v` prints "<name>\t<url> (fetch)" and "... (push)"; keep the fetch rows.
    if (runGit ({ "-C", dir, "remote", "-v" }, res) == 0)
        for (auto& line : juce::StringArray::fromLines (res))
        {
            if (! line.contains ("(fetch)")) continue;
            auto f = juce::StringArray::fromTokens (line, " \t", {});
            f.removeEmptyStrings();
            if (f.size() >= 2) out.push_back ({ f[0], f[1] });
        }
    return out;
}

MainComponent::GitResult MainComponent::apiGitFetch (const juce::String& dir, const juce::String& remote)
{
    juce::StringArray args { "-C", dir, "fetch" };
    if (remote.trim().isNotEmpty()) args.add (remote.trim());
    else                            args.add ("--all");
    return gitWrite (*this, args, dir);
}

MainComponent::GitResult MainComponent::apiGitPull (const juce::String& dir, const juce::String& remote, const juce::String& branch)
{
    juce::StringArray args { "-C", dir, "pull" };
    if (remote.trim().isNotEmpty()) { args.add (remote.trim()); if (branch.trim().isNotEmpty()) args.add (branch.trim()); }
    return gitWrite (*this, args, dir);
}

MainComponent::GitResult MainComponent::apiGitPush (const juce::String& dir, const juce::String& remote, const juce::String& branch)
{
    // Push is outward-facing — always an explicit user action (never auto). Set the upstream
    // (-u) when a remote+branch are named so ahead/behind tracking works afterward.
    juce::StringArray args { "-C", dir, "push" };
    if (remote.trim().isNotEmpty())
    {
        args.add ("-u"); args.add (remote.trim());
        if (branch.trim().isNotEmpty()) args.add (branch.trim());
    }
    return gitWrite (*this, args, dir);
}

juce::StringArray MainComponent::apiGitConflicts (const juce::String& dir)
{
    juce::StringArray out;
    juce::String version;
    if (! apiGitAvailable (version) || dir.isEmpty())
        return out;
    juce::String res;
    // Unmerged paths (diff-filter=U) — the files with conflict markers to resolve.
    if (runGit ({ "-C", dir, "diff", "--name-only", "--diff-filter=U" }, res) == 0)
    {
        out = juce::StringArray::fromLines (res);
        out.removeEmptyStrings();
        out.trim();
    }
    return out;
}

MainComponent::GitResult MainComponent::apiGitResolve (const juce::String& dir, const juce::String& path, const juce::String& mode)
{
    GitResult r;
    juce::String version;
    if (! apiGitAvailable (version)) { r.error = "git is not installed or not on PATH"; return r; }
    if (dir.isEmpty() || path.trim().isEmpty()) { r.error = "a directory and file are required"; return r; }
    const auto m = mode.trim();

    if (m == "ours" || m == "theirs")
    {
        // Take one whole side of the conflict.
        juce::String out;
        if (runGit ({ "-C", dir, "checkout", "--" + m, "--", path.trim() }, out, /*alsoStderr*/ true) != 0)
        { r.error = out.trim().isNotEmpty() ? out.trim() : "git checkout failed"; return r; }
    }
    else if (m == "both")
    {
        // Keep both sides: strip the conflict markers, leaving ours-then-theirs content.
        // The composition text format makes this a legible, valid result.
        auto file = juce::File (dir).getChildFile (path.trim());
        if (! file.existsAsFile()) { r.error = "conflicted file not found"; return r; }
        juce::StringArray lines;
        lines.addLines (file.loadFileAsString());
        juce::StringArray kept;
        for (auto& line : lines)
            if (! (line.startsWith ("<<<<<<<") || line.startsWith ("=======") || line.startsWith (">>>>>>>")))
                kept.add (line);
        file.replaceWithText (kept.joinIntoString ("\n"));
    }
    else { r.error = "resolve mode must be ours, theirs, or both"; return r; }

    // Mark resolved by staging the file.
    return apiGitAdd (dir, { path.trim() });
}

MainComponent::GitResult MainComponent::apiGitMergeContinue (const juce::String& dir)
{
    GitResult r;
    juce::String version;
    if (! apiGitAvailable (version)) { r.error = "git is not installed or not on PATH"; return r; }
    if (dir.isEmpty())              { r.error = "no directory given"; return r; }

    // Finishing a merge writes a merge commit, so it needs an author identity (same fallback
    // as commit). `commit --no-edit` uses the prepared MERGE_MSG.
    juce::StringArray args;
    juce::String cfg;
    if (runGit ({ "-C", dir, "config", "user.email" }, cfg) != 0 || cfg.trim().isEmpty())
    {
        args.add ("-c"); args.add ("user.email=gloopy@localhost");
        args.add ("-c"); args.add ("user.name=Gloopy");
    }
    args.add ("-C"); args.add (dir); args.add ("commit"); args.add ("--no-edit");
    juce::String out;
    if (runGit (args, out, /*alsoStderr*/ true) != 0)
    { r.error = out.trim().isNotEmpty() ? out.trim() : "git commit failed"; return r; }
    r.ok = true;
    return r;
}

MainComponent::GitResult MainComponent::apiGitMergeAbort (const juce::String& dir)
{
    return gitWrite (*this, { "-C", dir, "merge", "--abort" }, dir);
}

MainComponent::GitResult MainComponent::apiGitSetIdentity (const juce::String& dir, const juce::String& name, const juce::String& email)
{
    GitResult r;
    juce::String version;
    if (! apiGitAvailable (version)) { r.error = "git is not installed or not on PATH"; return r; }
    if (dir.isEmpty())               { r.error = "no directory given"; return r; }
    // Per-repo (local) identity — doesn't touch the user's global config.
    if (name.trim().isNotEmpty())
    {
        auto rr = gitWrite (*this, { "-C", dir, "config", "user.name", name.trim() }, dir);
        if (! rr.ok) return rr;
    }
    if (email.trim().isNotEmpty())
    {
        auto rr = gitWrite (*this, { "-C", dir, "config", "user.email", email.trim() }, dir);
        if (! rr.ok) return rr;
    }
    r.ok = true;
    return r;
}

MainComponent::GitIdentity MainComponent::apiGitGetIdentity (const juce::String& dir)
{
    GitIdentity id;
    juce::String version;
    if (! apiGitAvailable (version) || dir.isEmpty())
        return id;
    juce::String out;
    if (runGit ({ "-C", dir, "config", "--get", "user.name" }, out) == 0)  id.name  = out.trim();
    if (runGit ({ "-C", dir, "config", "--get", "user.email" }, out) == 0) id.email = out.trim();
    return id;
}

MainComponent::GitResult MainComponent::apiGitSetAutoCommit (const juce::String& dir, bool on)
{
    // Stored in the repo's own config (out of band, like all git state) — opt-in only.
    return gitWrite (*this, { "-C", dir, "config", "--bool", "gloopy.autocommit", on ? "true" : "false" }, dir);
}

bool MainComponent::apiGitGetAutoCommit (const juce::String& dir)
{
    juce::String version;
    if (! apiGitAvailable (version) || dir.isEmpty())
        return false;
    juce::String out;
    return runGit ({ "-C", dir, "config", "--bool", "--get", "gloopy.autocommit" }, out) == 0
           && out.trim() == "true";
}

MainComponent::GitResult MainComponent::apiGitAutoCommitOnSave (const juce::String& dir)
{
    GitResult r; r.ok = true;
    // No-op unless the repo opted in AND has staged/unstaged changes to record.
    if (dir.isEmpty() || ! apiGitGetAutoCommit (dir)) return r;
    auto st = apiGitStatus (dir);
    if (! st.isRepo || st.changes.empty()) return r;
    auto add = apiGitAdd (dir, {});
    if (! add.ok) return add;
    // Templated auto-save message (no clock dependency in the hot path — a fixed prefix).
    return apiGitCommit (dir, "Auto-save (Gloopy)", /*amend*/ false);
}

juce::String MainComponent::gitHistoryReport()
{
    juce::String version;
    if (! apiGitAvailable (version))
        return "git is not installed or not on PATH.";

    // Resolve the open project's dir (same rule as apiGitStatus, via a default dir).
    auto s = apiGitStatus();
    if (s.dir.isEmpty())
        return "No saved project — save as a Composition folder to track it in git.";
    if (! s.isRepo)
        return "Not a git repository yet — save the project (Ctrl+S) to initialise git.";

    auto commits = apiGitLog (s.dir, 100);
    if (commits.empty())
        return "No commits yet — make your first commit with File \xe2\x86\x92 Commit...";

    juce::String text;
    text << "History of " << s.dir << " (" << (int) commits.size() << " commits, newest first):\n\n";
    for (auto& c : commits)
    {
        text << c.hash;
        if (c.refs.isNotEmpty()) text << "  (" << c.refs << ")";
        text << "  " << c.subject << "\n";
        text << "        " << c.author << " \xc2\xb7 " << c.date;
        if (c.parents.size() > 1) text << " \xc2\xb7 merge of " << c.parents.size() << " parents";
        text << "\n";
    }
    return text;
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
                      "         (save the project to initialise git — every saved folder is a repo).\n";

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
