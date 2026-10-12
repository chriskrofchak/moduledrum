# Clew — Project State

This directory holds Clew project state (increments, archive, path).

## Agent Guidance

In order for your agent to use Clew:

Copy between the ==== lines below and paste it into the persistent instruction artifact your coding harness reads (`AGENTS.md`, `CLAUDE.md`, Cursor rules, Codex instructions, a skill, or equivalent):

================================================

## Clew workflow

### Core concepts

- **Increment** — a standalone unit of work that should leave the codebase stable, tested, and committable when complete.
- **Task** — a Markdown checkbox inside an Increment.
- **Path** — the hand-curated priority order across active Increments, stored in `.clew/path.md`.
- **Archive** — completed or abandoned Increments moved to `.clew/archive/`.
- **Tag** — lightweight frontmatter classification for filtering, such as `bug`, `needs-info`, or `p0` etc...

### Use Clew to track work in this project

- If no ID was given, run `clew list` (or `clew next`) to pick one.
- Run `clew show <id>` to read the full increment body before implementing.
- Run `clew start <id>` before beginning work.
- Keep discoveries, decisions, and remaining tasks in the Increment markdown.
- Run `clew done <id>` only after the code is stable and project checks pass; then commit code with the `.clew/` changes.
- Create new work with `clew new "Short title"`; pass a Markdown body on stdin for detail.

### Detailed Increment Creation Example

Create an increment with a Markdown body by passing non-TTY stdin:

```bash
clew new "Add OAuth routes" --tag needs-info --tag auth  <<'EOF'
## Requirements & the 'Why'

What's essential and why this increment matters.

## Acceptance criteria

- [ ] Criterion 1
...
- [ ] Criterion N

## Tasks

- [ ] First task
EOF
```

### Additional Information

See `.clew/README.md`, `clew --help`, or `clew <command> --help` for more detailed information.

================================================

## Common commands

_(Optionally you can also include this in your Agents.md to give your agent more Clew capabilities)_

| Need                       | Command                            |
| -------------------------- | ---------------------------------- |
| See active work            | `clew list`                        |
| See archived/abandoned too | `clew list --all`                  |
| Filter by status           | `clew list --status todo`          |
| Filter by tag              | `clew list --tag bug`              |
| Pick next work             | `clew next`                        |
| Pick and start next work   | `clew next --start`                |
| Read an Increment          | `clew show 0024`                   |
| Create an Increment        | `clew new "Short title"`           |
| Create with body           | `clew new "Short title" < body.md` |
| Create as child of another | `clew new "Child" --parent 0024`   |
| Add tags                   | `clew tag 0024 bug p0`             |
| Remove tags                | `clew untag 0024 p0`               |
| Start work                 | `clew start 0024`                  |
| Finish work                | `clew done 0024`                   |
| Mark blocked (with reason) | `clew block 0024 "waiting on API"` |
| Clear blocked flag         | `clew unblock 0024`                |
| Abandon work (with reason) | `clew abandon 0024 "obsolete"`     |
| Reopen archived work       | `clew reopen 0024`                 |
| Edit path priority         | `clew path`                        |
| Audit `.clew/` for drift   | `clew lint`                        |
| Renumber an Increment ID   | `clew renumber 24 34`              |

## Creating increments

Create a backlog item with a title heading:

```bash
clew new "Add OAuth routes"
```

Create an increment with a Markdown body by passing non-TTY stdin:

```bash
clew new "Add OAuth routes" <<'EOF'
## Context

Why this increment matters.

## Tasks

- [ ] First task
EOF
```

Attach tags at capture time with repeated singular `--tag` flags:

```bash
clew new "Verify Clew on WSL" --tag windows --tag distribution <<'EOF'
## Goal
Verify Clew works on WSL.
EOF
```

Link a child to an existing increment with `--parent`:

```bash
clew new "Add OAuth callback handler" --parent 0024
```

`--parent` writes a `parent:` field in the child's frontmatter. It is a link only — Clew does not yet roll children up into the parent's status, path position, or lifecycle. Use it for discoverability, not for epic automation.

Clew writes frontmatter itself; stdin is body content only. Passing stdin replaces the default title-heading body. Tags must match `[a-z0-9][a-z0-9-]*`.

For existing increments, use:

```bash
clew tag 0019 windows p0
clew untag 0019 windows
```

## Blocking

Blocking is a separate flag, not a lifecycle status. An increment can be `todo` or `doing` and simultaneously blocked.

```bash
clew block 0024 "waiting on upstream fix in repo-x"
clew unblock 0024
```

`clew block` writes a `blocked_reason:` field in the increment's frontmatter; `clew unblock` clears it. The `status:` value is untouched. `clew list` still shows blocked work — use `clew list` output (or `clew show <id>`) to see the reason. Pair with the `needs-info` tag below when you are blocked specifically on a clarifying answer.

## Triage tags (Matt Pocock convention)

Tags are free-form, but these conventions fit into [Matt Pocock's skills](https://github.com/mattpocock/skills/tree/main) and are generally useful for Agentic Development loops.

- `needs-info` — waiting on a clarifying answer. If progress is actually blocked, also run `clew block`.
- `ready-for-agent` — fully specified; an agent can pick this up without further conversation. Optional; not a substitute for `status: todo`.
- `ready-for-human` — requires human judgment, manual steps, or external access.
- `needs-triage` — needs maintainer review before it is workable.

Adopt the subset that fits your team. Tags must match `[a-z0-9][a-z0-9-]*`.

## Reviewing archive moves

`clew done`, `clew abandon`, and `clew reopen` move increment files with normal filesystem renames. Clew does not run `git mv` or mutate the git index.

Before staging, `git status --short` may show a deleted file plus an untracked file or directory. After `git add -A`, git normally reports the move as a rename:

```text
R  .clew/increments/0001-example.md -> .clew/archive/0001-example.md
```
