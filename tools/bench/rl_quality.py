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
import os
import subprocess
import sys
import time

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


if __name__ == '__main__':
    args = sys.argv[1:]
    use_ref = '--ref' in args
    calls = int(args[args.index('--calls') + 1]) if '--calls' in args else 5
    processes = int(args[args.index('--processes') + 1]) if '--processes' in args else 0
    if os.environ.get('DECK_RL_SEED_CACHE_DISABLE') != '1' and not use_ref:
        print('[warn] DECK_RL_SEED_CACHE_DISABLE=1 not set; persistent seed cache may leak between runs', file=sys.stderr)
    if processes > 1:
        child_args = [a for a in args if a not in ('--processes', str(processes))]
        for _ in range(processes):
            subprocess.run([sys.executable, os.path.abspath(__file__), *child_args], check=True)
    else:
        run_calls(calls, use_ref)
