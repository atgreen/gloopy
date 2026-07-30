---
name: make-release
version: 1.0.0
description: |
  Cut a tagged release of GLOOPY (~/git/gloopy). Invoke when the user says
  "make a release", "cut a release", "release gloopy", "tag a release", "ship
  X.Y.Z", or runs /make-release. Drives the whole flow: pick the SemVer version,
  roll the CHANGELOG + package versions, commit and tag on cave (upstream), let the
  mirror carry the tag to GitHub where release.yaml builds and attaches the RPM/DEB/
  Windows zip+installer and publishes the dnf/apt repos, then verifies everything
  landed. Encodes the repo's release landmines (cave-is-upstream, GPG signing,
  version flow, Pages, cave-down retries) so a release goes out correctly.
---

# make-release — cut a tagged Gloopy release

Gloopy releases are **tag-driven**: pushing a `v*` tag to cave (upstream) mirrors it
to GitHub, where `.github/workflows/release.yaml` builds every package, attaches them
to the GitHub Release, and publishes the `dnf`/`apt` repos to GitHub Pages. This skill
is the process for doing that safely.

**Read `AGENTS.md` first** — the operational landmines there are authoritative.

## Landmines (non-negotiable)

- **cave is UPSTREAM.** Push commits *and tags* to `origin`
  (`cave@cave.moxielogic.com:atgreen/gloopy.git`). A post-receive hook mirrors to
  `github.com/atgreen/gloopy`, which is a **read-only mirror — NEVER push to GitHub
  directly.** `release.yaml` runs on the mirror, so the tag must reach GitHub *via the
  mirror*.
- **cave is intermittently down.** Every push (commit and tag) may need retries — use
  an `until timeout 90 git push … ; do sleep 30; done` loop, not a one-shot.
- **Check CI with the token cleared:** `env -u GITHUB_TOKEN gh … -R atgreen/gloopy`
  (the ambient `GITHUB_TOKEN` is invalid).
- **GPG signing.** The committed `packaging/gloopy.repo` sets `gpgcheck=1`/`repo_gpgcheck=1`
  and the apt line is `signed-by=…`. If the **`RPM_GPG_PRIVATE_KEY`** secret is NOT set,
  the repos publish **unsigned** and users' `dnf`/`apt` will reject them. Before tagging,
  confirm the secret is set — or agree with the user to flip the repo config to unsigned
  for this release.
- **Version flow.** The tag drives the version everywhere — `release.yaml` derives it
  from `github.ref_name` for the RPM (`_gloopy_version`), the DEB (stamps
  `debian/changelog`), and the Windows installer. So just tag `vX.Y.Z` correctly; you do
  NOT hand-edit package versions.
- **GitHub Pages self-disables.** It has toggled off unprompted before; after the repos
  publish, verify the Pages site serves and re-enable via
  `gh api -X POST repos/atgreen/gloopy/pages …` if needed.
- This is an **outward-facing publish.** Confirm the version and that the user wants to
  release before tagging.

## Preconditions (check, don't assume)

1. On `main`, working tree clean (`git status`), local == `origin/main`.
2. **CI is green** — the latest `CI`, `Windows`, and `docs` runs succeed
   (`env -u GITHUB_TOKEN gh run list -R atgreen/gloopy`). A red Windows build means the
   release zip/installer won't build; don't tag over it.
3. Decide the **version** with the user — SemVer `X.Y.Z`, no leading `v` here (the tag
   adds it). Pre-1.0, `0.y.z`.
4. Decide **GPG signing** (secret present?) — see landmines.

## Roll the version + CHANGELOG (one commit)

