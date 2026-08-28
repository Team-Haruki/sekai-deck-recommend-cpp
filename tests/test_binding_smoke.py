import importlib
import json
import os
import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
binding_dir = os.environ.get("SEKAI_BINDING_DIR")
if binding_dir:
    sys.path.insert(0, binding_dir)

binding = importlib.import_module("sekai_deck_recommend")
binding.init_data_path(str(REPO_ROOT / "data"))

REQUIRED_MASTER_DATA_KEYS = """
areaItemLevels areaItems areas cardEpisodes cards cardRarities characterRanks
eventCards eventDeckBonuses eventExchangeSummaries events eventItems
eventRarityBonusRates gameCharacters gameCharacterUnits honors masterLessons
musicDifficulties musics musicVocals shopItems skills
worldBloomDifferentAttributeBonuses worldBlooms worldBloomSupportDeckBonuses
""".split()

OPTIONAL_MASTER_DATA_KEYS = """
worldBloomSupportDeckUnitEventLimitedBonuses cardMysekaiCanvasBonuses
eventCardBonusLimits eventHonorBonuses
eventMysekaiFixtureGameCharacterPerformanceBonusLimits eventSkillScoreUpLimits
ingameCombos ingameNotes mysekaiFixtureGameCharacterGroups
mysekaiFixtureGameCharacterGroupPerformanceBonuses mysekaiGates mysekaiGateLevels
""".split()


def empty_master_data():
    return {key: "[]" for key in REQUIRED_MASTER_DATA_KEYS + OPTIONAL_MASTER_DATA_KEYS}


def empty_user_data():
    return json.dumps(
        {
            "userGamedata": {},
            "userAreas": [],
            "userCards": [],
            "userCharacters": [],
            "userHonors": [],
            "userMysekaiCanvases": [],
            "userMysekaiFixtureGameCharacterPerformanceBonuses": [],
            "userMysekaiGates": [],
        }
    )


class BindingSmokeTests(unittest.TestCase):
    def setUp(self):
        self.engine = binding.SekaiDeckRecommend()

    def load_region(self, *, music=True):
        self.engine.update_masterdata_from_strings(empty_master_data(), "jp")
        if music:
            self.engine.update_musicmetas_from_string("{}", "jp")

    def test_updates_and_copies_region_data(self):
        self.load_region()
        copied = binding.SekaiDeckRecommend(self.engine)
        self.assertIsInstance(copied, binding.SekaiDeckRecommend)

    def test_snapshot_requires_master_data(self):
        options = binding.DeckRecommendOptions()
        options.region = "jp"
        options.live_type = "invalid"
        deck = binding.RecommendDeck()
        with self.assertRaisesRegex(ValueError, "Master data not found"):
            self.engine.recommend_music(options, deck)

    def test_snapshot_requires_music_metas(self):
        self.load_region(music=False)
        options = binding.DeckRecommendOptions()
        options.region = "jp"
        options.live_type = "invalid"
        deck = binding.RecommendDeck()
        with self.assertRaisesRegex(ValueError, "Music metas not found"):
            self.engine.recommend_music(options, deck)

    def test_snapshot_supplies_consistent_region_data(self):
        self.load_region()
        options = binding.DeckRecommendOptions()
        options.region = "jp"
        options.live_type = "invalid"
        deck = binding.RecommendDeck()
        with self.assertRaisesRegex(ValueError, "Invalid live type"):
            self.engine.recommend_music(options, deck)

    def test_recommend_options_use_region_snapshot(self):
        self.load_region()
        options = binding.DeckRecommendOptions()
        options.region = "jp"
        options.user_data_str = empty_user_data()
        options.live_type = "invalid"
        with self.assertRaisesRegex(ValueError, "Invalid live type"):
            self.engine.recommend(options)

    def test_support_cards_use_region_snapshot(self):
        self.load_region()
        options = binding.DeckRecommendOptions()
        options.region = "jp"
        options.user_data_str = empty_user_data()
        options.event_id = 1
        options.world_bloom_character_id = 1
        self.assertEqual(self.engine.get_world_bloom_support_cards(options), [])

    def test_exact_live_validates_after_snapshot(self):
        self.load_region()
        with self.assertRaisesRegex(ValueError, "Invalid live type"):
            self.engine.calculate_exact_live("jp", 1, [], "invalid", "{}")


if __name__ == "__main__":
    unittest.main()
