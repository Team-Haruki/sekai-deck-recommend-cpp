# Copilot Instructions

Treat this repository as production C++ scoring code for Project Sekai deck
recommendation and live calculation. The same engineering rules also live in
`AGENTS.md` and `CLAUDE.md` — keep all three in sync when changing guidance.

## Project Context

- Core language: C++20.
- Bindings: Python extension via pybind11.
- JSON: nlohmann/json from `3rdparty/json`.
- Downstream runtime: Team Haruki `deck-service` builds this repository and
  calls it through a C/Rust FFI bridge.

## Work Safely

- Keep changes narrow and calculation-focused.
- Preserve compatibility with object-shaped and compact array-shaped userdata.
- Do not rewrite scoring, event bonus, support deck, or skill order logic unless
  the task explicitly requires it.
- Avoid changing static data in `data/` unless the task is about static data.
- Prefer clear validation errors for malformed options and missing userdata.

## Important Paths

- `src/live-score/`: live score, skill order, and timing calculations.
- `src/deck-recommend/`: deck recommendation algorithms and result updates.
- `src/deck-information/`: fixed deck services and deck detail calculation.
- `src/card-information/`: card power, skill, and image state calculation.
- `src/data-provider/`: masterdata/music meta/userdata loading.
- `src/user-data/`: userdata parsers used by downstream services.
- `sekai_deck_recommend.cpp`: Python binding entry point.

## Build Checks

```bash
pip install -e . -v
python -c "import sekai_deck_recommend_cpp"
```

For deck-service integration changes, also run from the sibling repo:

```bash
cd ../deck-service
cargo check
cargo build
```

## Git Commits

All commit subjects must follow:

```text
[Type] Short description starting with capital letter
```

Allowed types: `[Feat]`, `[Fix]`, `[Chore]`, `[Docs]`.

Rules:

- Description starts with a capital letter.
- Use imperative mood: `Add ...`, not `Added ...`.
- No trailing period.
- Keep the subject at or below roughly 70 characters.
- Agent attribution uses a standard `Co-authored-by:` trailer in the commit
  body, separated from the subject by a blank line.

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
