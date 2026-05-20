# Copilot Instructions

Treat this repository as production C++ scoring code for Project Sekai deck
recommendation and live calculation. The same engineering rules also live in
`AGENTS.md` and `CLAUDE.md` — keep all three in sync when changing guidance.

## Project Context

- Core language: C++20.
- Bindings: Python extension via pybind11 and WebAssembly via Embind.
- Package names: PyPI and npm both use `haruki-sekai-deck-recommend-cpp`.
- JSON: nlohmann/json from `3rdparty/json`.
- Downstream runtime: Team Haruki `deck-service` builds this repository and
  calls it through a C/Rust FFI bridge.

## Work Safely

- Keep changes narrow and calculation-focused.
- Treat production userdata as object/dictionary shaped. Compact arrays can
  appear in Mongo-exported local fixtures, but they are not the runtime API
  contract.
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

For the WebAssembly target (Embind binding, browser/Worker), with `emsdk`
activated:

```bash
mkdir build_wasm && cd build_wasm
emcmake cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j
```

Two parallel binding files live in `src/`: `sekai_deck_recommend.cpp`
(pybind11) and `sekai_deck_recommend_wasm.cpp` (Embind, JSON-in / JSON-out).
Option validation is duplicated; keep both in sync when adding a field.

The npm package scaffold lives in `npm/haruki-sekai-deck-recommend-cpp`. Do not
bundle masterdata, music metas, or user data into the npm package; the
application provides them at runtime. Local benchmark fixtures may live beside
this repository as `../haruki-sekai-master`, `../music_metas.json`, and
`../collections.suite.json`; do not commit those fixture files.

Release workflows build PyPI wheels, PyPI sdist, and the npm wasm package. PyPI
and npm publishing use Trusted Publishing/OIDC through the `pypi-publish` and
`npm-publish` environments. PyPI publishing should only download artifacts
prefixed with `pypi-`.

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
- Before creating any commit, ask the user whether this commit should publish a
  new version.
- If the user wants a new version, bump the relevant package versions according
  to the user's requested release level. Versions must use `major.minor.patch`
  format.
- If the user does not want a new version, create the commit without changing
  package versions.

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
