"""Latency matrix: live types x algorithms, one process per cell.

    python tools/bench/bench_matrix.py <live_type> <algorithm> [reps]
    python tools/bench/bench_matrix.py all            # run the whole matrix

Old-vs-new comparison: build the pre-change version from a git
worktree, then run each cell against both via SEKAI_BENCH_SO_DIR.
Run cells of both builds in the same session to cancel thermal drift.
"""
import statistics
import subprocess
import sys
import time

import common

LIVE_TYPES = ['multi', 'solo', 'auto', 'challenge', 'challenge_auto', 'mysekai']
ALGORITHMS = ['ga', 'dfs_ga', 'rl', 'sa', 'dfs']
DEFAULT_REPS = {'sa': 6, 'dfs': 4}


def run_cell(live_type, algorithm, reps):
    m, sdr, user_data = common.make_engine()
    marathon, _, _ = common.detect_events()
    kw = {'live_type': live_type, 'algorithm': algorithm}
    if live_type in ('challenge', 'challenge_auto'):
        kw['challenge_live_character_id'] = 1
    else:
        kw['event_id'] = marathon
    if live_type != 'mysekai':
        kw['target'] = 'score'
    times, top = [], None
    for _ in range(reps):
        o = common.fixed_seeds(m, common.base_options(m, user_data, **kw))
        if algorithm == 'dfs':
            o.timeout_ms = 2000  # 上界松的场景（WL/mysekai）纯DFS跑不完，固定预算内对比
        t0 = time.perf_counter()
        r = sdr.recommend(o)
        times.append((time.perf_counter() - t0) * 1000)
        top = (r.decks[0].mysekai_event_point if live_type == 'mysekai' else r.decks[0].score) if r.decks else None
    print(f'CELL {live_type:15s} {algorithm:7s} best={min(times):8.1f} avg={statistics.mean(times):8.1f} '
          f'p50={statistics.median(times):8.1f} n={reps} top={top}', flush=True)


if __name__ == '__main__':
    if len(sys.argv) >= 2 and sys.argv[1] == 'all':
        for lt in LIVE_TYPES:
            for alg in ALGORITHMS:
                reps = DEFAULT_REPS.get(alg, 12)
                subprocess.run([sys.executable, __file__, lt, alg, str(reps)], check=True)
    elif len(sys.argv) >= 3:
        run_cell(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 12)
    else:
        print(__doc__)
        sys.exit(2)
