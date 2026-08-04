"""Shared helpers for the local benchmark / regression harness.

Fixtures are LOCAL ONLY and never committed. Configure via env vars:

    SEKAI_BENCH_SO_DIR      dir containing a bare sekai_deck_recommend*.so
                            (default: use the installed sekai_deck_recommend_cpp package)
    SEKAI_BENCH_MASTERDATA  masterdata dir      (default: <repo>/haruki-sekai-master/master)
    SEKAI_BENCH_MUSICMETAS  music metas json    (default: <repo>/../music_metas.json)
    SEKAI_BENCH_USERDATA    suite json          (default: <repo>/collections.suite.json)
    SEKAI_BENCH_MUSIC_ID    music id            (default: 74)
    SEKAI_BENCH_MUSIC_DIFF  music difficulty    (default: expert)

Run from the repo root: a bare .so resolves static data at ./data.
Mongo-exported suites (a JSON list wrapping one suite object) are
unwrapped automatically.
"""
import contextlib
import json
import os
import sys
import tempfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

MUSIC_ID = int(os.environ.get('SEKAI_BENCH_MUSIC_ID', '74'))
MUSIC_DIFF = os.environ.get('SEKAI_BENCH_MUSIC_DIFF', 'expert')


def masterdata_dir():
    return os.environ.get('SEKAI_BENCH_MASTERDATA', os.path.join(REPO_ROOT, 'haruki-sekai-master', 'master'))


def load_engine():
    so_dir = os.environ.get('SEKAI_BENCH_SO_DIR')
    if so_dir:
        sys.path.insert(0, so_dir)
        import sekai_deck_recommend as m
    else:
        from sekai_deck_recommend_cpp import sekai_deck_recommend as m
    return m


@contextlib.contextmanager
def userdata_path():
    path = os.environ.get('SEKAI_BENCH_USERDATA', os.path.join(REPO_ROOT, 'collections.suite.json'))
    with open(path, 'rb') as f:
        first = f.read(1)
    if first != b'[':
        yield path
        return

    with open(path) as f:
        data = json.load(f)
    inner = data[0] if isinstance(data, list) and data else data
    # A process-private directory prevents concurrent benchmark processes from
    # truncating each other's fixture and keeps the unwrapped user data private.
    # Publish only a fully written file, then remove it immediately after load.
    with tempfile.TemporaryDirectory(prefix='sekai_bench_userdata_') as temp_dir:
        out = os.path.join(temp_dir, 'suite-inner.json')
        fd, pending = tempfile.mkstemp(prefix='.suite-inner-', suffix='.tmp', dir=temp_dir)
        try:
            with os.fdopen(fd, 'w') as f:
                json.dump(inner, f)
            os.replace(pending, out)
            yield out
        finally:
            if os.path.exists(pending):
                os.unlink(pending)


def make_engine(region='jp'):
    m = load_engine()
    sdr = m.SekaiDeckRecommend()
    sdr.update_masterdata(masterdata_dir(), region)
    metas = os.environ.get('SEKAI_BENCH_MUSICMETAS', os.path.join(os.path.dirname(REPO_ROOT), 'music_metas.json'))
    sdr.update_musicmetas(metas, region)
    user_data = m.DeckRecommendUserData()
    with userdata_path() as path:
        user_data.load_from_file(path)
    return m, sdr, user_data


def detect_events():
    """Pick the newest real marathon and world-bloom events from masterdata."""
    md = masterdata_dir()
    events = json.load(open(os.path.join(md, 'events.json')))
    marathon = max(e['id'] for e in events if e.get('eventType') == 'marathon' and e['id'] < 1000)
    world_blooms = json.load(open(os.path.join(md, 'worldBlooms.json')))
    wl_chapters = [w for w in world_blooms if w['eventId'] < 1000 and w.get('worldBloomChapterType') == 'game_character']
    wl_event = max((w['eventId'] for w in wl_chapters), default=None)
    wl_char = next((w['gameCharacterId'] for w in wl_chapters if w['eventId'] == wl_event), None)
    return marathon, wl_event, wl_char


def detect_finale_event():
    """Pick the newest real world-bloom finale event, if available."""
    path = os.path.join(masterdata_dir(), 'worldBlooms.json')
    with open(path) as f:
        world_blooms = json.load(f)
    finale_events = [
        w['eventId']
        for w in world_blooms
        if w['eventId'] < 1000 and w.get('worldBloomChapterType') == 'finale'
    ]
    return max(finale_events, default=None)


def base_options(m, user_data, **kw):
    o = m.DeckRecommendOptions()
    o.region = 'jp'
    o.user_data = user_data
    o.music_id = MUSIC_ID
    o.music_diff = MUSIC_DIFF
    o.limit = 10
    for k, v in kw.items():
        setattr(o, k, v)
    return o


def fixed_seeds(m, o, seed=42):
    ga = m.DeckRecommendGaOptions()
    ga.seed = seed
    o.ga_options = ga
    sa = m.DeckRecommendSaOptions()
    sa.seed = seed
    o.sa_options = sa
    return o
