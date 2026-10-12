---
id: 0
status: done
created_at: 2026-10-12T15:04:06Z
updated_at: 2026-10-12T15:04:51Z
---
# Bootstrap Clew

## Goal

Connect Clew to your agent instructions so future agents can find and use project work items.

## Instructions

Copy the section between the `====` lines below and paste it into the persistent instruction artifact your coding harness reads (`AGENTS.md`, `CLAUDE.md`, Cursor rules, Codex instructions, a skill, or equivalent project-level instruction file).

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

## Done when

- [ ] The agent contract above is copied into your preferred persistent agent instruction artifact.
- [ ] `.clew/` and the instruction artifact are committed together.
- [ ] A future agent can run `clew list` and understand how to continue work.
- [ ] This bootstrap is marked done with `clew done 0000`.
