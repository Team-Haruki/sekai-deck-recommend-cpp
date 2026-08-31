#include "data-provider/master-data.h"
#include "data-provider/static-data.h"

#include <algorithm>
#include <cctype>
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
    "eventCardBonusLimits",
    "eventHonorBonuses",
    "eventMysekaiFixtureGameCharacterPerformanceBonusLimits",
    "eventSkillScoreUpLimits",
    "ingameCombos",
    "ingameNotes",
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

static std::uint64_t makeEventCardKey(int eventId, int cardId)
{
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(eventId)) << 32)
         | static_cast<std::uint32_t>(cardId);
}


void loadMasterDataJsonFromFile(std::map<std::string, json_doc>& jsons, const std::string& baseDir, const std::string& key) {
    try {
        std::string filePath = baseDir + "/" + key + ".json";
        jsons[key] = json_doc::parseFile(filePath, "master data file: " + key);
    }
    catch (const JsonFileOpenError&) {
        jsons.erase(key);
    }
    catch (const std::exception& e) {
        throw std::runtime_error("Failed to load master data from file: " + key + ", error: " + e.what());
    }
}

void loadMasterDataJsonFromStrings(std::map<std::string, json_doc>& jsons, std::map<std::string, std::string>& data, const std::string& key) {
    try {
        if (!data.count(key)) {
            jsons.erase(key);
            return;
        }
        jsons[key] = json_doc::parse(data.at(key), "master data string: " + key);
    }
    catch (const std::exception& e) {
        throw std::runtime_error("Failed to load master data from string: " + key + ", error: " + e.what());
    }
}


