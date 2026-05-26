#include "area-item-recommend/area-item-recommend.h"

#include <algorithm>

int AreaItemRecommend::findCost(const ShopItem& shopItem, const std::string& resourceType, int resourceId) const
{
    for (const auto& cost : shopItem.costs) {
        if (cost.cost.resourceType == resourceType && cost.cost.resourceId == resourceId) {
            return cost.cost.quantity;
        }
    }
    return 0;
}

std::vector<CardDetail> AreaItemRecommend::getCardDetails(
    const std::vector<int>& cardIds,
    const std::vector<AreaItemLevel>& areaItemLevels
)
{
    std::vector<UserCard> userCards{};
    userCards.reserve(cardIds.size());
    for (int cardId : cardIds) {
        userCards.push_back(DeckService(dataProvider).getUserCard(cardId));
    }

    return cardCalculator.batchGetCardDetail(
        userCards,
        {},
        {},
        std::nullopt,
        areaItemLevels
    );
}

int AreaItemRecommend::getDeckPower(
    const std::vector<int>& cardIds,
    const std::vector<AreaItemLevel>& areaItemLevels
)
{
    auto cardDetails = getCardDetails(cardIds, areaItemLevels);
    if (cardDetails.size() != cardIds.size()) {
        throw std::runtime_error("Failed to calculate all requested cards for area item recommendation");
    }

    std::vector<const CardDetail*> deckCards{};
    deckCards.reserve(cardDetails.size());
    for (const auto& card : cardDetails) {
        deckCards.push_back(&card);
    }
    std::map<int, std::vector<SupportDeckCard>> supportCards{};
    auto deckDetails = deckCalculator.getDeckDetailByCards(
        deckCards,
        supportCards,
        deckCalculator.getHonorBonusPower(),
        std::nullopt,
        std::nullopt,
        SkillReferenceChooseStrategy::Average,
        true,
        false
    );
    if (deckDetails.empty()) {
        throw std::runtime_error("Failed to calculate deck power for area item recommendation");
    }
    return deckDetails.front().power.total;
}

std::vector<RecommendAreaItem> AreaItemRecommend::recommendAreaItem(const std::vector<int>& cardIds)
{
    if (cardIds.empty() || cardIds.size() > 5) {
        throw std::invalid_argument("cardIds must contain 1 to 5 cards");
    }

    auto currentAreaItemLevels = areaItemService.getAreaItemLevels();
    int currentPower = getDeckPower(cardIds, currentAreaItemLevels);

    std::vector<RecommendAreaItem> recommend{};
    for (const auto& areaItem : dataProvider.masterData->areaItems) {
        auto currentIt = std::find_if(
            currentAreaItemLevels.begin(),
            currentAreaItemLevels.end(),
            [&](const AreaItemLevel& it) { return it.areaItemId == areaItem.id; }
        );
        std::optional<AreaItemLevel> currentLevel = currentIt == currentAreaItemLevels.end()
            ? std::nullopt
            : std::optional<AreaItemLevel>(*currentIt);
        auto nextLevel = areaItemService.getAreaItemNextLevel(areaItem, currentLevel);
        if (currentLevel.has_value() && nextLevel.level <= currentLevel->level) {
            continue;
        }

        auto newAreaItemLevels = currentAreaItemLevels;
        if (currentIt == currentAreaItemLevels.end()) {
            newAreaItemLevels.push_back(nextLevel);
        } else {
            auto newIt = std::find_if(
                newAreaItemLevels.begin(),
                newAreaItemLevels.end(),
                [&](const AreaItemLevel& it) { return it.areaItemId == areaItem.id; }
            );
            *newIt = nextLevel;
        }

        int power = getDeckPower(cardIds, newAreaItemLevels) - currentPower;
        if (power <= 0) {
            continue;
        }

        auto& area = findOrThrow(dataProvider.masterData->areas, [&](const Area& it) {
            return it.id == areaItem.areaId;
        }, [&]() { return "Area not found for areaId=" + std::to_string(areaItem.areaId); });
        auto shopItem = areaItemService.getShopItem(nextLevel);
        RecommendAreaItemCost cost{
            .coin = findCost(shopItem, "coin", 0),
            .seed = findCost(shopItem, "material", 17),
            .szk = findCost(shopItem, "material", 57),
        };
        recommend.push_back(RecommendAreaItem{
            .areaId = area.id,
            .areaType = area.areaType,
            .areaViewType = area.viewType,
            .areaItemId = areaItem.id,
            .nextLevel = nextLevel.level,
            .shopItemId = shopItem.id,
            .cost = cost,
            .power = power,
            .powerPerCoin = cost.coin > 0 ? double(power) / double(cost.coin) : 0.0,
        });
    }

    std::sort(recommend.begin(), recommend.end(), [](const RecommendAreaItem& a, const RecommendAreaItem& b) {
        return std::tuple(a.powerPerCoin, a.power, -a.areaItemId)
            > std::tuple(b.powerPerCoin, b.power, -b.areaItemId);
    });
    return recommend;
}
