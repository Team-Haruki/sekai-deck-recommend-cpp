#include "data-provider/master-data.h"
#include "data-provider/static-data.h"

#include <fstream>
#include <iostream>
#include <unordered_map>
#include <vector>
#include "master-data.h"


const std::vector<std::string> requiredMasterDataKeys = {
    "areaItemLevels",
    "areaItems",
    "areas",
    "cardEpisodes",
    "cards",
    "cardRarities",
    "characterRanks",
    "eventCards",
    "eventDeckBonuses",
    "eventExchangeSummaries",
    "events",
    "eventItems",
    "eventRarityBonusRates",
    "gameCharacters",
    "gameCharacterUnits",
    "honors",
    "masterLessons",
    "musicDifficulties",
    "musics",
    "musicVocals",
    "shopItems",
    "skills",
    "worldBloomDifferentAttributeBonuses",
    "worldBlooms",
    "worldBloomSupportDeckBonuses"
};
const std::vector<std::string> notRequiredMasterDataKeys = {
    "worldBloomSupportDeckUnitEventLimitedBonuses",
    "cardMysekaiCanvasBonuses",
    "mysekaiFixtureGameCharacterGroups",
    "mysekaiFixtureGameCharacterGroupPerformanceBonuses",
    "mysekaiGates",
    "mysekaiGateLevels"
};

static const std::vector<std::vector<int>> worldBloom3PartCharacterIds = {
    {21, 1, 6, 14, 17},
    {22, 23, 4, 5, 10, 13},
    {24, 3, 8, 9, 18},
    {26, 2, 12, 16, 20},
    {25, 7, 11, 15, 19},
};


void loadMasterDataJsonFromFile(std::map<std::string, json>& jsons, const std::string& baseDir, const std::string& key) {
    try {
        std::string filePath = baseDir + "/" + key + ".json";
        std::ifstream file(filePath);
        if (!file.is_open()) {
            jsons.erase(key);
            return;
        }
        json j;
        file >> j;
        file.close();
        jsons[key] = j;
    }
    catch (const std::exception& e) {
        throw std::runtime_error("Failed to load master data from file: " + key + ", error: " + e.what());
    }
}

void loadMasterDataJsonFromStrings(std::map<std::string, json>& jsons, std::map<std::string, std::string>& data, const std::string& key) {
    try {
        if (!data.count(key)) {
            jsons.erase(key);
            return;
        }
        json j = json::parse(data.at(key));
        jsons[key] = j;
    }
    catch (const std::exception& e) {
        throw std::runtime_error("Failed to load master data from string: " + key + ", error: " + e.what());
    }
}


void addFinalChapterEventIfNeeded(MasterData& md) {
    bool hasFinalChapter = false;
    for (const auto& e : md.events) {
        if (e.id == finalChapterEventId) {
            hasFinalChapter = true;
            break;
        }
    }
    if (!hasFinalChapter) {
        // 活动本身
        Event event;
        event.id = finalChapterEventId;
        event.eventType = Enums::EventType::world_bloom;
        md.events.push_back(event);

        // 角色加成
        for (auto& gameCharacterUnit : md.gameCharacterUnits) {
            EventDeckBonus bonus;
            bonus.eventId = finalChapterEventId;
            bonus.gameCharacterUnitId = gameCharacterUnit.id;
            bonus.bonusRate = 5.0;
            bonus.cardAttr = Enums::Attr::null;
            md.eventDeckBonuses.push_back(bonus);
        }

        // wl2限定卡牌加成
        const std::set<int> worldBloomEventIds = { 163, 167, 170, 171, 176, 179 };
        std::vector<EventCard> newEventCards{};
        for (const auto& eventCard : md.eventCards) {
            if (worldBloomEventIds.count(eventCard.eventId)) {
                auto newEventCard = eventCard;
                newEventCard.eventId = finalChapterEventId;
                newEventCard.bonusRate = 25.0;
                newEventCards.push_back(newEventCard);
            }
        }
        md.eventCards.insert(md.eventCards.end(), newEventCards.begin(), newEventCards.end());

        // 支援里的wl1限定卡牌加成
        std::vector<WorldBloomSupportDeckUnitEventLimitedBonus> newLimitedBonuses{};
        for (const auto& limitedBonus : md.worldBloomSupportDeckUnitEventLimitedBonuses) {
            auto newLimitBonus = limitedBonus;
            newLimitBonus.eventId = finalChapterEventId;
            newLimitedBonuses.push_back(newLimitBonus);
        }
        md.worldBloomSupportDeckUnitEventLimitedBonuses.insert(
            md.worldBloomSupportDeckUnitEventLimitedBonuses.end(),
            newLimitedBonuses.begin(),
            newLimitedBonuses.end()
        );
    }
}

