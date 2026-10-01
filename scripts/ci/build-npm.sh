#!/usr/bin/env bash
# WebAssembly build + npm pack, ported from build-npm.yml. Installs emsdk into ~/emsdk
# unless a restored cache already has the requested version active.
# Usage: scripts/ci/build-npm.sh <emsdk-version>
set -euo pipefail
cd "$(dirname "$0")/../.."
version="${1:?emsdk version, e.g. 5.0.7}"
command -v ninja >/dev/null 2>&1 || { sudo apt-get update && sudo apt-get install -y ninja-build; }
[ -d "$HOME/emsdk/.git" ] || git clone https://github.com/emscripten-core/emsdk.git "$HOME/emsdk"
"$HOME/emsdk/emsdk" install "$version"
"$HOME/emsdk/emsdk" activate "$version"
# shellcheck disable=SC1091
source "$HOME/emsdk/emsdk_env.sh"
emcmake cmake -S . -B build_wasm -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build_wasm -j
(cd npm/haruki-sekai-deck-recommend-cpp && npm pack)
