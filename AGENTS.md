# AGENTS.md

This file applies to the entire `sekai-deck-recommend-cpp` repository. The
same engineering rules also live in `CLAUDE.md` and
`.github/copilot-instructions.md` — keep all three in sync when changing
guidance.

## Project Overview

`sekai-deck-recommend-cpp` is a C++20 Project Sekai deck recommendation and
calculation engine with Python bindings. It is used directly by Python callers
and by Team Haruki's `deck-service` through a C/Rust FFI bridge.

This is production scoring code. Changes to card power, deck selection, event
bonus, support deck, live score, userdata parsing, or masterdata loading can
change live automation behavior downstream.

## Repository Layout

- `src/`: core C++ engine sources.
- `src/deck-recommend/`: event, challenge live, MySekai, and base deck
  recommendation logic.
- `src/deck-information/`: fixed deck lookup and deck power aggregation.
- `src/card-information/`: card detail, power, skill, and image state logic.
- `src/live-score/`: live score and skill order calculations.
- `src/event-point/`: event point and event bonus calculations.
- `src/data-provider/`: static data, masterdata, music metas, and userdata
  loading.
- `src/user-data/`: user-data model parsers. This area must support both normal
  object payloads and compact array payloads used by downstream services.
- `src/master-data/`: masterdata model structs.
- `3rdparty/json/`: vendored nlohmann/json dependency.
- `sekai_deck_recommend.cpp` and `.pyi`: Python binding surface.
- `data/`: static data required by the engine.

## Build And Test

Common local checks:

```bash
pip install -e . -v
python -c "import sekai_deck_recommend_cpp"
```

When this repository is being changed for `deck-service`, also validate from
that sibling repository:

```bash
cd ../deck-service
cargo check
cargo build
```

If submodules are missing after cloning:

```bash
git submodule update --init --recursive
```

### WebAssembly build

Browser/Worker target (Embind binding via `src/sekai_deck_recommend_wasm.cpp`).
Requires `emsdk` activated in the shell:

```bash
mkdir build_wasm && cd build_wasm
emcmake cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j
```

Outputs ES6 module glue (`sekai_deck_recommend.js`) + `sekai_deck_recommend.wasm`.
Static files in `data/` are embedded into the wasm via `--embed-file`; runtime
masterdata/music-metas are pushed in by the JS caller.

## Engineering Rules

- Prefer small, behavior-focused changes over broad refactors.
- Preserve existing enum mapping and validation behavior unless a caller-visible
  migration is intentional.
- Keep parsers backward compatible with both object-shaped and compact
  array-shaped userdata when the field is used by downstream services.
- Be careful with `std::optional` fields in score details; missing bonus data
  should not silently become a different calculation.
- Do not change static data or generated assets unless the task explicitly
  needs it.
- For code used through deck-service, remember that C++ exceptions cross the
  boundary as error strings; write messages that help identify the bad input.
- Use concise comments only when the calculation or data shape is not obvious.

## Bindings

Two parallel binding files live next to the engine:

- `src/sekai_deck_recommend.cpp` — pybind11 binding for the Python wheel and
  the `deck-service` FFI bridge.
- `src/sekai_deck_recommend_wasm.cpp` — Embind binding for the WebAssembly
  build. JSON-in / JSON-out surface (`recommend(optionsJson)` returns a JSON
  string).

Option validation logic is duplicated between the two until a shared core is
extracted; when adding a field to `DeckRecommendOptions`, update both files
and keep their schemas identical.

## High-Risk Areas

- `src/live-score/live-calculator.cpp`: skill ordering, multi-live active bonus,
  and fixed live score timing.
- `src/deck-recommend/base-deck-recommend.h`: card filtering and shared deck
  recommendation config.
- `src/deck-recommend/deck-result-update.*`: result ordering and emitted detail
  fields consumed by deck-service.
- `src/data-provider/user-data.cpp` and `src/user-data/*`: downstream runtime
  userdata compatibility.
- `src/card-information/*`: power breakdowns and card state normalization.

## Git Commits

All commit subjects must follow:

```text
[Type] Short description starting with capital letter
```

Allowed types:

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
  commit body, not a free-form `Agent:` line. This makes GitHub render the
  co-author avatar on the commit page.
- The trailer must be on its own line, separated from the subject by a blank
  line, in the form `Co-authored-by: <Display Name> <email>`.

Suggested values per agent:

- Claude (any 4.x): `Co-authored-by: Claude Opus 4.7 <noreply@anthropic.com>`
  (substitute the actual model, e.g. `Claude Sonnet 4.6`)
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

Co-authored-by: Codex <noreply@openai.com>
```
