# AGENTS.md

This file applies to the entire `sekai-deck-recommend-cpp` repository and is
the single source of truth for it. The same engineering rules also live in
`.github/copilot-instructions.md` — keep both in sync when changing guidance.

## Project Overview

`sekai-deck-recommend-cpp` is Team Haruki's maintained fork of the C++ Project
Sekai deck recommendation and calculation engine. The previous C++ optimization
project is [NeuraXmy/sekai-deck-recommend-cpp](https://github.com/NeuraXmy/sekai-deck-recommend-cpp).
Some modifications are based on
[moe-sekai/sekai-deck-recommend-cpp](https://github.com/moe-sekai/sekai-deck-recommend-cpp).
This fork ships Python bindings and a WebAssembly/npm package target. It is
used directly by Python callers, by browser/Worker callers through wasm, and by
Team Haruki's `deck-service` through a C/Rust FFI bridge.

This is production scoring code. Changes to card power, deck selection, event
bonus, support deck, live score, userdata parsing, or masterdata loading can
change live automation behavior downstream.

## Repository Layout

- `src/`: core C++ engine sources.
- `src/deck-recommend/`: event, challenge live, MySekai, and base deck
  recommendation logic; result ordering for emitted detail fields.
- `src/deck-information/`: fixed deck lookup and deck power aggregation.
- `src/card-information/`: card detail, power, skill, and image state logic.
- `src/live-score/`: live score, skill ordering, multi-live active bonus,
  and fixed-live timing calculations.
- `src/event-point/`: event point and event bonus calculations.
- `src/data-provider/`: static data, masterdata, music metas, and userdata
  loading.
- `src/user-data/`: user-data model parsers. Production API payloads are
  object/dictionary shaped. Compact arrays can appear in Mongo-exported local
  fixtures, but they are not the runtime API contract.
- `src/master-data/`: masterdata model structs.
- `3rdparty/yyjson/`: vendored yyjson dependency for masterdata, music metas,
  userdata parsing, and wasm JSON-in / JSON-out binding payloads.
- `src/sekai_deck_recommend.cpp` and `src/sekai_deck_recommend.pyi`: Python
  binding surface; `src/sekai_deck_recommend_wasm.cpp`: wasm binding (see
  Bindings below).
- `npm/haruki-sekai-deck-recommend-cpp/`: npm package scaffold for the wasm
  build.
- `tests/`: Python `unittest` suites (binding smoke/area-item tests and the
  bench tooling tests).
- `tools/bench/`: local benchmark and regression harness.
- `scripts/ci/`: CI helper scripts (`build-npm.sh`, `coverage.sh`).
- `data/`: static data required by the engine. `data/rl_seed_cache.tsv` is a
  runtime-generated RL warm-start cache (written when `DECK_DATA_DIR` or
  `DECK_RL_SEED_CACHE_FILE` is set). Only the default
  `data/rl_seed_cache.tsv` path is gitignored; add any custom in-repository
  cache path to `.gitignore`, and never commit generated cache files.

## Build And Test

Prerequisites: CMake ≥ 3.15, C++20 compiler (GCC/Clang/MSVC), Python 3.10+
with development headers.

Common local checks:

```bash
uv pip install -e . -v
uv run python -c "import sekai_deck_recommend_cpp"
```

`pip install -e . -v` remains supported for callers that are not using uv.

When this repository is being changed for `deck-service`, also validate from
that sibling repository:

```bash
cd ../deck-service
cargo check
cargo build
```

Python tests (the same suites `scripts/ci/coverage.sh` runs in CI, without the
coverage instrumentation). The binding tests import the bare native module from
`SEKAI_BINDING_DIR`:

```bash
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release
cmake --build build/native -j
SEKAI_BINDING_DIR="$PWD/build/native" python -m unittest discover -s tests -p 'test_binding*.py'
python -m unittest discover -s tests -p 'test_bench_tools.py'
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
The CMakeLists branches on `EMSCRIPTEN`: the pybind11 binding is dropped and the
Embind binding is linked instead. Static files in `data/` are embedded into the
wasm via `--embed-file`; runtime masterdata/music-metas are pushed in by the JS
caller.

The npm package scaffold lives in `npm/haruki-sekai-deck-recommend-cpp` and is
reserved as `haruki-sekai-deck-recommend-cpp`. It should contain only the wasm
loader, `.wasm` binary, wrapper helpers, and TypeScript declarations. Do not
bundle masterdata, music metas, or user data in the npm package; the application
provides them at runtime.

Local benchmark fixtures default to (see `tools/bench/common.py`; override with
`SEKAI_BENCH_MASTERDATA`, `SEKAI_BENCH_MUSICMETAS`, `SEKAI_BENCH_USERDATA`):

- masterdata: `./haruki-sekai-master/master` (inside the repo, gitignored)
- music metas: `../music_metas.json` (beside the repo)
- user data: `./collections.suite.json` (inside the repo, gitignored)

These fixtures are not package assets and should not be committed from this
repository.

## Packaging And Release

- PyPI package name: `haruki-sekai-deck-recommend-cpp`.
- npm package name: `haruki-sekai-deck-recommend-cpp`.
- The workflows reuse the shared templates in
  [`seiunx-dev/ci-templates`](https://github.com/seiunx-dev/ci-templates) at `@v1`
  where one fits (release gate, GitHub Release, Sonar, actionlint). cibuildwheel
  (scikit-build-core + pybind11) and the emsdk build have no template, so those jobs
  stay in the thin callers with a comment saying why. Reuse the templates first;
  customize only when they genuinely cannot meet a need, and fix template bugs
  upstream (new `v1.x.y` tag) instead of working around them here.
- `ci.yml` (`CI`, `master` pushes, PRs to `master`, manual dispatch): one cp313
  wheel per OS (linux x64 manylinux, Windows x64 with clang-cl + Ninja, macOS
  arm64), the WebAssembly build + `npm pack` (`scripts/ci/build-npm.sh`, emsdk
  pinned), the coverage build (`scripts/ci/coverage.sh`: instrumented native
  module, unittest, gcovr) feeding `Sonar`, and actionlint. The aggregate job
  **`CI OK`** is the only required status check.
- `release.yml` (`Release`): bump `version` in `pyproject.toml` and
  `npm/haruki-sekai-deck-recommend-cpp/package.json` in a PR → merge and wait for
  `CI OK` on `master` → push the tag `v<version>`. `release-gate` refuses a tag that
  differs from either file and waits for `CI OK` on the tagged commit. The run builds
  all wheels (cp310–cp315t on linux x64/arm64 manylinux and musllinux, Windows x64,
  macOS arm64; interpreters split into three parallel groups; musllinux legs may
  fail without blocking the release, as before), the sdist and the npm wasm package.
  Only after every build is done does it create the GitHub Release (all artifacts +
  `SHA256SUMS-<tag>.txt`) and publish to PyPI and npm. Manual dispatch is a dry run
  that builds everything and publishes nothing.
- PyPI and npm publishing use Trusted Publishing/OIDC. Keep the GitHub
  environments named `pypi-publish` and `npm-publish` unless the publishing
  setup is intentionally redesigned. Both trusted publishers must name the
  workflow file `release.yml`.
- The GitHub Release carries both PyPI artifacts and the npm tarball; PyPI
  publishing downloads only the `release-wheels-*` and `release-sdist` artifacts.

## Engineering Rules

- C++20. Headers and implementations live next to each other in `src/<area>/`.
- Masterdata, music metas, userdata parsing, and the wasm binding's JSON-in /
  JSON-out payloads go through yyjson via `src/common/collection-utils.h`.
- Prefer small, behavior-focused changes over broad refactors.
- Preserve existing enum mapping and validation behavior unless a caller-visible
  migration is intentional.
- Treat production userdata as object/dictionary shaped. Do not add runtime API
  behavior for Mongo-exported compact arrays unless the task is specifically
  about local fixtures or migration tooling.
- Be careful with `std::optional` fields in score details; missing bonus data
  should not silently become a different calculation.
- Do not change static data or generated assets unless the task explicitly
  needs it.
- For code used through deck-service, remember that C++ exceptions cross the
  boundary as error strings; write messages that help identify the bad input.
- Use concise comments only when the calculation or data shape is not obvious.

## Bindings

Two parallel binding files live next to the engine:

- `src/sekai_deck_recommend.cpp` — pybind11 binding for the Python wheel.
- `src/sekai_deck_recommend_wasm.cpp` — Embind binding for the WebAssembly
  build. JSON-in / JSON-out surface (`recommend(optionsJson)` returns a JSON
  string).

`deck-service` compiles neither of these: it builds only the engine sources and
ships its own C bridge (`cpp_bridge/deck_recommend_c.cpp` in that repository),
which parses the options itself.

Option validation logic is therefore duplicated in three places until a shared
core is extracted; when adding a field to `DeckRecommendOptions`, update both
binding files here, keep their schemas identical, and make the matching change
in the deck-service bridge.

## High-Risk Areas

These spots have produced production-visible regressions in the past — read
the surrounding code before changing them, and prefer adding tests or running
the deck-service integration build:

- `src/live-score/live-calculator.cpp`: skill ordering, multi-live active bonus,
  and fixed live score timing.
- `src/deck-recommend/base-deck-recommend.{h,cpp}`: card filtering and the
  shared deck recommendation config used by every recommender.
- `src/deck-recommend/deck-result-update.*`: result ordering and emitted detail
  fields consumed by deck-service.
- `src/data-provider/user-data.cpp` and `src/user-data/*`: downstream runtime
  userdata compatibility.
- `src/card-information/*`: power breakdowns and card state normalization.

## Performance & Regression Verification

Changes to search algorithms, deck evaluation, or other hot paths must
be validated with the local harness in `tools/bench/` (see its README):

- `regress.py run/compare` — fixed-seed regression. Deterministic
  scenarios must stay bit-identical; time-budgeted scenarios (DFS_GA /
  SA) are compared on top values only.
- `rl_quality.py` — RL hit-rate protocol (`DECK_RL_SEED_CACHE_DISABLE=1`;
  repeated runs must keep hitting the GA-reference optimum). RL stage
  budgets are a quality floor; never trim them for speed.
- `bench_matrix.py` — old-vs-new latency matrix; build the old version
  from a git worktree and run both builds in the same session.

Fixtures (masterdata, music metas, user suite) are local-only and must
never be committed.

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
- Before creating any commit, ask the user whether this commit should publish a
  new version.
- If the user wants a new version, bump the relevant package versions according
  to the user's requested release level. For this repository that normally means
  `pyproject.toml` and, when npm is affected, `npm/haruki-sekai-deck-recommend-cpp/package.json`.
- Version values for release bumps must use `major.minor.patch` format.
- If the user does not want a new version, create the commit without changing
  package versions.

Suggested values per agent:

- Claude: `Co-authored-by: Claude <Model> <noreply@anthropic.com>` with the
  actual model name (e.g. `Claude Fable 5`, `Claude Opus 4.7`,
  `Claude Haiku 4.5`)
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
