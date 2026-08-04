"""Fixed-seed regression suite for engine changes.

    python tools/bench/regress.py run out.json [scenario-filter]
    python tools/bench/regress.py compare baseline.json after.json

Deterministic scenarios (pure GA, DFS runs that complete) must stay
bit-identical across a pure-performance change. Time-budgeted scenarios
(DFS_GA / SA — internal wall-clock budgets make node counts vary with
speed) are compared on top score only.
"""
import json
import sys
import time

import common


def scenarios():
    marathon, wl_event, wl_char = common.detect_events()
    finale_event = common.detect_finale_event()
    s = {
        'ga-marathon-multi-score': dict(live_type='multi', event_id=marathon, algorithm='ga', target='score'),
        'ga-marathon-multi-skill': dict(live_type='multi', event_id=marathon, algorithm='ga', target='skill'),
        'ga-marathon-solo-score':  dict(live_type='solo', event_id=marathon, algorithm='ga', target='score'),
        'ga-marathon-multi-bonus': dict(live_type='multi', event_id=marathon, algorithm='ga', target='bonus'),
        'ga-marathon-member4':     dict(live_type='multi', event_id=marathon, algorithm='ga', target='score', member=4),
        'ga-marathon-fixedchars':  dict(live_type='multi', event_id=marathon, algorithm='ga', target='score', fixed_characters=[1, 2]),
        'ga-noevent-solo-power':   dict(live_type='solo', algorithm='ga', target='power'),
        'ga-challenge':            dict(live_type='challenge', challenge_live_character_id=1, algorithm='ga', target='score'),
        'dfs-challenge':           dict(live_type='challenge', challenge_live_character_id=1, algorithm='dfs', target='score'),
        'dfs-marathon-member2':    dict(live_type='multi', event_id=marathon, algorithm='dfs', target='score', member=2),
        'dfs-marathon-multi':      dict(live_type='multi', event_id=marathon, algorithm='dfs', target='score'),
        'bonuslist-marathon-easy': dict(live_type='multi', event_id=marathon, algorithm='dfs', target='bonus', target_bonus_list=[355]),
        'bonuslist-marathon-hard': dict(live_type='multi', event_id=marathon, algorithm='dfs', target='bonus', target_bonus_list=[185]),
        'bonuslist-marathon-multi': dict(live_type='multi', event_id=marathon, algorithm='dfs', target='bonus', target_bonus_list=[185, 355]),
        'bonuslist-marathon-miss': dict(live_type='multi', event_id=marathon, algorithm='dfs', target='bonus', target_bonus_list=[999]),
        'dfsga-marathon-multi':    dict(live_type='multi', event_id=marathon, algorithm='dfs_ga', target='score'),
        'dfsga-challenge':         dict(live_type='challenge', challenge_live_character_id=1, algorithm='dfs_ga', target='score'),
        'sa-marathon-multi':       dict(live_type='multi', event_id=marathon, algorithm='sa', target='score'),
    }
    if wl_event is not None:
        s['ga-wl-multi-score'] = dict(live_type='multi', event_id=wl_event, algorithm='ga', target='score', world_bloom_character_id=wl_char)
        s['bonuslist-wl-easy'] = dict(live_type='multi', event_id=wl_event, algorithm='dfs', target='bonus', target_bonus_list=[345], world_bloom_character_id=wl_char)
        s['bonuslist-wl-hard'] = dict(live_type='multi', event_id=wl_event, algorithm='dfs', target='bonus', target_bonus_list=[225], world_bloom_character_id=wl_char)
        s['bonuslist-wl-multi'] = dict(live_type='multi', event_id=wl_event, algorithm='dfs', target='bonus', target_bonus_list=[300, 345], world_bloom_character_id=wl_char)
        s['bonuslist-wl-miss'] = dict(live_type='multi', event_id=wl_event, algorithm='dfs', target='bonus', target_bonus_list=[999], world_bloom_character_id=wl_char)
    if finale_event is not None:
        s['ga-finale-support'] = dict(
            live_type='multi',
            event_id=finale_event,
            algorithm='ga',
            target='power',
            forcedLeaderCharacterId=1,
            limit=1,
            member=2,
        )
    return s


