#include "event-point/card-event-calculator.h"

#include <algorithm>

double CardEventCalculator::getEventDeckBonus(int eventId, const Card& card)
{
    auto& eventDeckBonuses = this->dataProvider.masterData->eventDeckBonuses;
    auto& gameCharacterUnits = this->dataProvider.masterData->gameCharacterUnits;
    double maxBonus = 0;

    for (const auto& it : eventDeckBonuses) {
        if (it.eventId != eventId) {
            continue;
        }
        if (it.cardAttr != Enums::Attr::null && it.cardAttr != card.attr) {
            continue;
        }

        if (it.gameCharacterUnitId == 0) {
            maxBonus = std::max(maxBonus, it.bonusRate);
            continue;
        }

        auto unit = findOrThrow(
            gameCharacterUnits,
            [it](const GameCharacterUnit& a) { return a.id == it.gameCharacterUnitId; },
            [&]() { return "Game character unit not found for gameCharacterUnitId=" + std::to_string(it.gameCharacterUnitId); }
        );
        if (unit.gameCharacterId != card.characterId) {
            continue;
        }

        if (card.characterId < 21
         || card.supportUnit == unit.unit
         || card.supportUnit == Enums::Unit::none) {
            maxBonus = std::max(maxBonus, it.bonusRate);
        }
    }

    return maxBonus;
}

static double getCustomEventDeckBonus(
    const Card& card,
    const std::optional<std::vector<int>>& customBonusCharacterIds,
    const std::optional<int>& customBonusAttr,
    const std::optional<std::unordered_map<int, int>>& customBonusSupportUnits
) {
    if (!customBonusCharacterIds.has_value() && !customBonusAttr.has_value()) {
        return 0.0;
    }

    bool inCharacterList = false;
    if (customBonusCharacterIds.has_value()) {
        const auto& ids = customBonusCharacterIds.value();
        inCharacterList = std::find(ids.begin(), ids.end(), card.characterId) != ids.end();
    }

    bool supportUnitMatched = true;
    if (inCharacterList && card.characterId >= 21 && customBonusSupportUnits.has_value()) {
        const auto& supportUnits = customBonusSupportUnits.value();
        auto it = supportUnits.find(card.characterId);
        if (it != supportUnits.end()) {
            int expectedSupportUnit = it->second;
            supportUnitMatched = card.supportUnit == expectedSupportUnit || card.supportUnit == Enums::Unit::none;
        }
    }

    const bool characterMatched = inCharacterList && supportUnitMatched;
    const bool attrMatched = customBonusAttr.has_value() && card.attr == customBonusAttr.value();

    if (characterMatched && attrMatched) {
        return 50.0;
    }
    if (characterMatched || attrMatched) {
        return 25.0;
    }
    return 0.0;
}

CardEventBonusInfo CardEventCalculator::getCardEventBonus(
    const UserCard& userCard,
    int eventId,
    const std::optional<std::vector<int>>& customBonusCharacterIds,
    const std::optional<int>& customBonusAttr,
    const std::optional<std::unordered_map<int, int>>& customBonusSupportUnits
)
{
    auto& cards = this->dataProvider.masterData->cards;
    auto card = findOrThrow(
        cards,
        [&](const Card& it) { return it.id == userCard.cardId; },
        [&]() { return "Card not found for cardId=" + std::to_string(userCard.cardId); }
    );
    auto& eventCards = this->dataProvider.masterData->eventCards;
    auto& eventRarityBonusRates = this->dataProvider.masterData->eventRarityBonusRates;

    if (eventId == this->dataProvider.masterData->getNoEventFakeEventId(Enums::EventType::marathon)
     || eventId == this->dataProvider.masterData->getNoEventFakeEventId(Enums::EventType::cheerful)) {
        if (!customBonusCharacterIds.has_value() && !customBonusAttr.has_value()) {
            return {};
        }
    }

    double basicBonus = this->getEventDeckBonus(eventId, card);
    basicBonus += getCustomEventDeckBonus(card, customBonusCharacterIds, customBonusAttr, customBonusSupportUnits);

    auto masterRankBonus = findOrThrow(
        eventRarityBonusRates,
        [&](const EventRarityBonusRate& it) {
            return it.cardRarityType == card.cardRarityType && it.masterRank == userCard.masterRank;
        },
        [&]() {
            return "Event Rarity Bonus Rate not found for cardRarityType="
                + std::to_string(card.cardRarityType)
                + " masterRank="
                + std::to_string(userCard.masterRank);
        }
    );
    basicBonus += masterRankBonus.bonusRate;

    double limitedBonus = 0.0;
    for (const auto& it : eventCards) {
        if (it.eventId == eventId && it.cardId == card.id) {
            limitedBonus = it.bonusRate;
            break;
        }
    }

    if (eventId == finalChapterEventId) {
        double leaderHonorBonus = this->dataProvider.userData->userCharacterFinalChapterHonorEventBonusMap[card.characterId];
        double leaderLimitBonus = 0.0;
        for (const auto& it : eventCards) {
            if (it.eventId == eventId && it.cardId == card.id) {
                leaderLimitBonus = 20.0;
                break;
            }
        }
        return CardEventBonusInfo{
            .maxBonus = basicBonus + limitedBonus + leaderHonorBonus + leaderLimitBonus,
            .minBonus = basicBonus,
            .limitedBonus = limitedBonus,
            .leaderHonorBonus = leaderHonorBonus,
            .leaderLimitBonus = leaderLimitBonus,
        };
    }

    return CardEventBonusInfo{
        .maxBonus = basicBonus + limitedBonus,
        .minBonus = basicBonus + limitedBonus,
        .limitedBonus = limitedBonus,
        .leaderHonorBonus = 0.0,
        .leaderLimitBonus = 0.0,
    };
}
