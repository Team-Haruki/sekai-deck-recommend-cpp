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

    // 加载后预构建的查询缓存；这些查询位于组卡评估的热路径上，
    // 逐次线性扫描 worldBlooms/eventCardBonusLimits 的开销不可接受。
    void buildDerivedCaches();

    std::unordered_set<int> worldBloomFinaleEventIds;
    std::unordered_map<int, int> eventCardBonusCountLimits;
    std::unordered_map<int, int> honorIndexById;
    std::unordered_map<int, std::vector<int>> eventDeckBonusIndicesByEventId;
    std::unordered_map<int, int> gameCharacterUnitIndexById;

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

    const Honor& getHonorById(int honorId) const;

    // 指定活动的eventDeckBonuses下标列表（无则返回空列表）
    const std::vector<int>& getEventDeckBonusIndices(int eventId) const;

    const GameCharacterUnit& getGameCharacterUnitById(int gameCharacterUnitId) const;

    std::optional<double> getEventSkillScoreUpLimit(int eventId) const;

    std::optional<int> getMysekaiFixtureBonusLimit(int eventId) const;

};

#endif // MASTER_DATA_PROVIDER_H
