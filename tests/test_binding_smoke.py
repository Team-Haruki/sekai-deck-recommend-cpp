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


WL3_TEST_CARD_CHARACTERS = {
    1: 1,
    2: 5,
    3: 9,
    4: 13,
    5: 17,
    6: 2,
    7: 6,
    8: 21,
    201: 1,
    202: 1,
    203: 1,
}


def wl3_test_unit(character_id):
    if character_id > 20:
        return "piapro"
    return ("light_sound", "idol", "street", "theme_park", "school_refusal")[(character_id - 1) // 4]


def wl3_test_card(card_id, character_id, support_unit="none"):
    return {
        "id": card_id,
        "characterId": character_id,
        "cardRarityType": "rarity_4",
        "attr": "cute",
        "supportUnit": support_unit,
        "skillId": 1,
        "cardParameters": [
            {"cardLevel": 1, "cardParameterType": parameter, "power": 30000}
            for parameter in ("param1", "param2", "param3")
        ],
    }


def wl3_rule_master_data():
    data = empty_master_data()
    characters = [
        {"id": character_id, "unit": wl3_test_unit(character_id)}
        for character_id in range(1, 27)
    ]
    character_units = [
        {
            "id": character_id,
            "gameCharacterId": character_id,
            "unit": wl3_test_unit(character_id),
        }
        for character_id in range(1, 27)
    ]
    character_units.append(
        {"id": 101, "gameCharacterId": 21, "unit": "light_sound"}
    )
    cards = [
        wl3_test_card(card_id, character_id, "light_sound" if card_id == 8 else "none")
        for card_id, character_id in WL3_TEST_CARD_CHARACTERS.items()
    ]
    data.update(
        {
            "cards": json.dumps(cards),
            "cardRarities": json.dumps(
                [
                    {
                        "cardRarityType": "rarity_4",
                        "maxLevel": 1,
                        "trainingMaxLevel": 1,
                        "maxSkillLevel": 1,
                    }
                ]
            ),
            "characterRanks": json.dumps(
                [
                    {
                        "id": character_id,
                        "characterId": character_id,
                        "characterRank": 1,
                    }
                    for character_id in range(1, 27)
                ]
            ),
            "eventCards": json.dumps(
                [
                    {
                        "id": card_id,
                        "eventId": 202,
                        "cardId": card_id,
                        "bonusRate": 30,
                        "leaderBonusRate": 0,
                    }
                    for card_id in range(1, 9)
                ]
            ),
            "eventRarityBonusRates": json.dumps(
                [
                    {
                        "id": 1,
                        "cardRarityType": "rarity_4",
                        "masterRank": 0,
                        "bonusRate": 10,
                    }
                ]
            ),
            "events": json.dumps(
                [
                    {"id": 163, "eventType": "world_bloom"},
                    {"id": 202, "eventType": "world_bloom"},
                ]
            ),
            "gameCharacters": json.dumps(characters),
            "gameCharacterUnits": json.dumps(character_units),
            "honors": json.dumps(
                [
                    {
                        "id": 301,
                        "assetbundleName": "honor_top_001000_event_wl_3rd_part1_cp1",
                        "levels": [{"honorId": 301, "level": 1, "bonus": 0}],
                    },
                    {
                        "id": 302,
                        "assetbundleName": "honor_top_001000_event_wl_2nd_part1_cp1",
                        "levels": [{"honorId": 302, "level": 1, "bonus": 0}],
                    },
                ]
            ),
            "skills": json.dumps(
                [
                    {
                        "id": 1,
                        "skillEffects": [
                            {
                                "id": 1,
                                "skillEffectType": "score_up",
                                "skillEffectDetails": [
                                    {
                                        "id": 1,
                                        "level": 1,
                                        "activateEffectValue": 200,
                                    }
                                ],
                            }
                        ],
                    }
                ]
            ),
            "worldBloomDifferentAttributeBonuses": json.dumps(
                [{"attributeCount": count, "bonusRate": 0} for count in range(1, 6)]
            ),
            "worldBlooms": json.dumps(
                [
                    {
                        "id": 1,
                        "eventId": 202,
                        "gameCharacterId": 1,
                        "chapterNo": 1,
                        "worldBloomChapterType": "game_character",
                    },
                    {
                        "id": 2,
                        "eventId": 163,
                        "gameCharacterId": 1,
                        "chapterNo": 1,
                        "worldBloomChapterType": "game_character",
                    }
                ]
            ),
            "worldBloomSupportDeckUnitEventLimitedBonuses": json.dumps(
                [
                    {
                        "id": 1,
                        "eventId": 202,
                        "gameCharacterId": 1,
                        "cardId": 201,
                        "bonusRate": 20,
                    },
                    {
                        "id": 2,
                        "eventId": 163,
                        "gameCharacterId": 1,
                        "cardId": 203,
                        "bonusRate": 20,
                    },
                ]
            ),
        }
    )
    return data


def wl3_test_user_data(card_ids, fixture_rate=100, honor_ids=()):
    character_ids = sorted({WL3_TEST_CARD_CHARACTERS[card_id] for card_id in card_ids})
    return json.dumps(
        {
            "userGamedata": {"userId": 1},
            "userAreas": [],
            "userCards": [
                {
                    "userId": 1,
                    "cardId": card_id,
                    "level": 1,
                    "skillLevel": 1,
                    "masterRank": 0,
                    "specialTrainingStatus": "not_doing",
                    "defaultImage": "original",
                    "episodes": [],
                }
                for card_id in card_ids
            ],
            "userChallengeLiveSoloDecks": [],
            "userCharacters": [
                {"characterId": character_id, "characterRank": 1}
                for character_id in character_ids
            ],
            "userDecks": [],
            "userHonors": [
                {"honorId": honor_id, "level": 1} for honor_id in honor_ids
            ],
            "userMysekaiCanvases": [],
            "userMysekaiFixtureGameCharacterPerformanceBonuses": [
                {
                    "gameCharacterId": character_id,
                    "totalBonusRate": fixture_rate,
                }
                for character_id in character_ids
            ],
            "userMysekaiGates": [],
            "userWorldBloomSupportDecks": [],
        }
    ).encode()


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


class WorldBloom3FinaleRuleTests(unittest.TestCase):
    def setUp(self):
        self.init_region("jp")

    def init_region(self, region):
        self.region = region
        self.engine = binding.SekaiDeckRecommend()
        self.engine.update_masterdata_from_strings(wl3_rule_master_data(), region)
        self.engine.update_musicmetas_from_string(
            json.dumps(
                [
                    {
                        "music_id": 1,
                        "difficulty": "expert",
                        "music_time": 100,
                        "event_rate": 100,
                        "base_score": 1,
                        "base_score_auto": 1,
                        "skill_score_solo": [0] * 6,
                        "skill_score_auto": [0] * 6,
                        "skill_score_multi": [0] * 6,
                        "fever_score": 0,
                        "fever_end_time": 0,
                        "tap_count": 100,
                    }
                ]
            ),
            region,
        )

    def recommend(self, card_ids, leader, *, finale=True, honor_ids=(), event_id=None):
        event_options = (
            {"world_bloom_finale_turn": 3, "forcedLeaderCharacterId": leader}
            if finale
            else {"world_bloom_event_turn": 3, "world_bloom_character_id": leader}
        )
        if event_id is not None:
            event_options = {"event_id": event_id, "world_bloom_character_id": leader}
        options = binding.DeckRecommendOptions.from_dict(
            {
                "region": self.region,
                "user_data_str": wl3_test_user_data(card_ids, honor_ids=honor_ids),
                "live_type": "multi",
                "music_id": 1,
                "music_diff": "expert",
                "target": "power",
                "algorithm": "dfs",
                "limit": 1,
                "fixed_cards": card_ids,
                **event_options,
            }
        )
        return self.engine.recommend(options).decks[0]

    def test_shuffle_unit_bonus_and_five_event_members(self):
        cases = (
            ([1, 6, 2, 7, 3], 1, 230),
            ([1, 6, 2, 3, 4], 1, 250),
            ([1, 2, 3, 4, 5], 1, 270),
            # Card 8 is a virtual singer with a light_sound support unit. It
            # still counts as piapro, making this a five-unit deck.
            ([8, 2, 3, 4, 5], 21, 270),
        )
        for card_ids, leader, expected_bonus in cases:
            with self.subTest(card_ids=card_ids):
                deck = self.recommend(card_ids, leader)
                self.assertEqual(deck.event_bonus_rate, expected_bonus)
                self.assertEqual(
                    sorted(card.event_bonus_rate for card in deck.cards),
                    [40, 40, 40, 40, 60],
                )

    def test_event_skill_cap_uses_master_value_in_all_regions(self):
        for region in ("jp", "en", "cn", "tw", "kr"):
            for raw_cap in (140, 240):
                with self.subTest(region=region, raw_cap=raw_cap):
                    self.init_region(region)
                    data = wl3_rule_master_data()
                    data["eventSkillScoreUpLimits"] = json.dumps(
                        [{"id": 2, "eventId": 202, "scoreUpRateLimit": raw_cap}]
                    )
                    self.engine.update_masterdata_from_strings(data, region)
                    deck = self.recommend([1, 2, 3, 4, 5], 1, finale=False, event_id=202)
                    cap = raw_cap
                    self.assertEqual({card.skill_score_up for card in deck.cards}, {min(200, cap)})

    def test_wl3_power_cap_applies_to_chapters_and_finale_but_fixture_cap_is_finale_only(self):
        card_ids = [1, 2, 3, 4, 5]
        finale = self.recommend(card_ids, 1)
        normal = self.recommend(card_ids, 1, finale=False)

        self.assertEqual(finale.base_power, 450000)
        self.assertEqual(finale.fixture_bonus_power, 27000)
        self.assertEqual(finale.total_power, 336000)
        self.assertEqual(normal.fixture_bonus_power, 45000)
        self.assertEqual(normal.total_power, 336000)
        released_chapter = self.recommend(card_ids, 1, finale=False, event_id=202)
        self.assertEqual(released_chapter.total_power, 336000)
        wl2 = self.recommend(card_ids, 1, finale=False, event_id=163)
        self.assertGreater(wl2.total_power, 336000)

    def test_score_up_and_current_wl_honor_limits(self):
        card_ids = [1, 2, 3, 4, 5]
        current_honor = self.recommend(card_ids, 1, honor_ids=(301,))
        old_honor = self.recommend(card_ids, 1, honor_ids=(302,))
        normal = self.recommend(card_ids, 1, finale=False)

        self.assertEqual(current_honor.event_bonus_rate, 320)
        self.assertEqual(old_honor.event_bonus_rate, 270)
        self.assertEqual(max(card.event_bonus_rate for card in current_honor.cards), 110)
        self.assertEqual({card.skill_score_up for card in current_honor.cards}, {140})
        self.assertEqual({card.skill_score_up for card in normal.cards}, {200})

    def test_support_member_bonus_matches_leader_and_current_wl(self):
        options = binding.DeckRecommendOptions.from_dict(
            {
                "region": "jp",
                "user_data_str": wl3_test_user_data([201, 202, 203], fixture_rate=0),
                "world_bloom_finale_turn": 3,
                "forcedLeaderCharacterId": 1,
            }
        )
        bonuses = {
            card.card_id: card.bonus
            for card in self.engine.get_world_bloom_support_cards(options)
        }
        self.assertEqual(bonuses, {201: 32, 202: 12, 203: 12})


# JP 7.0.0 gates: five unit gates with level rows up to 70, plus gate 6
# ("shuffle", unit "none") which has no level rows at all.
GATE_TEST_CARDS = {
    # card id: (character id, support unit)
    11: (1, "none"),   # light_sound
    12: (5, "none"),   # idol
    13: (9, "none"),   # street (user has no street gate)
    14: (21, "none"),  # virtual singer without support unit
    15: (22, "idol"),  # virtual singer supporting idol
}


def gate_master_data():
    data = wl3_rule_master_data()
    data["cards"] = json.dumps(
        [
            wl3_test_card(card_id, character_id, support_unit)
            for card_id, (character_id, support_unit) in GATE_TEST_CARDS.items()
        ]
    )
    gate_units = ("light_sound", "idol", "street", "theme_park", "school_refusal")
    gates = [
        {"id": gate_id, "unit": unit, "mysekaiGateType": "unit"}
        for gate_id, unit in enumerate(gate_units, start=1)
    ]
    gates.append({"id": 6, "unit": "none", "mysekaiGateType": "shuffle"})
    data["mysekaiGates"] = json.dumps(gates)
    data["mysekaiGateLevels"] = json.dumps(
        [
            {
                "id": gate_id * 1000 + level,
                "mysekaiGateId": gate_id,
                "level": level,
                "powerBonusRate": level / 10,
            }
            for gate_id in range(1, 6)
            for level in range(1, 71)
        ]
    )
    return data


def gate_user_data(user_gates):
    character_ids = sorted({character_id for character_id, _ in GATE_TEST_CARDS.values()})
    return json.dumps(
        {
            "userGamedata": {"userId": 1},
            "userAreas": [],
            "userCards": [
                {
                    "userId": 1,
                    "cardId": card_id,
                    "level": 1,
                    "skillLevel": 1,
                    "masterRank": 0,
                    "specialTrainingStatus": "not_doing",
                    "defaultImage": "original",
                    "episodes": [],
                }
                for card_id in GATE_TEST_CARDS
            ],
            "userCharacters": [
                {"characterId": character_id, "characterRank": 1}
                for character_id in character_ids
            ],
            "userDecks": [],
            "userHonors": [],
            "userMysekaiCanvases": [],
            "userMysekaiFixtureGameCharacterPerformanceBonuses": [],
            "userMysekaiGates": [
                {"mysekaiGateId": gate_id, "mysekaiGateLevel": level}
                for gate_id, level in user_gates
            ],
        }
    ).encode()


class MysekaiGateBonusTests(unittest.TestCase):
    def setUp(self):
        self.engine = binding.SekaiDeckRecommend()
        self.engine.update_masterdata_from_strings(gate_master_data(), "jp")
        self.engine.update_musicmetas_from_string(
            json.dumps(
                [
                    {
                        "music_id": 1,
                        "difficulty": "expert",
                        "music_time": 100,
                        "event_rate": 100,
                        "base_score": 1,
                        "base_score_auto": 1,
                        "skill_score_solo": [0] * 6,
                        "skill_score_auto": [0] * 6,
                        "skill_score_multi": [0] * 6,
                        "fever_score": 0,
                        "fever_end_time": 0,
                        "tap_count": 100,
                    }
                ]
            ),
            "jp",
        )

    def gate_bonuses(self, user_gates):
        options = binding.DeckRecommendOptions.from_dict(
            {
                "region": "jp",
                "user_data_str": gate_user_data(user_gates),
                "live_type": "multi",
                "music_id": 1,
                "music_diff": "expert",
                "target": "power",
                "algorithm": "dfs",
                "limit": 1,
                "fixed_cards": list(GATE_TEST_CARDS),
            }
        )
        deck = self.engine.recommend(options).decks[0]
        bonuses = {card.card_id: card.total_power - card.base_power for card in deck.cards}
        self.assertEqual(deck.gate_bonus_power, sum(bonuses.values()))
        return bonuses

    def test_shuffle_gate_without_levels_does_not_break_calculation(self):
        # Base power is 90000 per card; level 70 = 7%, level 40 = 4%.
        bonuses = self.gate_bonuses([(1, 70), (2, 40), (6, 1)])
        self.assertEqual(
            bonuses,
            {
                11: 6300,  # own unit gate, level 70
                12: 3600,  # own unit gate, level 40
                13: 0,  # no street gate
                14: 6300,  # highest-level gate (gate 1)
                15: 3600,  # support unit gate only
            },
        )

    def test_virtual_singer_uses_highest_level_gate_even_without_level_rows(self):
        # The client orders userMysekaiGates by level and takes the first; a
        # gate without a level row then yields 0 instead of falling back.
        bonuses = self.gate_bonuses([(1, 70), (2, 40), (6, 71)])
        self.assertEqual(bonuses[14], 0)
        self.assertEqual(bonuses[11], 6300)
        self.assertEqual(bonuses[15], 3600)

    def test_virtual_singer_ties_keep_user_data_order(self):
        self.assertEqual(self.gate_bonuses([(6, 40), (2, 40)])[14], 0)
        self.assertEqual(self.gate_bonuses([(2, 40), (6, 40)])[14], 3600)

    def test_unknown_gate_or_level_contributes_zero(self):
        bonuses = self.gate_bonuses([(99, 1), (1, 999)])
        self.assertEqual(set(bonuses.values()), {0})


if __name__ == "__main__":
    unittest.main()
