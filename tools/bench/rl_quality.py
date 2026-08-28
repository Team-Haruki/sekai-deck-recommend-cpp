"""RL hit-rate quality protocol (methodology from commit a3e5bcb).

    DECK_RL_SEED_CACHE_DISABLE=1 python tools/bench/rl_quality.py [--processes N] [--calls M]

For each scenario, prints the top target value of every call. Quality
gate: with the seed cache disabled, every RL call should keep hitting
the reference optimum (run with --ref once to print GA-reference
values). Use --processes to sample fresh cold-start processes; RL keeps
in-process memory, so within-process calls warm up (that is also the
production shape). Compare VALUES, not card lists: budget cutoffs make
tied-optimum compositions nondeterministic.

RL stage budgets are a quality floor; never trim them for speed.
"""
import argparse
import os
import subprocess
import sys
import time
from pathlib import Path

import common


def scenarios():
    marathon, wl_event, wl_char = common.detect_events()
    s = {
        'score-marathon': dict(live_type='multi', event_id=marathon, target='score'),
        'skill-marathon': dict(live_type='multi', event_id=marathon, target='skill'),
        'bonus-marathon': dict(live_type='multi', event_id=marathon, target='bonus'),
        'score-challenge': dict(live_type='challenge', challenge_live_character_id=1, target='score'),
    }
    if wl_event is not None:
        s['score-wl'] = dict(live_type='multi', event_id=wl_event, target='score', world_bloom_character_id=wl_char)
        s['bonus-wl'] = dict(live_type='multi', event_id=wl_event, target='bonus', world_bloom_character_id=wl_char)
    return s


def top_value(kw, deck):
    if kw['target'] == 'bonus':
        return round(deck.event_bonus_rate + deck.support_deck_bonus_rate, 4)
    if kw['target'] == 'skill':
        return round(deck.multi_live_score_up, 4)
    return deck.score


def run_calls(calls, use_ref):
    m, sdr, user_data = common.make_engine()
    for name, kw in scenarios().items():
        for i in range(calls):
            o = common.base_options(m, user_data, **kw)
            o.algorithm = 'ga' if use_ref else 'rl'
            if use_ref:
                o.timeout_ms = 8000
            t0 = time.perf_counter()
            r = sdr.recommend(o)
            ms = (time.perf_counter() - t0) * 1000
            print(f'{name} call={i} val={top_value(kw, r.decks[0])} ms={ms:.0f}', flush=True)


def bounded_count(value):
    parsed = int(value)
    if parsed < 1 or parsed > 100:
        raise argparse.ArgumentTypeError('must be between 1 and 100')
    return parsed


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ref', action='store_true')
    parser.add_argument('--calls', type=bounded_count, default=5)
    parser.add_argument('--processes', type=bounded_count, default=1)
    args = parser.parse_args(argv)
    use_ref = args.ref
    calls = args.calls
    processes = args.processes
    if os.environ.get('DECK_RL_SEED_CACHE_DISABLE') != '1' and not use_ref:
        print('[warn] DECK_RL_SEED_CACHE_DISABLE=1 not set; persistent seed cache may leak between runs', file=sys.stderr)
    if processes > 1:
        script = Path(__file__).resolve(strict=True)
        child_args = ['--calls', str(calls)]
        if use_ref:
            child_args.append('--ref')
        for _ in range(processes):
            subprocess.run([sys.executable, str(script), *child_args], check=True)
    else:
        run_calls(calls, use_ref)
    return 0


if __name__ == '__main__':
    sys.exit(main())
