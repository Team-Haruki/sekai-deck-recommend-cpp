# Performance Optimizations

This document records performance optimizations applied to the deck
recommendation hot path. Every change in here is a **behavior-preserving**
refactor: it removes redundant work (copies, allocations, lookups) without
changing the recommendation results.

## Verification method

Each batch is validated with a before/after parity harness:

1. Build the current `master` working tree (baseline).
2. Run `recommend()` with **fixed RNG seeds** across multiple algorithms and
   live types, recording each returned deck's `(score, total_power, card_ids)`.
3. `git stash` the changes, rebuild, run the same harness → baseline output.
4. `git stash pop`, rebuild, run again → modified output.
5. `diff` the two. They must be **byte-identical**.

Coverage used for validation (fixed seed 12345):

| Case | Algorithm | Live | Notes / path exercised |
|------|-----------|------|------------------------|
| `dfs_m2`   | `dfs`    | solo  | `member=2`, exercises the DFS unit-overlap filter (`unitMask`) |
| `dfsga_m2` | `dfs_ga` | solo  | `member=2` |
| `ga_best`  | `ga`     | solo  | `skill_order=max`, exercises `getSkillScore` sort path |
| `sa_multi` | `sa`     | multi | multi skill ordering + active bonus |
| `ga_score` | `ga`     | solo  | default score path |

Result: **✅ identical** before/after for all batches below.

> Local fixtures used (not committed): masterdata `haruki-sekai-master/master`,
> music metas `../music_metas.json`, a single object-shaped user record derived
> from the Mongo-exported `suite.json`. DFS over a large diverse pool is
> exhaustive and slow; the parity harness uses `member=2` and disables low
> rarities to keep the DFS cases tractable while still exercising the changed
> code. The same config is applied to both builds, so parity remains valid.

---

## Batch 1 — copy elimination in per-card precompute and GA/SA grouping

| File | Change | Why it was slow |
|------|--------|-----------------|
| `card-information/card-skill-calculator.cpp` | `getSkill` / `getCharacterRank`: `auto` → `auto&` | `auto skills = dataProvider.masterData->skills;` deep-copied the **entire** skills table (each `Skill` holds nested effect vectors) on every skill-detail computation. Same for `userCharacters`. |
| `card-information/card-calculator.cpp` | `getCardDetail`: `Card card{}` value copy → `const Card&` | `findOrThrow` already returns `T&`; assigning into a local `Card` deep-copied its `std::vector<CardParameter>` per card. |
| `data-provider/data-provider.cpp` | `DataProvider::init()` `std::map<std::string,std::set<int>>` literal → `static const` | The unit→characters map (6 string-keyed `std::set` nodes) was rebuilt on every request. |
| `deck-recommend/find-best-cards-ga.cpp`, `find-best-cards-sa.cpp` | Per-character grouping `std::vector<CardDetail>` → `std::vector<const CardDetail*>` | `CardDetail` is ~1–4 KB (two 144-slot `CardDetailMap`s). GA/SA grouped every card by value, then only ever used addresses. Now stores pointers (input is `const&`, addresses are stable). GA gained a pointer overload of `calcRandomSelectWeights`. |

`CardDetail` size note: each `CardDetailMap<T>` is a `std::array<std::optional<T>, 12*6*2>` =
144 inline slots; `CardDetail` has two of them (`power`, `skill`). Any by-value
copy of a `CardDetail` or `std::vector<CardDetail>` is expensive — this is why
the GA/SA grouping change is the single biggest win in this batch.

## Batch 2 — search inner-loop copies and the DFS hot line

| File | Change | Why it was slow |
|------|--------|-----------------|
| `deck-recommend/base-deck-recommend.cpp` `getBestPermutation` | (a) The per-leaf `orderedDeckCards = deckCards` vector copy is now **materialized only when** a fixed-card order or leader rotation is actually required; the common path passes `deckCards` through by const reference. (b) The two per-leaf `std::unordered_set<int>` (card/character dedup) are replaced by `std::array<int,5>` linear scans. | `getBestPermutation` runs once per complete candidate deck (millions of times). It allocated one vector + two hash tables every call, all for ≤5 elements. `member` is validated to `[2,5]`, so the fixed-size arrays are safe. |
| `live-score/live-calculator.{h,cpp}` `getSkillScore` | Return type `std::vector<double>` → `const std::vector<double>&`; the caller copies only when the skill order actually needs sorting (`max`/`min`). | The skill-score vector lives in `MusicMeta` and is loop-invariant across the whole search, but was copied out of `MusicMeta` on every deck evaluation. |
| `card-information/card-calculator.{h,cpp}` + `deck-recommend/find-best-cards-dfs.cpp` | Added `unsigned int unitMask` to `CardDetail` (computed once at construction). The DFS C-position filter `!containsAny(a->units, b.units)` became a single bitwise AND `!(a->unitMask & b.unitMask)`. | This is the highest-frequency line in the engine (checked for nearly every card at every DFS node). `containsAny` was a nested scan over two heap `std::vector<int>`. Units are a tiny closed enum (values well under 32), so a bitmask is an exact equivalent. |
| `deck-recommend/base-deck-recommend.cpp:1617`, `find-best-cards-ga.cpp:227` | `map.count(k)` + `map[k]` → single `map.find(k)` | The deck-score cache hit path hashed and probed the map twice. |

