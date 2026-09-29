"""Area item rules added in JP 7.0.0 (multi_unit effects, multi-row levels).

Expected values follow the client's CardUtility.GetAreaItemBuffList and
DeckUtility.IsMultiUnitDeck (UnityFramework 7.0.0). Every card has a base
power of 30000 per stat, so an effect of r% adds 900 * r to a card.
"""
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

MASTER_DATA_KEYS = """
areaItemLevels areaItems areas cardEpisodes cards cardRarities characterRanks
eventCards eventDeckBonuses eventExchangeSummaries events eventItems
eventRarityBonusRates gameCharacters gameCharacterUnits honors masterLessons
musicDifficulties musics musicVocals shopItems skills
worldBloomDifferentAttributeBonuses worldBlooms worldBloomSupportDeckBonuses
worldBloomSupportDeckUnitEventLimitedBonuses cardMysekaiCanvasBonuses
eventCardBonusLimits eventHonorBonuses
eventMysekaiFixtureGameCharacterPerformanceBonusLimits eventSkillScoreUpLimits
ingameCombos ingameNotes mysekaiFixtureGameCharacterGroups
mysekaiFixtureGameCharacterGroupPerformanceBonuses mysekaiGates mysekaiGateLevels
""".split()

UNITS = ("light_sound", "idol", "street", "theme_park", "school_refusal")

# card id: (character id, support unit)
CARDS = {
    1: (1, "none"),  # light_sound
    2: (2, "none"),
    3: (3, "none"),
    4: (4, "none"),
    5: (5, "none"),  # idol
    6: (6, "none"),
    21: (21, "none"),  # virtual singers without support unit
    22: (22, "none"),
    23: (23, "none"),
    24: (24, "none"),
    25: (25, "none"),
    122: (22, "light_sound"),  # virtual singers supporting light_sound
    123: (23, "light_sound"),
    124: (24, "light_sound"),
    125: (25, "light_sound"),
    126: (26, "light_sound"),
    136: (26, "idol"),
}

LIGHT_SOUND_ITEM = 1  # light_sound: normal 10%, all-match 15% (levels 16-20: 11% / 16%)
PIAPRO_ITEM = 2  # piapro: level 1 normal 12% / all-match 20%, level 2 normal 15% / all-match 20%
MULTI_ITEM = 56  # level 1: all characters 1% + multi_unit 3%; level 2: 1% + 6%


