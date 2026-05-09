# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with
code in this repository. The same engineering rules also live in `AGENTS.md`
and `.github/copilot-instructions.md` — keep all three in sync when changing
guidance.

## What This Is

A C++20 Project Sekai deck recommendation and live-score engine with Python
bindings (pybind11). Two production callers consume it:

- Python: `import sekai_deck_recommend_cpp` directly via the binding surface in
  `sekai_deck_recommend.cpp` / `.pyi`.
- Team Haruki `deck-service` (sibling repo): builds this code through a Rust
  FFI bridge and exposes it as an HTTP service. C++ exceptions cross the FFI
  boundary as error strings, so error messages must identify the bad input.

This is production scoring code. Changes to card power, deck selection, event
bonus, support deck, live score, userdata parsing, or masterdata loading change
live automation behavior downstream.

## Repository Layout

- `src/deck-recommend/`: event, challenge live, MySekai, and base deck
  recommendation logic; result ordering for emitted detail fields.
- `src/deck-information/`: fixed deck lookup and deck power aggregation.
- `src/card-information/`: card detail, power, skill, and image state.
- `src/live-score/`: live score, skill ordering, multi-live active bonus,
  fixed-live timing.
- `src/event-point/`: event point and event bonus calculations.
- `src/data-provider/`: static data, masterdata, music metas, userdata loaders.
- `src/user-data/`: userdata model parsers — must support both normal object
  payloads and compact array payloads used by downstream services.
- `src/master-data/`: masterdata model structs.
- `3rdparty/json/`: vendored nlohmann/json (submodule).
- `sekai_deck_recommend.cpp` / `.pyi`: Python binding surface.
- `data/`: static data required by the engine. Do not change unless the task
  is explicitly about static data.

## Build & Run

Prerequisites: CMake ≥ 3.15, C++20 compiler (GCC/Clang/MSVC), Python 3.10+
with development headers.

```bash
# If submodules are missing
git submodule update --init --recursive

# Local install + smoke test
pip install -e . -v
python -c "import sekai_deck_recommend_cpp"
```

When changes are intended for `deck-service`, also validate from the sibling
repo:

```bash
cd ../deck-service
cargo check
cargo build
```

## Code Conventions

- C++20, headers and implementations live next to each other in `src/<area>/`.
- JSON parsing goes through nlohmann/json from `3rdparty/json` — do not pull
  in another JSON library.
- Prefer small, behavior-focused changes over broad refactors.
- Preserve existing enum mapping and validation behavior unless a
  caller-visible migration is intentional.
- Keep userdata parsers backward compatible with both object-shaped and
  compact array-shaped payloads when the field is used downstream.
- Be careful with `std::optional` fields in score details — missing bonus
  data must not silently turn into a different calculation result.
- Use concise comments only when the calculation or data shape is non-obvious.
  Don't restate what the code already says.

## High-Risk Areas

These spots have produced production-visible regressions in the past — read
the surrounding code before changing them, and prefer adding tests or running
the deck-service integration build:

- `src/live-score/live-calculator.cpp` — skill ordering, multi-live active
  bonus, fixed live score timing.
- `src/deck-recommend/base-deck-recommend.{h,cpp}` — card filtering and the
  shared deck recommendation config used by every recommender.
- `src/deck-recommend/deck-result-update.*` — result ordering and emitted
  detail fields consumed by `deck-service`.
- `src/data-provider/user-data.cpp` and `src/user-data/*` — downstream
  runtime userdata compatibility (object vs compact array shape).
- `src/card-information/*` — power breakdown and card state normalization.

## Git Commit Format

All commit subjects must follow:

```text
[Type] Short description starting with capital letter
```

| Type      | Usage                                                 |
|-----------|-------------------------------------------------------|
| `[Feat]`  | New feature or capability                             |
| `[Fix]`   | Bug fix                                               |
| `[Chore]` | Maintenance, refactoring, dependency or build changes |
| `[Docs]`  | Documentation-only changes                            |

Rules:

- Description starts with a capital letter.
- Use imperative mood: `Add ...`, not `Added ...`.
- No trailing period.
- Keep the subject at or below roughly 70 characters.
- Agent attribution uses the standard Git `Co-authored-by:` trailer in the
  commit body, on its own line, separated from the subject by a blank line.

Suggested values per agent:

- Claude (any 4.x): `Co-authored-by: Claude Opus 4.7 <noreply@anthropic.com>`
  (substitute the actual model, e.g. `Claude Sonnet 4.6`, `Claude Haiku 4.5`)
- Codex: `Co-authored-by: Codex <noreply@openai.com>`
- Copilot: `Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>`

Project examples:

```text
[Feat] Add MySekai bonus target support
[Fix] Parse compact runtime user cards
[Chore] Update pybind11 build metadata
[Docs] Document deck recommend options
```

Agent-authored commit example:

```text
[Docs] Add agent commit guidelines

Co-authored-by: Claude Opus 4.7 <noreply@anthropic.com>
```
