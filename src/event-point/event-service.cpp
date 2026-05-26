#include "event-point/event-service.h"
#include <set>

int EventService::getEventType(int eventId)
{
    auto& events = this->dataProvider.masterData->events;
    auto& event = findOrThrow(events, [eventId](const Event& it) { 
        return it.id == eventId; 
    }, [&]() { return "Event not found for eventId=" + std::to_string(eventId); });
    return event.eventType;
}

EventConfig EventService::getEventConfig(int eventId, int specialCharacterId)
{
    int eventType = this->getEventType(eventId);
    bool isWorldBloomFinale = this->dataProvider.masterData->isWorldBloomFinale(eventId);
    return {
        .eventId = eventId,
        .eventType = eventType,
        .eventUnit = this->getEventBonusUnit(eventId),
        .specialCharacterId = specialCharacterId,
        .cardBonusCountLimit = this->dataProvider.masterData->getEventCardBonusCountLimit(eventId),
        .skillScoreUpLimit = this->dataProvider.masterData->getEventSkillScoreUpLimit(eventId),
        .mysekaiFixtureLimit = this->dataProvider.masterData->getMysekaiFixtureBonusLimit(eventId),
        .isWorldBloomFinale = isWorldBloomFinale,
        .worldBloomSupportUnit = eventType == Enums::EventType::world_bloom
            ? this->getWorldBloomSupportUnit(specialCharacterId)
            : 0,
    };
}

int EventService::getEventBonusUnit(int eventId)
{
    auto& eventDeckBonuses = this->dataProvider.masterData->eventDeckBonuses;
    auto& gameCharacterUnits = this->dataProvider.masterData->gameCharacterUnits;
    auto& gameCharacters = this->dataProvider.masterData->gameCharacters;
    std::unordered_map<int, int> unitCounts{};
    int bonusCount = 0;
    for (const auto& it : eventDeckBonuses) {
        if (it.eventId == eventId && it.gameCharacterUnitId != 0) {
            auto unit = findOrThrow(gameCharacterUnits, [it](const GameCharacterUnit& a) { 
                return a.id == it.gameCharacterUnitId; 
            }, [&]() { return "Game character unit not found for gameCharacterUnitId=" + std::to_string(it.gameCharacterUnitId); });
            auto gameCharacter = findOrThrow(gameCharacters, [unit](const GameCharacter& a) {
                return a.id == unit.gameCharacterId;
            }, [&]() { return "Game character not found for gameCharacterId=" + std::to_string(unit.gameCharacterId); });
            unitCounts[gameCharacter.unit]++;
            if (gameCharacter.unit != unit.unit) {
                unitCounts[unit.unit]++;
            }
            bonusCount++;
        }
    }
    for (const auto& [unit, count] : unitCounts) {
        if (count == bonusCount) {
            return unit;
        }
    }
    return 0;
}

int EventService::getWorldBloomSupportUnit(int specialCharacterId)
{
    if (specialCharacterId == 0) {
        return 0;
    }
    auto& gameCharacters = this->dataProvider.masterData->gameCharacters;
    auto& gameCharacter = findOrThrow(gameCharacters, [specialCharacterId](const GameCharacter& it) {
        return it.id == specialCharacterId;
    }, [&]() { return "Game character not found for gameCharacterId=" + std::to_string(specialCharacterId); });
    return gameCharacter.unit;
}