def character_unit(character_id):
    return "piapro" if character_id > 20 else UNITS[(character_id - 1) // 4]


def level_row(item_id, level, rate, all_match=0.0, unit="any", attr="any"):
    return {
        "areaItemId": item_id,
        "level": level,
        "targetUnit": unit,
        "targetCardAttr": attr,
        "targetGameCharacterId": 0,
        **{f"power{i}BonusRate": rate for i in (1, 2, 3)},
        **{f"power{i}AllMatchBonusRate": all_match for i in (1, 2, 3)},
    }


def master_data(with_multi_rows=True):
    data = {key: "[]" for key in MASTER_DATA_KEYS}
    levels = [
        level_row(LIGHT_SOUND_ITEM, level, 10 + (level > 15), 15 + (level > 15), unit="light_sound")
        for level in range(1, 21)
    ] + [
        level_row(PIAPRO_ITEM, 1, 12, 20, unit="piapro"),
        level_row(PIAPRO_ITEM, 2, 15, 20, unit="piapro"),
    ]
    for level, multi_rate in ((1, 3), (2, 6)):
        levels.append(level_row(MULTI_ITEM, level, 1))
        if with_multi_rows:
            levels.append(level_row(MULTI_ITEM, level, multi_rate, unit="multi_unit"))
    shop_ids = [1001 + level - 1 for level in range(1, 11)]
    shop_ids += [1551 + level - 11 for level in range(11, 16)]
    shop_ids += [1826 + level - 16 for level in range(16, 21)]
    shop_ids += [1011, 1012, 2101, 2102]
    data.update(
        {
            "areaItemLevels": json.dumps(levels),
            "areaItems": json.dumps(
                [
                    {"id": LIGHT_SOUND_ITEM, "areaId": 1},
                    {"id": PIAPRO_ITEM, "areaId": 1},
                    {"id": MULTI_ITEM, "areaId": 27},
                ]
            ),
            "areas": json.dumps(
                [
                    {"id": 1, "areaType": "spirit_world", "viewType": "side_view"},
                    {"id": 27, "areaType": "reality_world", "viewType": "side_view"},
                ]
            ),
            "shopItems": json.dumps(
                [
                    {
                        "id": shop_id,
                        "shopId": 1,
                        "costs": [
                            {"shopItemId": shop_id, "seq": 1, "cost": {"resourceType": "coin", "resourceId": 0, "quantity": 100}}
                        ],
                    }
                    for shop_id in shop_ids
                ]
            ),
            "cards": json.dumps(
                [
                    {
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
                    for card_id, (character_id, support_unit) in CARDS.items()
                ]
            ),
            "cardRarities": json.dumps(
                [{"cardRarityType": "rarity_4", "maxLevel": 1, "trainingMaxLevel": 1, "maxSkillLevel": 1}]
            ),
            "characterRanks": json.dumps(
                [{"id": c, "characterId": c, "characterRank": 1} for c in range(1, 27)]
            ),
            "gameCharacters": json.dumps(
                [{"id": c, "unit": character_unit(c)} for c in range(1, 27)]
            ),
            "gameCharacterUnits": json.dumps(
                [{"id": c, "gameCharacterId": c, "unit": character_unit(c)} for c in range(1, 27)]
            ),
            "skills": json.dumps(
                [
                    {
                        "id": 1,
                        "skillEffects": [
                            {
                                "id": 1,
                                "skillEffectType": "score_up",
                                "skillEffectDetails": [{"id": 1, "level": 1, "activateEffectValue": 100}],
                            }
                        ],
                    }
                ]
            ),
            "worldBloomDifferentAttributeBonuses": json.dumps(
                [{"attributeCount": count, "bonusRate": 0} for count in range(1, 6)]
            ),
        }
    )
    return data


def user_data(area_items):
    return json.dumps(
        {
            "userGamedata": {"userId": 1},
            "userAreas": [
                {
                    "areaId": 1,
                    "areaItems": [
                        {"areaItemId": item_id, "level": level}
                        for item_id, level in area_items.items()
                        if item_id != MULTI_ITEM
                    ],
                },
                {
                    "areaId": 27,
                    "areaItems": [
                        {"areaItemId": item_id, "level": level}
                        for item_id, level in area_items.items()
                        if item_id == MULTI_ITEM
                    ],
                },
            ],
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
                for card_id in CARDS
            ],
            "userCharacters": [{"characterId": c, "characterRank": 1} for c in range(1, 27)],
            "userDecks": [],
            "userHonors": [],
            "userMysekaiCanvases": [],
            "userMysekaiFixtureGameCharacterPerformanceBonuses": [],
            "userMysekaiGates": [],
        }
    ).encode()


MUSIC_METAS = json.dumps(
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
)

LIGHT_SOUND_DECK = [1, 2, 3, 4]
MIXED_DECK = [1, 2, 5, 6, 21]


class AreaItemMultiUnitTests(unittest.TestCase):
    def setUp(self):
        self.load(with_multi_rows=True)

    def load(self, with_multi_rows):
        self.engine = binding.SekaiDeckRecommend()
        self.engine.update_masterdata_from_strings(master_data(with_multi_rows), "jp")
        self.engine.update_musicmetas_from_string(MUSIC_METAS, "jp")

    def area_bonus(self, cards, area_items, evaluation=None):
        options = {
            "region": "jp",
            "user_data_str": user_data(area_items),
            "live_type": "multi",
            "music_id": 1,
            "music_diff": "expert",
            "target": "power",
            "algorithm": "dfs",
            "limit": 1,
            "member": len(cards),
            "fixed_cards": cards,
        }
        if evaluation is not None:
            options["multi_unit_bonus_evaluation"] = evaluation
        deck = self.engine.recommend(binding.DeckRecommendOptions.from_dict(options)).decks[0]
        bonuses = {card.card_id: card.total_power - card.base_power for card in deck.cards}
        return bonuses, deck

    def multi_applies(self, cards):
        # Only item 56 level 1: 1% for everyone plus 3% when the deck is multi-unit.
        bonuses, _ = self.area_bonus(cards, {MULTI_ITEM: 1})
        values = set(bonuses.values())
        self.assertEqual(len(values), 1, bonuses)
        value = values.pop()
        self.assertIn(value, (900, 3600))
        return value == 3600

    def test_is_multi_unit_deck(self):
        cases = (
            # Four light_sound members; results pad to five with the leader.
            ("single unit", LIGHT_SOUND_DECK, False),
            ("five virtual singers without support", [21, 22, 23, 24, 25], False),
            ("unit members plus unsupported virtual singer", LIGHT_SOUND_DECK + [21], True),
            ("unit members plus virtual singer supporting that unit", LIGHT_SOUND_DECK + [122], True),
            ("five virtual singers supporting the same unit", [122, 123, 124, 125, 126], True),
            ("virtual singers supporting two units", [122, 123, 124, 125, 136], True),
            ("mixed units", MIXED_DECK, True),
        )
        for name, cards, expected in cases:
            with self.subTest(name):
                self.assertEqual(self.multi_applies(cards), expected)

    def test_all_character_row_applies_without_multi_unit(self):
        bonuses, deck = self.area_bonus([21, 22, 23, 24, 25], {MULTI_ITEM: 2})
        self.assertEqual(set(bonuses.values()), {900})
        self.assertEqual(deck.area_item_bonus_power, 5 * 900)

    def test_all_match_bonus_beats_smaller_multi_unit_bonus(self):
        # All five match light_sound and the deck is multi-unit (the virtual
        # singer counts as piapro). All-match extra 5% >= multi 3%: multi dropped.
        bonuses, _ = self.area_bonus(LIGHT_SOUND_DECK + [122], {LIGHT_SOUND_ITEM: 1, MULTI_ITEM: 1})
        self.assertEqual(bonuses, {1: 900 * 16, 2: 900 * 16, 3: 900 * 16, 4: 900 * 16, 122: 900 * 16})

    def test_larger_multi_unit_bonus_replaces_all_match_extra(self):
        # Multi 6% > all-match extra 5%: unit falls back to 10% and multi is kept.
        bonuses, _ = self.area_bonus(LIGHT_SOUND_DECK + [122], {LIGHT_SOUND_ITEM: 1, MULTI_ITEM: 2})
        self.assertEqual(set(bonuses.values()), {900 * 17})

    def test_support_unit_is_chosen_before_multi_unit_adjustment(self):
        # Virtual singer supporting light_sound: piapro 12% vs light_sound
        # all-match 15%, so piapro is dropped first; then multi 6% replaces
        # the 5% all-match extra -> 1 + 10 + 6. Choosing piapro after the
        # multi adjustment would give 1 + 12 + 6 instead.
        bonuses, _ = self.area_bonus(
            LIGHT_SOUND_DECK + [122], {LIGHT_SOUND_ITEM: 1, PIAPRO_ITEM: 1, MULTI_ITEM: 2}
        )
        self.assertEqual(bonuses[122], 900 * 17)
        # Multi 3% < all-match extra 5%: multi dropped, support keeps 15%.
        bonuses, _ = self.area_bonus(
            LIGHT_SOUND_DECK + [122], {LIGHT_SOUND_ITEM: 1, PIAPRO_ITEM: 1, MULTI_ITEM: 1}
        )
        self.assertEqual(bonuses[122], 900 * 16)

    def test_unit_and_support_tie_keeps_character_unit(self):
        # piapro 15% ties light_sound all-match 15%: the character unit wins,
        # has no all-match extra, so multi 3% is added -> 1 + 15 + 3.
        bonuses, _ = self.area_bonus(
            LIGHT_SOUND_DECK + [122], {LIGHT_SOUND_ITEM: 1, PIAPRO_ITEM: 2, MULTI_ITEM: 1}
        )
        self.assertEqual(bonuses[122], 900 * 19)

    def test_force_on_and_force_off(self):
        single_vs = [21, 22, 23, 24, 25]
        by_deck, _ = self.area_bonus(single_vs, {MULTI_ITEM: 1})
        forced_on, _ = self.area_bonus(single_vs, {MULTI_ITEM: 1}, "force_on")
        self.assertEqual(set(by_deck.values()), {900})
        self.assertEqual(set(forced_on.values()), {3600})

        by_deck, _ = self.area_bonus(MIXED_DECK, {MULTI_ITEM: 1}, "by_deck")
        forced_off, _ = self.area_bonus(MIXED_DECK, {MULTI_ITEM: 1}, "force_off")
        self.assertEqual(set(by_deck.values()), {3600})
        self.assertEqual(set(forced_off.values()), {900})

    def test_invalid_evaluation_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "Invalid multi unit bonus evaluation"):
            self.area_bonus(MIXED_DECK, {MULTI_ITEM: 1}, "sometimes")

    def test_evaluation_is_noop_without_multi_unit_rows(self):
        # Regions whose master has no multi_unit rows keep the old calculation.
        self.load(with_multi_rows=False)
        items = {LIGHT_SOUND_ITEM: 1, PIAPRO_ITEM: 1, MULTI_ITEM: 2}
        for cards in (MIXED_DECK, LIGHT_SOUND_DECK + [122]):
            results = [
                self.area_bonus(cards, items, evaluation)[0]
                for evaluation in (None, "force_on", "force_off")
            ]
            with self.subTest(cards=cards):
                self.assertEqual(results[0], results[1])
                self.assertEqual(results[0], results[2])
        bonuses, _ = self.area_bonus(LIGHT_SOUND_DECK + [122], items, "force_on")
        self.assertEqual(bonuses[1], 900 * 16)
        self.assertEqual(bonuses[122], 900 * 16)

    def test_area_item_recommendation_swaps_all_rows(self):
        options = binding.DeckRecommendOptions.from_dict(
            {"region": "jp", "user_data_str": user_data({LIGHT_SOUND_ITEM: 15, MULTI_ITEM: 1})}
        )
        items = {
            item["area_item_id"]: item
            for item in self.engine.recommend_area_items(options, MIXED_DECK)
        }
        # Level 2 of item 56 raises both rows: multi 3% -> 6% on all five cards.
        self.assertEqual(items[MULTI_ITEM]["next_level"], 2)
        self.assertEqual(items[MULTI_ITEM]["shop_item_id"], 2102)
        self.assertEqual(items[MULTI_ITEM]["power"], 5 * 900 * 3)
        self.assertEqual(items[LIGHT_SOUND_ITEM]["next_level"], 16)
        self.assertEqual(items[LIGHT_SOUND_ITEM]["power"], 2 * 900)
        self.assertEqual(items[LIGHT_SOUND_ITEM]["shop_item_id"], 1826)
        self.assertEqual(items[PIAPRO_ITEM]["shop_item_id"], 1011)

        forced_off = binding.DeckRecommendOptions.from_dict(
            {
                "region": "jp",
                "user_data_str": user_data({LIGHT_SOUND_ITEM: 15, MULTI_ITEM: 1}),
                "multi_unit_bonus_evaluation": "force_off",
            }
        )
        items = {
            item["area_item_id"]: item
            for item in self.engine.recommend_area_items(forced_off, MIXED_DECK)
        }
        self.assertNotIn(MULTI_ITEM, items)


if __name__ == "__main__":
    unittest.main()
