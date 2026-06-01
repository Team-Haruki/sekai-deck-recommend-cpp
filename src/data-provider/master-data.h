#ifndef MASTER_DATA_PROVIDER_H
#define MASTER_DATA_PROVIDER_H

#include "data-provider/master-data-types.h"

#include <unordered_map>


constexpr int legacyWorldBloom2FinaleEventId = 180;
constexpr int legacyWorldBloom2FinaleCardBonusCountLimit = 4;
constexpr int legacyWorldBloom2FinaleMysekaiFixtureBonusLimit = 20;
constexpr double legacyWorldBloom2FinaleSkillScoreUpLimit = 140.0;

struct MasterDataIndexes {
    std::unordered_map<int, const AreaItemLevel*> areaItemLevelByItemLevel;
    std::unordered_map<int, int> maxAreaItemLevelByItem;
    std::unordered_map<int, const Card*> cardById;
    std::unordered_map<int, std::vector<const CardEpisode*>> cardEpisodesByCardId;
    std::unordered_map<int, const CardEpisode*> cardEpisodeById;
    std::unordered_map<int, const CardMysekaiCanvasBonus*> canvasBonusByRarity;
    std::unordered_map<int, const CardRarity*> cardRarityByType;
    std::unordered_map<int, const CharacterRank*> characterRankByCharacterRank;
    std::unordered_map<int, const GameCharacter*> gameCharacterById;
    std::unordered_map<int, const Honor*> honorById;
    std::unordered_map<int, std::vector<const MasterLesson*>> masterLessonsByRarity;
    std::unordered_map<int, const Skill*> skillById;
    std::unordered_map<int, const WorldBloomDifferentAttributeBonus*> worldBloomDiffAttrBonusByCount;
};


class MasterData {

private:

    void addFakeEvent(int eventType);

public:
    std::string baseDir;
    MasterDataIndexes indexes;

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

    void buildIndexes();

    const AreaItemLevel* findAreaItemLevel(int areaItemId, int level) const;

    int getMaxAreaItemLevelIndexed(int areaItemId) const;

    const Card* findCard(int cardId) const;

    const CardEpisode* findCardEpisode(int cardEpisodeId) const;

    const std::vector<const CardEpisode*>& getCardEpisodesByCardId(int cardId) const;

    const CardMysekaiCanvasBonus* findCanvasBonus(int cardRarityType) const;

    const CardRarity* findCardRarity(int cardRarityType) const;

    const CharacterRank* findCharacterRank(int characterId, int characterRank) const;

    const GameCharacter* findGameCharacter(int characterId) const;

    const Honor* findHonor(int honorId) const;

    const std::vector<const MasterLesson*>& getMasterLessonsByRarity(int cardRarityType) const;

    const Skill* findSkill(int skillId) const;

    const WorldBloomDifferentAttributeBonus* findWorldBloomDiffAttrBonus(int attributeCount) const;

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