`containsAny` is now unused at call sites but kept (it is a function template, so it
is simply not instantiated — no warning).

## Batch 3 — DFS upper-bound pruning and SA membership containers

| File | Change | Why it was slow |
|------|--------|-----------------|
| `deck-recommend/find-best-cards-dfs.cpp` | `calcPowerUpperBound`, `calcScoreUpperBound`, `calcSkillTargetUpperBound`: the full `std::sort(..., greater)` of the whole remaining-card pool is replaced by `std::nth_element` (power) / a `nth_element`-based top-`k` fill (skills). | These run at every internal DFS node and only use the top `needed`≤5 values; sorting the entire pool (hundreds of cards) was `O(n log n)` where `O(n)` suffices. Safe because only the **values** are consumed (a sum, or a list that is re-sorted afterward), so boundary ties don't matter. |
| `deck-recommend/find-best-cards-sa.cpp` | `deckCharacters`: `std::set<int>` → `std::bitset<32>`; `deckCardIds`: `std::set<int>` → `std::unordered_set<int>`. | The per-iteration replaceable-index rebuild does a membership test per character (and per card in challenge) for up to ~1M iterations. `deckCharacters` is only read on the non-challenge path where each character has at most one card, so a bitset is exact. |

### Reverted: GA population `partial_sort`

Replacing the per-generation `std::sort(population, greater)` with
`std::partial_sort` of just the top `max(parentSize, eliteSize)` **changed GA
results** and was reverted. Unlike the DFS bounds (which consume only values),
the GA selects **individuals**: two different decks can share the same fitness,
and `partial_sort` orders such ties differently from `sort`. That changes which
parents are picked, which changes the RNG stream, which changes the output. The
parity harness caught this immediately. Lesson: only relax a full sort to a
partial/nth ordering when downstream consumes **values**, not **identity/order
of equal-key elements**.

## Batch 4 — masterdata id indexes (O(n) `findOrThrow` → O(1))

`MasterData` stores ~40 flat `std::vector`s and every lookup was a linear
`findOrThrow` (`std::find_if`). Several of these run **per owned card** during
the precompute (`batchGetCardDetail`), so for a collection of C cards each
lookup is `O(C)` and the precompute is effectively `O(C²)`.

Fix: build `std::unordered_map<int,int>` (id → vector index) **once** at the end
of `MasterData::loadFromJsons`, after `addFakeEvent`/legacy mutations (which do
not touch the indexed vectors), and expose `findById`-style accessors that
return `const T*` (or `nullptr`). Index-by-position (not raw pointers) so the
maps survive a `MasterData` copy. Masterdata is cached as a `shared_ptr` across
requests, so this index is built once and amortized over all requests.

| Index | Key | Call sites converted |
|-------|-----|----------------------|
| `cardIdToIndex` → `findCardById` | `Card.id` | `card-calculator.cpp` `getCardDetail` + `getSupportDeckCard`; `card-event-calculator.cpp` `getCardEventBonus`; `card-bloom-event-calculator.cpp` `getCardSupportDeckBonus` |
| `skillIdToIndex` → `findSkillById` | `Skill.id` | `card-skill-calculator.cpp` `getSkill` |
| `cardEpisodeIdToIndex` → `findCardEpisodeById` | `CardEpisode.id` | `card-power-calculator.cpp` `getBasePower` |
| `characterRankToIndex` → `findCharacterRank` | `characterId*1000 + rank` | `card-power-calculator.cpp` `getCharacterBonusPower` |

Two of the `findCardById` conversions also removed a by-value `Card` copy (the
old code did `auto card = findOrThrow(...)`, copying the `Card` and its
`std::vector<CardParameter>`). Error messages and the `ElementNoFoundError`
exception type are preserved at each call site.