void addLegacyWorldBloom2FinaleIfNeeded(MasterData& md) {
    bool hasFinalChapterEvent = false;
    for (const auto& e : md.events) {
        if (e.id == legacyWorldBloom2FinaleEventId) {
            hasFinalChapterEvent = true;
            break;
        }
    }
    if (!hasFinalChapterEvent) {
        // 活动本身
        Event event;
        event.id = legacyWorldBloom2FinaleEventId;
        event.eventType = Enums::EventType::world_bloom;
        md.events.push_back(event);

        // 角色加成
        for (auto& gameCharacterUnit : md.gameCharacterUnits) {
            EventDeckBonus bonus;
            bonus.eventId = legacyWorldBloom2FinaleEventId;
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
                newEventCard.eventId = legacyWorldBloom2FinaleEventId;
                newEventCard.bonusRate = 25.0;
                newEventCards.push_back(newEventCard);
            }
        }
        md.eventCards.insert(md.eventCards.end(), newEventCards.begin(), newEventCards.end());

        // 支援里的wl1限定卡牌加成
        std::vector<WorldBloomSupportDeckUnitEventLimitedBonus> newLimitedBonuses{};
        for (const auto& limitedBonus : md.worldBloomSupportDeckUnitEventLimitedBonuses) {
            auto newLimitBonus = limitedBonus;
            newLimitBonus.eventId = legacyWorldBloom2FinaleEventId;
            newLimitedBonuses.push_back(newLimitBonus);
        }
        md.worldBloomSupportDeckUnitEventLimitedBonuses.insert(
            md.worldBloomSupportDeckUnitEventLimitedBonuses.end(),
            newLimitedBonuses.begin(),
            newLimitedBonuses.end()
        );
    }

    for (const auto& worldBloom : md.worldBlooms) {
        if (worldBloom.eventId == legacyWorldBloom2FinaleEventId
         && worldBloom.worldBloomChapterType == "finale") {
            return;
        }
    }

    WorldBloom worldBloom;
    worldBloom.id = legacyWorldBloom2FinaleEventId * 100 + 1;
    worldBloom.eventId = legacyWorldBloom2FinaleEventId;
    worldBloom.worldBloomChapterType = "finale";
    worldBloom.chapterNo = 1;
    md.worldBlooms.push_back(worldBloom);
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
            if (bonus.eventId != legacyWorldBloom2FinaleEventId
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
            if (eventCard.eventId == legacyWorldBloom2FinaleEventId
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

static bool hasWorldBloomFinaleChapter(const MasterData& md, int eventId) {
    return std::any_of(md.worldBlooms.begin(), md.worldBlooms.end(), [&](const WorldBloom& worldBloom) {
        return worldBloom.eventId == eventId && worldBloom.worldBloomChapterType == "finale";
    });
}

static bool isTop1000WorldBloomHonor(const Honor& honor) {
    const std::string rankPrefix = "honor_top_";
    if (!honor.assetbundleName.starts_with(rankPrefix)) {
        return false;
    }
    const auto rankEnd = honor.assetbundleName.find("_event_wl_", rankPrefix.size());
    if (rankEnd == std::string::npos) {
        return false;
    }
    const auto rankText = honor.assetbundleName.substr(rankPrefix.size(), rankEnd - rankPrefix.size());
    if (rankText.empty()
     || !std::all_of(rankText.begin(), rankText.end(), [](unsigned char ch) { return std::isdigit(ch); })) {
        return false;
    }
    const int rank = std::stoi(rankText);
    return rank > 0 && rank <= 1000;
}

static void addFakeWorldBloomFinale(MasterData& md, int turn) {
    const int fakeEventId = md.getWorldBloomFakeFinaleEventId(turn);
    if (std::any_of(md.events.begin(), md.events.end(), [&](const Event& event) {
        return event.id == fakeEventId;
    })) {
        return;
    }

    std::set<int> sourceEventIds{};
    for (const auto& event : md.events) {
        if (event.id >= 1000
         || event.eventType != Enums::EventType::world_bloom
         || md.getWorldBloomEventTurn(event.id) != turn
         || hasWorldBloomFinaleChapter(md, event.id)) {
            continue;
        }
        sourceEventIds.insert(event.id);
    }

    Event event;
    event.id = fakeEventId;
    event.eventType = Enums::EventType::world_bloom;
    md.events.push_back(event);

    std::set<int> allCharacters{};
    for (const auto& gameCharacterUnit : md.gameCharacterUnits) {
        EventDeckBonus bonus;
        bonus.eventId = fakeEventId;
        bonus.gameCharacterUnitId = gameCharacterUnit.id;
        bonus.bonusRate = 5.0;
        bonus.cardAttr = Enums::Attr::null;
        md.eventDeckBonuses.push_back(bonus);
        if (gameCharacterUnit.gameCharacterId >= 1 && gameCharacterUnit.gameCharacterId <= 26) {
            allCharacters.insert(gameCharacterUnit.gameCharacterId);
        }
    }

    std::set<int> copiedCardIds{};
    std::vector<EventCard> finaleEventCards{};
    for (const auto& eventCard : md.eventCards) {
        if (!sourceEventIds.count(eventCard.eventId)
         || eventCard.bonusRate <= 0
         || !copiedCardIds.insert(eventCard.cardId).second) {
            continue;
        }
        auto finaleEventCard = eventCard;
        finaleEventCard.eventId = fakeEventId;
        finaleEventCard.bonusRate = 25.0;
        finaleEventCard.leaderBonusRate = 20.0;
        finaleEventCards.push_back(finaleEventCard);
    }
    md.eventCards.insert(md.eventCards.end(), finaleEventCards.begin(), finaleEventCards.end());

    auto supportBonuses = buildFakeWorldBloomSupportDeckUnitEventLimitedBonuses(
        md,
        turn,
        fakeEventId,
        allCharacters
    );
    md.worldBloomSupportDeckUnitEventLimitedBonuses.insert(
        md.worldBloomSupportDeckUnitEventLimitedBonuses.end(),
        supportBonuses.begin(),
        supportBonuses.end()
    );

    if (turn == 3) {
        const std::string prefix = "wl_3rd_part";
        for (const auto& honor : md.honors) {
            if (!isTop1000WorldBloomHonor(honor)) {
                continue;
            }
            auto partStart = honor.assetbundleName.find(prefix);
            if (partStart == std::string::npos) {
                continue;
            }
            partStart += prefix.size();
            auto chapterMarker = honor.assetbundleName.find("_cp", partStart);
            if (chapterMarker == std::string::npos) {
                continue;
            }
            const auto partText = honor.assetbundleName.substr(partStart, chapterMarker - partStart);
            auto chapterEnd = chapterMarker + 3;
            while (chapterEnd < honor.assetbundleName.size()
                && std::isdigit(static_cast<unsigned char>(honor.assetbundleName[chapterEnd]))) {
                ++chapterEnd;
            }
            const auto chapterText = honor.assetbundleName.substr(chapterMarker + 3, chapterEnd - chapterMarker - 3);
            if (partText.empty() || chapterText.empty()
             || !std::all_of(partText.begin(), partText.end(), [](unsigned char ch) { return std::isdigit(ch); })) {
                continue;
            }
            int part = std::stoi(partText);
            int chapter = std::stoi(chapterText);
            for (const auto& worldBloom : md.worldBlooms) {
                if (!sourceEventIds.count(worldBloom.eventId)
                 || worldBloom.chapterNo != chapter
                 || md.getWorldBloom3PartByCharacterId(worldBloom.gameCharacterId) != part) {
                    continue;
                }
                md.eventHonorBonuses.push_back(EventHonorBonus{
                    .id = 0,
                    .eventId = fakeEventId,
                    .honorId = honor.id,
                    .leaderGameCharacterId = worldBloom.gameCharacterId,
                    .bonusRate = 50.0,
                });
                break;
            }
        }
    }

    WorldBloom finale;
    finale.id = fakeEventId * 100 + 1;
    finale.eventId = fakeEventId;
    finale.worldBloomChapterType = "finale";
    finale.chapterNo = 1;
    md.worldBlooms.push_back(finale);
}


template <typename T>
std::vector<T> loadMasterData(std::map<std::string, json_doc>& jsons, const std::string& key, bool required = true) {
    if (!jsons.count(key)) {
        if (required) {
            throw std::runtime_error("master data key not found: " + key);
        } else {
            std::cerr << "[sekai-deck-recommend-cpp] warning: master data key not found: " + key << std::endl;
            return {};
        }
    }
    return T::fromJsonList(jsons.at(key).root());
}

void MasterData::loadFromJsons(std::map<std::string, json_doc>& jsons) {
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
    this->eventCardBonusLimits = loadMasterData<EventCardBonusLimit>(jsons, "eventCardBonusLimits", false);
    this->eventHonorBonuses = loadMasterData<EventHonorBonus>(jsons, "eventHonorBonuses", false);
    this->eventMysekaiFixtureGameCharacterPerformanceBonusLimits = loadMasterData<EventMysekaiFixtureGameCharacterPerformanceBonusLimit>(jsons, "eventMysekaiFixtureGameCharacterPerformanceBonusLimits", false);
    this->eventSkillScoreUpLimits = loadMasterData<EventSkillScoreUpLimit>(jsons, "eventSkillScoreUpLimits", false);
    this->ingameCombos = loadMasterData<IngameCombo>(jsons, "ingameCombos", false);
    this->ingameNotes = loadMasterData<IngameNote>(jsons, "ingameNotes", false);
    this->mysekaiFixtureGameCharacterGroups = loadMasterData<MysekaiFixtureGameCharacterGroup>(jsons, "mysekaiFixtureGameCharacterGroups", false);
    this->mysekaiFixtureGameCharacterGroupPerformanceBonuses = loadMasterData<MysekaiFixtureGameCharacterGroupPerformanceBonus>(jsons, "mysekaiFixtureGameCharacterGroupPerformanceBonuses", false);
    this->mysekaiGates = loadMasterData<MysekaiGate>(jsons, "mysekaiGates", false);
    this->mysekaiGateLevels = loadMasterData<MysekaiGateLevel>(jsons, "mysekaiGateLevels", false);

    std::map<std::string, json_doc> tmp{};
    loadMasterDataJsonFromFile(tmp, getStaticDataDir(), "worldBloomSupportDeckBonusesWL1");
    loadMasterDataJsonFromFile(tmp, getStaticDataDir(), "worldBloomSupportDeckBonusesWL2");
    loadMasterDataJsonFromFile(tmp, getStaticDataDir(), "worldBloomSupportDeckBonusesWL3");
    this->worldBloomSupportDeckBonusesWL1 = loadMasterData<WorldBloomSupportDeckBonus>(tmp, "worldBloomSupportDeckBonusesWL1");
    this->worldBloomSupportDeckBonusesWL2 = loadMasterData<WorldBloomSupportDeckBonus>(tmp, "worldBloomSupportDeckBonusesWL2");
    this->worldBloomSupportDeckBonusesWL3 = loadMasterData<WorldBloomSupportDeckBonus>(tmp, "worldBloomSupportDeckBonusesWL3");

    addFakeEvent(Enums::EventType::world_bloom);
    addFakeEvent(Enums::EventType::marathon);
    addFakeEvent(Enums::EventType::cheerful);
    addLegacyWorldBloom2FinaleIfNeeded(*this);
    addFakeWorldBloomFinale(*this, 3);
    buildDerivedCaches();
}

void MasterData::buildDerivedCaches() {
    this->worldBloomFinaleEventIds.clear();
    for (const auto& worldBloom : worldBlooms) {
        if (worldBloom.worldBloomChapterType == "finale") {
            this->worldBloomFinaleEventIds.insert(worldBloom.eventId);
        }
    }
    this->eventCardBonusCountLimits.clear();
    for (const auto& limit : eventCardBonusLimits) {
        this->eventCardBonusCountLimits.emplace(limit.eventId, limit.memberCountLimit);
    }
    this->honorIndexById.clear();
    for (int i = 0; i < (int)honors.size(); ++i) {
        this->honorIndexById.emplace(honors[i].id, i);
    }
    this->eventCardIndexByKey.clear();
    this->eventCardIndexByKey.reserve(eventCards.size());
    for (int i = 0; i < (int)eventCards.size(); ++i) {
        this->eventCardIndexByKey.emplace(makeEventCardKey(eventCards[i].eventId, eventCards[i].cardId), i);
    }
    this->eventDeckBonusIndicesByEventId.clear();
    for (int i = 0; i < (int)eventDeckBonuses.size(); ++i) {
        this->eventDeckBonusIndicesByEventId[eventDeckBonuses[i].eventId].push_back(i);
    }
    this->gameCharacterUnitIndexById.clear();
    for (int i = 0; i < (int)gameCharacterUnits.size(); ++i) {
        this->gameCharacterUnitIndexById.emplace(gameCharacterUnits[i].id, i);
    }
    this->cardEpisodeIndexById.clear();
    this->cardEpisodeIndexById.reserve(cardEpisodes.size());
    for (int i = 0; i < (int)cardEpisodes.size(); ++i) {
        this->cardEpisodeIndexById.emplace(cardEpisodes[i].id, i);
    }
    this->cardIndexById.clear();
    for (int i = 0; i < (int)cards.size(); ++i) {
        this->cardIndexById.emplace(cards[i].id, i);
    }
    this->skillIndexById.clear();
    for (int i = 0; i < (int)skills.size(); ++i) {
        this->skillIndexById.emplace(skills[i].id, i);
    }
    this->characterRankIndexByKey.clear();
    for (int i = 0; i < (int)characterRanks.size(); ++i) {
        this->characterRankIndexByKey.emplace(
            (long long)characterRanks[i].characterId * 100000 + characterRanks[i].characterRank, i);
    }
}

const CardEpisode* MasterData::findCardEpisodeById(int cardEpisodeId) const
{
    auto it = cardEpisodeIndexById.find(cardEpisodeId);
    return it != cardEpisodeIndexById.end() ? &cardEpisodes[it->second] : nullptr;
}

const Card* MasterData::findCardById(int cardId) const
{
    auto it = cardIndexById.find(cardId);
    return it != cardIndexById.end() ? &cards[it->second] : nullptr;
}

const Skill& MasterData::getSkillById(int skillId) const
{
    auto it = skillIndexById.find(skillId);
    if (it == skillIndexById.end()) {
        throw ElementNoFoundError("Skill not found for skillId=" + std::to_string(skillId));
    }
    return skills[it->second];
}

const CharacterRank& MasterData::getCharacterRank(int characterId, int rank) const
{
    auto it = characterRankIndexByKey.find((long long)characterId * 100000 + rank);
    if (it == characterRankIndexByKey.end()) {
        throw ElementNoFoundError("Character rank not found for characterId=" + std::to_string(characterId) + " rank=" + std::to_string(rank));
    }
    return characterRanks[it->second];
}

const std::vector<int>& MasterData::getEventDeckBonusIndices(int eventId) const
{
    static const std::vector<int> empty{};
    auto it = eventDeckBonusIndicesByEventId.find(eventId);
    return it != eventDeckBonusIndicesByEventId.end() ? it->second : empty;
}

const EventCard* MasterData::findEventCard(int eventId, int cardId) const
{
    auto it = eventCardIndexByKey.find(makeEventCardKey(eventId, cardId));
    return it != eventCardIndexByKey.end() ? &eventCards[it->second] : nullptr;
}

const GameCharacterUnit& MasterData::getGameCharacterUnitById(int gameCharacterUnitId) const
{
    auto it = gameCharacterUnitIndexById.find(gameCharacterUnitId);
    if (it == gameCharacterUnitIndexById.end()) {
        throw ElementNoFoundError("Game character unit not found for gameCharacterUnitId=" + std::to_string(gameCharacterUnitId));
    }
    return gameCharacterUnits[it->second];
}

const Honor& MasterData::getHonorById(int honorId) const
{
    auto it = honorIndexById.find(honorId);
    if (it == honorIndexById.end()) {
        throw ElementNoFoundError("Honor not found for honorId=" + std::to_string(honorId));
    }
    return honors[it->second];
}

void MasterData::loadFromFiles(const std::string& baseDir) {
    this->baseDir = baseDir;
    std::map<std::string, json_doc> jsons;
    for (const auto& key : requiredMasterDataKeys) 
        loadMasterDataJsonFromFile(jsons, baseDir, key);
    for (const auto& key : notRequiredMasterDataKeys) 
        loadMasterDataJsonFromFile(jsons, baseDir, key);
    loadFromJsons(jsons);
}

void MasterData::loadFromStrings(std::map<std::string, std::string>& data) {
    this->baseDir.clear();
    std::map<std::string, json_doc> jsons;
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

int MasterData::getWorldBloomFakeFinaleEventId(int worldBloomTurn) const
{
    if (worldBloomTurn < 2 || worldBloomTurn > 3) {
        throw std::invalid_argument("Invalid world bloom finale turn: " + std::to_string(worldBloomTurn));
    }
    return getWorldBloomFakeEventId(worldBloomTurn, 0);
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

bool MasterData::isWorldBloomFinale(int eventId) const
{
    return worldBloomFinaleEventIds.count(eventId) > 0;
}

bool MasterData::isWorldBloomFakeFinale(int eventId) const
{
    return eventId == getWorldBloomFakeFinaleEventId(2)
        || eventId == getWorldBloomFakeFinaleEventId(3);
}

int MasterData::getEventCardBonusCountLimit(int eventId) const
{
    auto it = eventCardBonusCountLimits.find(eventId);
    if (it != eventCardBonusCountLimits.end()) {
        return it->second;
    }
    if (eventId == legacyWorldBloom2FinaleEventId || isWorldBloomFakeFinale(eventId)) {
        return legacyWorldBloom2FinaleCardBonusCountLimit;
    }
    if (isWorldBloomFinale(eventId)) {
        throw std::runtime_error("Event card bonus count limit not found for world bloom finale eventId=" + std::to_string(eventId));
    }
    return 5;
}

std::optional<double> MasterData::getEventSkillScoreUpLimit(int eventId) const
{
    if (eventId == legacyWorldBloom2FinaleEventId || isWorldBloomFakeFinale(eventId)) {
        return legacyWorldBloom2FinaleSkillScoreUpLimit;
    }
    for (const auto& limit : eventSkillScoreUpLimits) {
        if (limit.eventId == eventId) {
            return std::max(0.0, limit.scoreUpRateLimit - 100.0);
        }
    }
    if (isWorldBloomFinale(eventId)) {
        throw std::runtime_error("Event skill score up limit not found for world bloom finale eventId=" + std::to_string(eventId));
    }
    return std::nullopt;
}

std::optional<int> MasterData::getMysekaiFixtureBonusLimit(int eventId) const
{
    for (const auto& limit : eventMysekaiFixtureGameCharacterPerformanceBonusLimits) {
        if (limit.eventId == eventId) {
            return limit.bonusRateLimit;
        }
    }
    if (eventId == legacyWorldBloom2FinaleEventId || isWorldBloomFakeFinale(eventId)) {
        return legacyWorldBloom2FinaleMysekaiFixtureBonusLimit;
    }
    if (isWorldBloomFinale(eventId)) {
        throw std::runtime_error("MySekai fixture bonus limit not found for world bloom finale eventId=" + std::to_string(eventId));
    }
    return std::nullopt;
}
