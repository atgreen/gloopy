# Writing docs

How the Gloopy manual is built and the rules for contributing to it. This is the
short version of the documentation strategy — enough to write a good page and get
it merged.

## Docs-as-code

Documentation is Markdown in the repo, reviewed in PRs like code. The site is
[Material for MkDocs](https://squidfunk.github.io/mkdocs-material/); build it
locally with:

```sh
pip install -r requirements-docs.txt
mkdocs serve
```

## Structure: Diátaxis

Every page is **exactly one** of four kinds. Mixing them is the most common way
manuals go wrong.

| Type | Serves | Written as |
|------|--------|-----------|
| **Tutorial** | learning | A lesson: hand-held, guaranteed to succeed, names exactly one path — no options. |
| **How-to guide** | a task | A recipe: assumes competence, solves one real problem. |
| **Reference** | looking things up | Dry, complete, structured. No rationale. |
| **Explanation** | understanding | An essay: gives context, admits alternatives. |

Two failure modes to catch in review:

1. **A tutorial turning into reference** — the writer lists every option and the
   beginner drowns. A tutorial names one path.
2. **Reference turning into explanation** — design rationale creeps into an API
   description. Move it to an explanation page and link.

> The reviewer question that catches most problems: **which of the four is this
> page, and is anything in it from a different one?**

## Two front doors, one core

The nav splits by audience: **User guide** (musicians, producers) and
**Control & scripting** (integrators, script authors, hardware vendors). They
barely overlap, so they get separate front doors.

But the **domain model is documented once**, in
[Control & scripting → Concepts → The Gloopy model](../control-scripting/concepts/model.md),
written language-agnostically. Every other page — Python, OSC, gRPC, Lisp, user
guide — **links** there rather than re-explaining what a clip or a scene is. If
the Python guide and the OSC guide each define "session slot" in their own words,
they will contradict each other within a year. Don't let them.

## Generated reference

Four references are generated from their source of truth, never hand-edited:

| Reference | Generated from | Tool |
|-----------|----------------|------|
| gRPC | `proto/gloopy.proto` comments | `buf generate` → `protoc-gen-doc` |
| OSC | `osc.yaml` schema | our Jinja generator |
| Python | `python/gloopy/client.py` docstrings | mkdocstrings (inline, at build) |
| Common Lisp | `common-lisp/src/` docstrings | 40ants-doc |

Generated Markdown is **git-ignored** and rebuilt in CI — committing it causes
noisy diffs and drift. Edit the *source*, not the output.

## Versioning

The control API has external consumers pinned to old versions (a hardware vendor
built against 0.4 needs the 0.4 reference to stay up). Docs are versioned with
`mike`, and **doc versions track the protocol version, not just the app version**
— if the app bumps with no protocol change, say so rather than implying a new API.

## Rules for contributors

For the `CONTRIBUTING` file:

- Documentation changes go through PRs, like code.
- Every page is exactly one Diátaxis type. **Say which in the PR description.**
- Comments in `proto/gloopy.proto` are **published docs** — write them for users:
  every service, method, message, and field gets a sentence-case comment with
  units and ranges.
- New OSC addresses are added to the `osc.yaml` schema **first**. The
  implementation follows the schema, not the other way round.
- Concepts are explained once, in
  [Concepts](../control-scripting/concepts/model.md), and linked to. Do not
  re-explain the domain model in a language-specific guide.
- Never edit generated Markdown. Edit the source it came from.

## Printable manual

DAW users expect a printable manual; library users don't. The PDF build
(`mkdocs-with-pdf`) covers the **User guide only** — the API reference is useless
on paper and enormous.