Smaller vectors (`gameCharacters`, `gameCharacterUnits` ≈ 26–50 rows) were left
as linear scans — indexing them is not worth the surface area.

### `isWorldBloomFinale` memoization

`MasterData::isWorldBloomFinale(eventId)` scanned `worldBlooms` with a string
compare (`worldBloomChapterType == "finale"`) on **every** call, and
`getBestPermutation` calls it per candidate deck (millions of times). It is now a
single `unordered_set<int>` lookup against a `worldBloomFinaleEventIds` set
precomputed at load time. This fixes all nine `getBestPermutation` call sites at
once with no signature change.

## Batch 5 — per-deck-evaluation allocations in `getDeckDetailByCards`

`getDeckDetailByCards` is the single hottest callee — the SA/GA search invokes it
once per candidate deck (hundreds of thousands of times per request). Converted
its remaining small per-call heap vectors to stack storage:

| File | Change |
|------|--------|
| `deck-calculator.h` / `deck-calculator.cpp` | `DeckBonusInfo::cardBonus`: `std::vector<double>` → `std::array<double,5>` (deck ≤ 5; unused entries stay 0, so the `accumulate` is unchanged). |
| `deck-calculator.cpp` `getDeckDetailByCards` | `scoreUps`: `std::vector<std::pair<double,double>>` → `std::array<…,32>` + count (bounded by `2^needEnumerateCount ≤ 32`). `memberSkillMaxs`: `std::vector<double>` → `std::array<double,5>` + count. |

## Performance measurement findings (important)

Batches 1–5 are all **verified behavior-identical** on real JP production data
(`collections.suite.json`) against the latest synced masterdata. However, on
representative SA/GA/DFS workloads the **wall-clock change is within ±2% noise —
there is no measurable speedup (and no regression).**

Why: a coarse profile (running the same SA workload with `target=power` vs
`target=score`, which share the same 300k deck evaluations but skip vs run the
live-score step) shows:

```
sa target=power: 297 ms      # getBestPermutation + getDeckDetailByCards only
sa target=score: 338 ms      # + live-score  (~41 ms, ~12%)
sa target=skill: 385 ms
```

So ~88% of the time is `getDeckDetailByCards` (deck building, skill-state
enumeration, power/skill `CardDetailMap::get` probing), **not** the live-score
and **not** the allocations these batches removed. The path is compute-bound, and
the small heap blocks (≤5 elements) hit the allocator fast path, so removing them
does not move wall-clock. The changes remain worthwhile — fewer allocations is
better hygiene, especially for the multi-threaded `deck-service` consumer — but
they are not a wall-clock win on their own.

**To get a real speedup**, the lever is algorithmic: the search builds a full
`DeckDetail` (including the `std::vector<DeckCardDetail> cards` and a full skill
breakdown) for *every* candidate, but `getBestPermutation` only needs the scalar
`targetValue` for all candidates except the eventual winner. A "score-only" fast
path that computes the target value without materializing the per-card detail —
deferring full `DeckDetail` construction to the winning deck — would cut the
dominant cost. This is a larger, higher-risk refactor across
`getDeckDetailByCards` / `getBestPermutation` (both flagged high-risk in
CLAUDE.md). It was investigated and **empirically rejected** — see batch 6.

---

## Batch 6 — profiling-driven (the one real win) and the negative results

A symbol build (`SKBUILD_CMAKE_BUILD_TYPE=RelWithDebInfo`) profiled with macOS
`sample` against real JP data, per algorithm. Findings:

### SA: unthrottled per-iteration `clock::now()` — the real win (shipped)

`std::chrono::now` was **~2108 / ~10368 samples (~20% of `findBestCardsSA`
self-time)**: the per-run `saMaxTimeMs` check called `high_resolution_clock::now()`
on every iteration, while the adjacent `RecommendCalcInfo::isTimeout()` already
throttles to every 256 calls. Throttling the per-run check to every 256
iterations drops `clock::now` to **8 samples** (re-profiled). Byte-identical for
iteration/no-improve-bound runs (verified on the 31-case matrix *and* default
time-bound SA, which converges before the limit binds). GA/DFS use the throttled
`isTimeout()` and were never affected.

### The score-only fast path (batch 5's "recommended next step"): rejected