1. **CHANGELOG.md** — move the `[Unreleased]` block to a new `## [X.Y.Z] - YYYY-MM-DD`
   section (use today's real date — check it in the shell), leave a fresh empty
   `[Unreleased]`, and add/refresh the link refs at the bottom. This is the ONE file you
   roll by hand.
2. **Package versions are automatic** — `release.yaml` derives the RPM/DEB/installer
   version from the tag, so nothing to edit in `packaging/gloopy.spec` or
   `debian/changelog`. (Optionally add a real `debian/changelog` prose entry for a proper
   Debian changelog, but the *version* is stamped from the tag either way.)
3. Commit: `release: X.Y.Z` (the CHANGELOG roll), then push to cave (retry loop).

## Tag and push

```sh
git tag -a vX.Y.Z -m "Gloopy X.Y.Z"
# push the tag to cave (upstream); the mirror carries it to GitHub
until timeout 90 git push origin vX.Y.Z >/dev/null 2>&1; do sleep 30; done
```

Then **verify the tag reached GitHub** (the mirror must carry it, or release.yaml never
fires): `env -u GITHUB_TOKEN gh api repos/atgreen/gloopy/git/refs/tags/vX.Y.Z`. If it
didn't mirror, push a trivial commit to cave to re-trigger the mirror, or investigate.

## Monitor the release run

`release.yaml` on the tag runs, in this dependency order:
- `rpm`, `deb`, `windows` → build the packages and **attach them to the GitHub Release**
  (the `if: startsWith(github.ref,'refs/tags/')` "Upload to release" steps).
- `build-rpm-repo` (needs `rpm`) → `createrepo_c` + rewrite hrefs to the Release assets.
- `build-deb-repo` (needs `deb`) → `pool/` + `dists/` via `dpkg-scanpackages`.
- `deploy-pages` (needs both repo jobs) → publishes `/rpm-repo` + `/deb-repo` onto
  `gh-pages` (beside the landing page + manual). Note `deploy-pages` depends only on the
  rpm/deb jobs, **not** windows — a Windows failure won't block the repos, but you still
  want it green for the zip/installer assets.

Watch it: `env -u GITHUB_TOKEN gh run watch <run-id> -R atgreen/gloopy` (or poll
`gh run view`). The `docs` workflow also fires on the tag and `mike deploy`s the version
as `latest`.

## Verify (the release isn't done until these pass)

1. **GitHub Release** exists for `vX.Y.Z` with the assets: `gloopy-*.rpm`, `gloopy_*.deb`,
   `gloopy-windows-x64.zip`, `gloopy-setup-x64.exe` (+ `.dsc/.changes` if produced).
   `env -u GITHUB_TOKEN gh release view vX.Y.Z -R atgreen/gloopy`.
2. **Repos serve** — `curl -sI https://atgreen.github.io/gloopy/rpm-repo/repodata/repomd.xml`
   and `…/deb-repo/dists/stable/Release` return 200. Re-enable Pages if it dropped.
3. **Package versions match the tag** — `rpm -qip` the asset / check the `.deb` name; catch
   the version-flow landmine here.
4. **A real install works** if you can: add the repo and `dnf install gloopy` / `apt
   install gloopy` (or at least `dnf install ./gloopy-*.rpm` locally on this Fedora box).
5. **Docs** — `https://atgreen.github.io/gloopy/latest/` serves the tagged manual.

## Announce / finish

Note the release in any changelog/README badges if present; tell the user the Release
URL and the install one-liners. If anything failed mid-way, the tag can be deleted from
cave (`git push origin :refs/tags/vX.Y.Z`, which mirrors the deletion) and re-cut after
fixing — but only if no one has pulled it yet.

## If it goes wrong

- **release.yaml red on rpm/deb** → the repos won't publish. Fix on `main`, and either
  re-run the failed jobs (`gh run rerun`) or delete+re-cut the tag.
- **cave down** → keep retrying the push loop; the tag is safe locally until it lands.
- **repos 404 after a green run** → Pages disabled itself; re-enable via the API.
- **users can't install (GPG)** → the `RPM_GPG_PRIVATE_KEY` secret wasn't set; either add
  it and re-run the repo jobs, or ship an unsigned `.repo`/apt line.
