#ifndef MASTER_DATA_PROVIDER_H
#define MASTER_DATA_PROVIDER_H

#include "data-provider/master-data-types.h"

#include <unordered_map>
#include <unordered_set>


constexpr int legacyWorldBloom2FinaleEventId = 180;
constexpr int legacyWorldBloom2FinaleCardBonusCountLimit = 4;
constexpr int legacyWorldBloom2FinaleMysekaiFixtureBonusLimit = 20;
constexpr double legacyWorldBloom2FinaleSkillScoreUpLimit = 140.0;


class MasterData {

private:

    void addFakeEvent(int eventType);

public:
    std::string baseDir;

    std::vector<AreaItemLevel> areaItemLevels;
    std::vector<AreaItem> areaItems;
    std::vector<Area> areas;
    std::vector<CardEpisode> cardEpisodes;
    std::vector<Card> cards;
    std::vector<CardMysekaiCanvasBonus> cardMysekaiCanvasBonuses;
    std::vector<CardRarity> cardRarities;
    std::vector<CharacterRank> characterRanks;
    std::vector<EventCard> eventCards;
    std::vector<EventCardBonusLimit> eventCardBonusLimits;
    std::vector<EventDeckBonus> eventDeckBonuses;
    std::vector<EventExchangeSummary> eventExchangeSummaries;
    std::vector<EventHonorBonus> eventHonorBonuses;
    std::vector<Event> events;
    std::vector<EventItem> eventItems;
    std::vector<EventMysekaiFixtureGameCharacterPerformanceBonusLimit> eventMysekaiFixtureGameCharacterPerformanceBonusLimits;
    std::vector<EventRarityBonusRate> eventRarityBonusRates;
    std::vector<EventSkillScoreUpLimit> eventSkillScoreUpLimits;
    std::vector<GameCharacter> gameCharacters;
    std::vector<GameCharacterUnit> gameCharacterUnits;
    std::vector<Honor> honors;
    std::vector<IngameCombo> ingameCombos;
    std::vector<IngameNote> ingameNotes;
    std::vector<MasterLesson> masterLessons;
    std::vector<MusicDifficulty> musicDifficulties;
    std::vector<Music> musics;
    std::vector<MusicVocal> musicVocals;
    std::vector<MysekaiFixtureGameCharacterGroup> mysekaiFixtureGameCharacterGroups;
    std::vector<MysekaiFixtureGameCharacterGroupPerformanceBonus> mysekaiFixtureGameCharacterGroupPerformanceBonuses;
    std::vector<MysekaiGate> mysekaiGates;
    std::vector<MysekaiGateLevel> mysekaiGateLevels;
    std::vector<ShopItem> shopItems;
    std::vector<Skill> skills;
    std::vector<WorldBloomDifferentAttributeBonus> worldBloomDifferentAttributeBonuses;
    std::vector<WorldBloom> worldBlooms;
    std::vector<WorldBloomSupportDeckUnitEventLimitedBonus> worldBloomSupportDeckUnitEventLimitedBonuses;

    std::vector<WorldBloomSupportDeckBonus> worldBloomSupportDeckBonusesWL1;
    std::vector<WorldBloomSupportDeckBonus> worldBloomSupportDeckBonusesWL2;
    std::vector<WorldBloomSupportDeckBonus> worldBloomSupportDeckBonusesWL3;

    // id -> 在对应 vector 中的下标索引，避免热路径上的 O(n) 线性查找。
    // 在 loadFromJsons 末尾构建；对应 vector 之后不再变动。
    std::unordered_map<int, int> cardIdToIndex;
    std::unordered_map<int, int> skillIdToIndex;
    std::unordered_map<int, int> cardEpisodeIdToIndex;
    std::unordered_map<int, int> characterRankToIndex;   // key = characterId * 1000 + characterRank
    std::unordered_set<int> worldBloomFinaleEventIds;    // 预计算的终章活动 id 集合

    // 按 id 查，找不到返回 nullptr（O(1)）。
    const Card* findCardById(int id) const;
    const Skill* findSkillById(int id) const;
    const CardEpisode* findCardEpisodeById(int id) const;
    const CharacterRank* findCharacterRank(int characterId, int characterRank) const;

    void loadFromJsons(std::map<std::string, json_doc>& jsons);

    void loadFromFiles(const std::string& baseDir);

    void loadFromStrings(std::map<std::string, std::string>& data);

    int getNoEventFakeEventId(int eventType) const;

    int getUnitAttrFakeEventId(int eventType, int unit, int attr) const;

    int getWorldBloomFakeEventId(int worldBloomTurn, int unit) const;

    int getWorldBloom3PartByCharacterId(int characterId) const;

    int getWorldBloomEventTurn(int eventId) const;

    bool isWorldBloomFinale(int eventId) const;

    int getEventCardBonusCountLimit(int eventId) const;

    std::optional<double> getEventSkillScoreUpLimit(int eventId) const;

    std::optional<int> getMysekaiFixtureBonusLimit(int eventId) const;

};

#endif // MASTER_DATA_PROVIDER_H