A spike that skips the per-card display build under a `scoreOnly` flag measured
**neutral** (within noise). The per-card display materialization is not the
bottleneck; the deck build's arithmetic (`CardDetailMap::get` probing,
`getDeckBonus`, the skill-state enumeration) is, and that is shared by the
fast and full paths. `CardDetailMap::get` returning by value vs by-ref
(`getPtr`) was also tried — neutral in a clean back-to-back A/B (an earlier
"+6%" was load drift). Do not pursue these for a wall-clock win.

### RL / GA / DFS_GA: eval-bound, no clean byte-identical single-request win

Per-recommend wall times (real JP data, default options): RL 76 ms, GA 113 ms,
DFS_GA 248 ms, SA 793 ms. Profiles:
- **GA**: per-deck eval + live/event score + population `std::sort<Individual>`
  (~1300 samples, parity-stuck — `partial_sort` reorders equal-fitness ties and
  changes results) + `Individual::calcDeckHash` `std::sort` of ≤5 ids (~347).
- **RL**: ~44% per-request *setup* (string-key construction, card precompute,
  pool fingerprint) — inherent, no redundancy found (checked double-precompute,
  fingerprint, the int hash-table) — plus the same eval. `savePersistentSeedBuckets`
  runs every request but only matters for cross-process persistence under
  concurrency (a global-mutex + disk write); throttling it is parity-safe
  in-process but is a scale/throughput fix, not a single-request speedup, so it
  was left out.
- The shared per-deck evaluation is the floor for all four and is irreducible
  for byte-identical output (three independent spikes confirmed neutral).

**Conclusion:** byte-identical single-request optimization is exhausted. Further
gains require an explicit trade-off: GA `partial_sort` (~6%, non-byte-identical /
makes GA deterministic), the RL save throttle (concurrency/scale), parallelizing
the GA population / SA runs (threading + shared-cache work), or cross-request
precompute caching.

---

## Remaining opportunities (not yet done)

Ordered roughly by value/effort. These touch the search hot path and should be
done in small, individually-verified steps.

- **DFS upper-bound: fuse scans + reuse scratch deck** (`find-best-cards-dfs.cpp`):
  the full-pool sort is already `nth_element` (batch 3), but `calcScoreUpperBound`
  and `calcSkillTargetUpperBound` still scan the pool for power and skill
  separately (and `calcSkillTargetUpperBound` calls `calcPowerUpperBound`, a
  third pass). Fuse into one pass, and reuse a scratch `DeckDetail` instead of
  allocating one per node.
- **SA replaceable-index incremental maintenance** (`find-best-cards-sa.cpp:~219`):
  containers are now `bitset`/`unordered_set` (batch 3), but the replaceable set
  is still fully rebuilt (`O(MAX_CID × pool)`) every iteration even though only
  one position changes. Maintaining it incrementally would cut the per-iteration
  cost further. Higher risk.
- **`sortCardsByStrength` and the `build*Cards` helpers**
  (`base-deck-recommend.cpp`): stop deep-copying whole `std::vector<CardDetail>`
  to re-sort by a different key; sort `std::vector<const CardDetail*>` or
  indices instead.
- **Loop-invariant hoisting in the deck search** (`base-deck-recommend.cpp`):
  `isWorldBloomFinale` is now memoized (batch 4), but `resolveRequiredCharacters`
  / `resolveRemainingFixedCharacters` still rebuild a vector + set per call / per
  DFS node. They are constant for the whole run and can be computed once and
  passed in.
- **`supportCards` container** (`std::map<int, vector<SupportDeckCard>>` threaded
  through `getBestPermutation` / `getDeckDetailByCards`): in finale scoring the
  keys are characterId 1..26, so a `std::array<vector<...>,27>` removes the
  red-black-tree lookups on the hot path.
- **More masterdata indexes**: `eventDeckBonuses`, `eventCards`,
  `gameCharacterUnits` (and the `[it]` by-value capture in
  `card-event-calculator.cpp:25` → `[&it]`) if profiling shows the per-card event
  bonus path still matters. Tiny vectors were intentionally skipped (batch 4).
- **`getDeckDetailByCards` small heap vectors** (`deck-calculator.cpp`):
  `scoreUps` / `memberSkillMaxs` could become stack `std::array`s. Marginal gain,
  higher risk in this high-risk file — deferred.
- **Scattered**: reuse one `std::uniform_real_distribution` in GA instead of
  constructing per draw; throttle the SA per-iteration `clock::now()`;
  replace the bonus-DFS `std::map`/`std::set` hot path with sorted flat vectors.
  (GA population `partial_sort` was tried and reverted — see batch 3.)