static std::vector<WorldBloomSupportDeckUnitEventLimitedBonus> buildFakeWorldBloomSupportDeckUnitEventLimitedBonuses(
    const MasterData& md,
    int turn,
    int fakeEventId,
    const std::set<int>& charas
) {
    std::vector<WorldBloomSupportDeckUnitEventLimitedBonus> bonuses{};

    if (turn == 2) {
        for (const auto& bonus : md.worldBloomSupportDeckUnitEventLimitedBonuses) {
            if (bonus.eventId != finalChapterEventId
             && md.getWorldBloomEventTurn(bonus.eventId) == 2
             && charas.count(bonus.gameCharacterId)) {
                auto newBonus = bonus;
                newBonus.eventId = fakeEventId;
                bonuses.push_back(newBonus);
            }
        }
        return bonuses;
    }

    if (turn == 3) {
        std::unordered_map<int, int> cardCharacterMap{};
        for (const auto& card : md.cards) {
            cardCharacterMap[card.id] = card.characterId;
        }
        std::unordered_map<int, int> eventTypeMap{};
        for (const auto& event : md.events) {
            eventTypeMap[event.id] = event.eventType;
        }

        std::set<std::pair<int, int>> used{};
        for (const auto& eventCard : md.eventCards) {
            if (eventCard.eventId == finalChapterEventId
             || md.getWorldBloomEventTurn(eventCard.eventId) > 2
             || eventCard.bonusRate <= 0) {
                continue;
            }
            auto eventTypeIt = eventTypeMap.find(eventCard.eventId);
            if (eventTypeIt == eventTypeMap.end()
             || eventTypeIt->second != Enums::EventType::world_bloom) {
                continue;
            }

            auto it = cardCharacterMap.find(eventCard.cardId);
            if (it == cardCharacterMap.end()) {
                continue;
            }

            int gameCharacterId = it->second;
            if (!charas.count(gameCharacterId)) {
                continue;
            }

            auto key = std::make_pair(gameCharacterId, eventCard.cardId);
            if (used.count(key)) {
                continue;
            }
            used.insert(key);

            bonuses.push_back(WorldBloomSupportDeckUnitEventLimitedBonus{
                .id = 0,
                .eventId = fakeEventId,
                .gameCharacterId = gameCharacterId,
                .cardId = eventCard.cardId,
                .bonusRate = 20.0,
            });
        }
    }

    return bonuses;
}


template <typename T>
std::vector<T> loadMasterData(std::map<std::string, json>& jsons, const std::string& key, bool required = true) {
    if (!jsons.count(key)) {
        if (required) {
            throw std::runtime_error("master data key not found: " + key);
        } else {
            std::cerr << "[sekai-deck-recommend-cpp] warning: master data key not found: " + key << std::endl;
            return {};
        }
    }
    return T::fromJsonList(jsons.at(key));
}

