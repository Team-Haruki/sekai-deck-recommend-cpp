# haruki-sekai-deck-recommend-cpp

WebAssembly package for Haruki Sekai Deck Recommendation C++.

This package contains only the WebAssembly engine and a small JavaScript wrapper.
It does not include Project Sekai master data, music metas, or user data.
Applications should load those datasets from their own API, CDN, cache, or local
files.

## Build

From the repository root, with `emsdk` activated:

```bash
emcmake cmake -S . -B build_wasm -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build_wasm -j
cd npm/haruki-sekai-deck-recommend-cpp
npm pack
```

`prepack` copies `build_wasm/sekai_deck_recommend.js` and
`build_wasm/sekai_deck_recommend.wasm` into this package.

## Usage

```ts
import { createSekaiDeckRecommend } from "haruki-sekai-deck-recommend-cpp"
import wasmUrl from "haruki-sekai-deck-recommend-cpp/sekai_deck_recommend.wasm?url"

const engine = await createSekaiDeckRecommend({ wasmUrl })

engine.loadMasterData("jp", masterData)
engine.loadMusicMetas("jp", musicMetasText)

const result = engine.recommend({
  region: "jp",
  live_type: "multi",
  event_id: 205,
  world_bloom_character_id: 5,
  target: "score",
  algorithm: "ga",
  music_id: 1,
  music_diff: "easy",
  user_data: suiteData,
  limit: 10,
  timeout_ms: 15000,
})

engine.dispose()
```

Run recommendation calls in a Web Worker for production UI usage. Master data can
be tens of MiB after JSON encoding, and long-running algorithms should not block
the main thread.