# 时间预算驱动的场景：代码变快后结点数合法变化，只比较top值
TIME_BUDGETED = {'dfsga-marathon-multi', 'dfsga-challenge', 'sa-marathon-multi', 'ga-challenge'}


def deck_repr(d):
    return {
        'score': d.score,
        'live_score': d.live_score,
        'total_power': d.total_power,
        'event_bonus_rate': round(d.event_bonus_rate, 6),
        'support_deck_bonus_rate': round(d.support_deck_bonus_rate, 6),
        'multi_live_score_up': round(d.multi_live_score_up, 6),
        'cards': [c.card_id for c in d.cards],
        'card_skill_score_up': [c.skill_score_up for c in d.cards],
        'card_total_power': [c.total_power for c in d.cards],
        'support_deck_cards': [
            {
                'card_id': c.card_id,
                'bonus': c.bonus,
                'skill_level': c.skill_level,
                'master_rank': c.master_rank,
                'level': c.level,
                'after_training': c.after_training,
                'default_image': c.default_image,
            }
            for c in d.support_deck_cards
        ],
    }


def run(out_path, filt=None):
    m, sdr, user_data = common.make_engine()
    results, timings = {}, {}
    for name, kw in scenarios().items():
        if filt and filt not in name:
            continue
        try:
            o = common.fixed_seeds(m, common.base_options(m, user_data, **kw))
            o.timeout_ms = 1000000
            t0 = time.perf_counter()
            r = sdr.recommend(o)
            timings[name] = round((time.perf_counter() - t0) * 1000, 1)
            results[name] = [deck_repr(d) for d in r.decks]
        except Exception as e:  # noqa: BLE001 - record engine errors as data
            results[name] = {'error': str(e)}
            timings[name] = -1
        top = results[name][0]['score'] if isinstance(results[name], list) and results[name] else 'ERR'
        print(f'{name:28s} {timings[name]:9.1f}ms top={top}', flush=True)
    json.dump({'results': results, 'timings': timings}, open(out_path, 'w'), indent=1, sort_keys=True)


def compare(base_path, after_path):
    a = json.load(open(base_path))['results']
    b = json.load(open(after_path))['results']
    fail = 0
    for name in sorted(set(a) | set(b)):
        ra, rb = a.get(name), b.get(name)
        if name in TIME_BUDGETED:
            sa_ = ra[0]['score'] if isinstance(ra, list) and ra else None
            sb_ = rb[0]['score'] if isinstance(rb, list) and rb else None
            ok = sa_ is not None and sb_ is not None and sb_ >= sa_ * 0.98
            print(f'{name:28s} {"SANITY-OK" if ok else "SANITY-FAIL"} before={sa_} after={sb_}')
            fail += 0 if ok else 1
        elif ra == rb:
            print(f'{name:28s} EXACT-MATCH')
        else:
            fail += 1
            print(f'{name:28s} MISMATCH')
            if isinstance(ra, list) and isinstance(rb, list):
                for i, (da, db) in enumerate(zip(ra, rb)):
                    if da != db:
                        diffs = {k: (da[k], db.get(k)) for k in da if da[k] != db.get(k)}
                        print(f'   deck[{i}]: {diffs}')
                        break
    print('FAIL' if fail else 'ALL-OK')
    return 1 if fail else 0


if __name__ == '__main__':
    if len(sys.argv) >= 3 and sys.argv[1] == 'run':
        run(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
    elif len(sys.argv) == 4 and sys.argv[1] == 'compare':
        sys.exit(compare(sys.argv[2], sys.argv[3]))
    else:
        print(__doc__)
        sys.exit(2)