void MasterData::loadFromJsons(std::map<std::string, json>& jsons) {
    this->areaItemLevels = loadMasterData<AreaItemLevel>(jsons, "areaItemLevels");
    this->areaItems = loadMasterData<AreaItem>(jsons, "areaItems");
    this->areas = loadMasterData<Area>(jsons, "areas");
    this->cardEpisodes = loadMasterData<CardEpisode>(jsons, "cardEpisodes");
    this->cards = loadMasterData<Card>(jsons, "cards");
    this->cardRarities = loadMasterData<CardRarity>(jsons, "cardRarities");
    this->characterRanks = loadMasterData<CharacterRank>(jsons, "characterRanks");
    this->eventCards = loadMasterData<EventCard>(jsons, "eventCards");
    this->eventDeckBonuses = loadMasterData<EventDeckBonus>(jsons, "eventDeckBonuses");
    this->eventExchangeSummaries = loadMasterData<EventExchangeSummary>(jsons, "eventExchangeSummaries");
    this->events = loadMasterData<Event>(jsons, "events");
    this->eventItems = loadMasterData<EventItem>(jsons, "eventItems");
    this->eventRarityBonusRates = loadMasterData<EventRarityBonusRate>(jsons, "eventRarityBonusRates");
    this->gameCharacters = loadMasterData<GameCharacter>(jsons, "gameCharacters");
    this->gameCharacterUnits = loadMasterData<GameCharacterUnit>(jsons, "gameCharacterUnits");
    this->honors = loadMasterData<Honor>(jsons, "honors");
    this->masterLessons = loadMasterData<MasterLesson>(jsons, "masterLessons");
    this->musicDifficulties = loadMasterData<MusicDifficulty>(jsons, "musicDifficulties");
    this->musics = loadMasterData<Music>(jsons, "musics");
    this->musicVocals = loadMasterData<MusicVocal>(jsons, "musicVocals");
    this->shopItems = loadMasterData<ShopItem>(jsons, "shopItems");
    this->skills = loadMasterData<Skill>(jsons, "skills");
    this->worldBloomDifferentAttributeBonuses = loadMasterData<WorldBloomDifferentAttributeBonus>(jsons, "worldBloomDifferentAttributeBonuses");
    this->worldBlooms = loadMasterData<WorldBloom>(jsons, "worldBlooms");

    this->worldBloomSupportDeckUnitEventLimitedBonuses = loadMasterData<WorldBloomSupportDeckUnitEventLimitedBonus>(jsons, "worldBloomSupportDeckUnitEventLimitedBonuses", false);
    this->cardMysekaiCanvasBonuses = loadMasterData<CardMysekaiCanvasBonus>(jsons, "cardMysekaiCanvasBonuses", false);
    this->mysekaiFixtureGameCharacterGroups = loadMasterData<MysekaiFixtureGameCharacterGroup>(jsons, "mysekaiFixtureGameCharacterGroups", false);
    this->mysekaiFixtureGameCharacterGroupPerformanceBonuses = loadMasterData<MysekaiFixtureGameCharacterGroupPerformanceBonus>(jsons, "mysekaiFixtureGameCharacterGroupPerformanceBonuses", false);
    this->mysekaiGates = loadMasterData<MysekaiGate>(jsons, "mysekaiGates", false);
    this->mysekaiGateLevels = loadMasterData<MysekaiGateLevel>(jsons, "mysekaiGateLevels", false);

    std::map<std::string, json> tmp{};
    loadMasterDataJsonFromFile(tmp, getStaticDataDir(), "worldBloomSupportDeckBonusesWL1");
    loadMasterDataJsonFromFile(tmp, getStaticDataDir(), "worldBloomSupportDeckBonusesWL2");
    loadMasterDataJsonFromFile(tmp, getStaticDataDir(), "worldBloomSupportDeckBonusesWL3");
    this->worldBloomSupportDeckBonusesWL1 = loadMasterData<WorldBloomSupportDeckBonus>(tmp, "worldBloomSupportDeckBonusesWL1");
    this->worldBloomSupportDeckBonusesWL2 = loadMasterData<WorldBloomSupportDeckBonus>(tmp, "worldBloomSupportDeckBonusesWL2");
    this->worldBloomSupportDeckBonusesWL3 = loadMasterData<WorldBloomSupportDeckBonus>(tmp, "worldBloomSupportDeckBonusesWL3");

    addFakeEvent(Enums::EventType::world_bloom);
    addFakeEvent(Enums::EventType::marathon);
    addFakeEvent(Enums::EventType::cheerful);
    addFinalChapterEventIfNeeded(*this);
}

void MasterData::loadFromFiles(const std::string& baseDir) {
    this->baseDir = baseDir;
    std::map<std::string, json> jsons;
    for (const auto& key : requiredMasterDataKeys) 
        loadMasterDataJsonFromFile(jsons, baseDir, key);
    for (const auto& key : notRequiredMasterDataKeys) 
        loadMasterDataJsonFromFile(jsons, baseDir, key);
    loadFromJsons(jsons);
}

void MasterData::loadFromStrings(std::map<std::string, std::string>& data) {
    this->baseDir.clear();
    std::map<std::string, json> jsons;
    for (const auto& key : requiredMasterDataKeys) 
        loadMasterDataJsonFromStrings(jsons, data, key);
    for (const auto& key : notRequiredMasterDataKeys)
        loadMasterDataJsonFromStrings(jsons, data, key);
    loadFromJsons(jsons);
}


