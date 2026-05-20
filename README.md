# Haruki Sekai Deck Recommendation C++

Team Haruki's maintained fork of the C++ Project Sekai deck recommendation
engine. The previous C++ optimization project lives at
[NeuraXmy/sekai-deck-recommend-cpp](https://github.com/NeuraXmy/sekai-deck-recommend-cpp).

This project provides Python bindings and a WebAssembly target for the Haruki
ecosystem, including the original brute-force search algorithm and additional
randomized / heuristic recommendation algorithms.

## Install Python package

Using uv:

```bash
uv add haruki-sekai-deck-recommend-cpp
```

To install into the current Python environment:

```bash
uv pip install haruki-sekai-deck-recommend-cpp
```

pip is still supported for users who are not using uv:

```bash
pip install haruki-sekai-deck-recommend-cpp
```

## Install from source

### Prerequisites

- CMake ≥ 3.15
- C++20 compatible compiler (GCC/Clang/MSVC)
- Python 3.10+ with development headers
- uv (recommended; pip workflows are also supported)

### Steps

```bash
# Clone with submodules
git clone --recursive https://github.com/Team-Haruki/sekai-deck-recommend-cpp.git
cd sekai-deck-recommend-cpp

# Create a local virtual environment and install the package in editable mode
uv venv
uv pip install -e . -v

# Smoke test the Python binding
uv run python -c "import sekai_deck_recommend_cpp"
```

If you are not using uv, `pip install -e . -v` remains supported.

## Usage

```python
from sekai_deck_recommend_cpp import (
    SekaiDeckRecommend, 
    DeckRecommendOptions,
    DeckRecommendCardConfig
)
   
sekai_deck_recommend = SekaiDeckRecommend()

sekai_deck_recommend.update_masterdata("base/dir/of/masterdata", "jp")
sekai_deck_recommend.update_musicmetas("file/path/of/musicmetas.json", "jp")

options = DeckRecommendOptions()

# optimizing target in ["score", "power", "skill", "bonus"], default is "score"
options.target = "score"

# "ga" for genetic algorithm, "dfs" for brute-force search
# default is "ga"
options.algorithm = "ga"   

options.region = "jp"
options.user_data_file_path = "user/data/file/path.json"
options.live_type = "multi"
options.music_id = 74
options.music_diff = "expert"
options.event_id = 160

result = sekai_deck_recommend.recommend(options)
```

For more details of options, please refer the docstring of `sekai_deck_recommend.DeckRecommendOptions`

## WebAssembly npm package

The WebAssembly npm package scaffold lives in
`npm/haruki-sekai-deck-recommend-cpp`. It publishes only the generated Embind
loader, the `.wasm` binary, JavaScript wrapper helpers, and TypeScript
declarations. Master data, music metas, and user data are intentionally loaded
by the application instead of being bundled into the npm package.

With `emsdk` activated:

```bash
emcmake cmake -S . -B build_wasm -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build_wasm -j
cd npm/haruki-sekai-deck-recommend-cpp
npm pack
```

## Acknowledgments
- Original implementation by [xfl03/sekai-calculator](https://github.com/xfl03/sekai-calculator)
- Previous C++ optimization project by [NeuraXmy/sekai-deck-recommend-cpp](https://github.com/NeuraXmy/sekai-deck-recommend-cpp)
- JSON parsing by [nlohmann/json](https://github.com/nlohmann/json)
- Python bindings powered by [pybind11](https://github.com/pybind/pybind11)