// 添加用于无活动组卡和指定团+颜色组卡的假活动
void MasterData::addFakeEvent(int eventType) {
    if (eventType == Enums::EventType::world_bloom) {
        // 模拟WL组卡
        for (int turn = 1; turn <= 3; turn++) {
            std::vector<int> fakeGroups = turn == 3
                ? std::vector<int>{1, 2, 3, 4, 5}
                : std::vector<int>(Enums::Unit::specificUnits.begin(), Enums::Unit::specificUnits.end());
            for (auto group : fakeGroups) {
                // 活动本身
                Event e;
                e.id = getWorldBloomFakeEventId(turn, group);
                e.eventType = eventType;
                events.push_back(e);
                std::set<int> charas{};
                // 相同团的角色加成
                for (auto& charaUnit : gameCharacterUnits) {
                    bool inGroup = false;
                    if (turn == 3) {
                        inGroup = std::find(
                            worldBloom3PartCharacterIds[group - 1].begin(),
                            worldBloom3PartCharacterIds[group - 1].end(),
                            charaUnit.gameCharacterId
                        ) != worldBloom3PartCharacterIds[group - 1].end();
                    } else {
                        inGroup = (charaUnit.unit == group && charaUnit.id <= 20)
                               || (group == Enums::Unit::piapro && charaUnit.id > 20);
                    }
                    if (inGroup) {
                        EventDeckBonus b;
                        b.eventId = e.id;
                        b.gameCharacterUnitId = charaUnit.id;
                        b.cardAttr = Enums::Attr::null;
                        b.bonusRate = 25.0;
                        eventDeckBonuses.push_back(b);
                        if (charaUnit.id <= 26)
                            charas.insert(charaUnit.id);
                    }
                }
                // WL章节
                int chapterNo = 0;
                for (auto chara : charas) {
                    WorldBloom wb;
                    wb.eventId = e.id;
                    wb.gameCharacterId = chara;
                    wb.chapterNo = ++chapterNo;
                    worldBlooms.push_back(wb);
                }
                // 提取前几轮wl的加成卡（作为后面wl后排的额外加成卡）
                if (turn >= 2) {
                    auto newBonuses = buildFakeWorldBloomSupportDeckUnitEventLimitedBonuses(*this, turn, e.id, charas);
                    worldBloomSupportDeckUnitEventLimitedBonuses.insert(
                        worldBloomSupportDeckUnitEventLimitedBonuses.end(),
                        newBonuses.begin(),
                        newBonuses.end()
                    );
                }
            }
        }
    }
    else {
        // 无活动组卡
        Event noEvent;
        noEvent.id = getNoEventFakeEventId(eventType);
        noEvent.eventType = eventType;
        events.push_back(noEvent);

        // 指定团名+指定颜色组卡
        for (auto unit : Enums::Unit::specificUnits) {
            for (auto attr : Enums::Attr::specificAttrs) {
                Event e;
                e.id = getUnitAttrFakeEventId(eventType, unit, attr);
                e.eventType = eventType;
                events.push_back(e);
                // 相同团的角色加成
                for (auto& charaUnit : gameCharacterUnits) {
                    if (charaUnit.unit == unit || (unit == Enums::Unit::piapro && charaUnit.id > 20)) {
                        // 同团同色
                        EventDeckBonus b;
                        b.eventId = e.id;
                        b.gameCharacterUnitId = charaUnit.id;
                        b.cardAttr = attr;
                        b.bonusRate = 50.0;
                        eventDeckBonuses.push_back(b);
                        // 同团不同色
                        EventDeckBonus b2;
                        b2.eventId = e.id;
                        b2.gameCharacterUnitId = charaUnit.id;
                        b2.cardAttr = Enums::Attr::null;
                        b2.bonusRate = 25.0;
                        eventDeckBonuses.push_back(b2);
                    }
                }
                // 不同团同色加成
                EventDeckBonus b;
                b.eventId = e.id;
                b.gameCharacterUnitId = 0;
                b.cardAttr = attr;
                b.bonusRate = 25.0;
                eventDeckBonuses.push_back(b);
            }
        }
    }
}

int MasterData::getNoEventFakeEventId(int eventType) const
{
    if (eventType == Enums::EventType::world_bloom) {
        throw std::invalid_argument("Not supported event type for fake event");
    }
    return 2000000 + eventType * 100000;
}

int MasterData::getUnitAttrFakeEventId(int eventType, int unit, int attr) const
{
    if (eventType == Enums::EventType::world_bloom) {
        throw std::invalid_argument("Not supported event type for fake event");
    }
    return 1000000 + unit * 100 + attr + eventType * 100000;
}

int MasterData::getWorldBloomFakeEventId(int worldBloomTurn, int unit) const
{
    if (worldBloomTurn < 1 || worldBloomTurn > 3) {
        throw std::invalid_argument("Invalid world bloom turn: " + std::to_string(worldBloomTurn));
    }
    return 3000000 + (worldBloomTurn - 1) * 100000 + unit;
}
// 映射角色id
int MasterData::getWorldBloom3PartByCharacterId(int characterId) const
{
    for (int i = 0; i < (int)worldBloom3PartCharacterIds.size(); i++) {
        if (std::find(
            worldBloom3PartCharacterIds[i].begin(),
            worldBloom3PartCharacterIds[i].end(),
            characterId
        ) != worldBloom3PartCharacterIds[i].end()) {
            return i + 1;
        }
    }
    throw std::invalid_argument("Character is not in any world bloom 3 part: " + std::to_string(characterId));
}

int MasterData::getWorldBloomEventTurn(int eventId) const
{
    if (eventId > 1000) 
        return (eventId / 100000) % 10 + 1;
    else if (eventId <= 140)
        return 1;  // 140之前为第一轮
    else if (eventId <= 180)
        return 2;
    else
        return 3;
}



