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
    const std::vector<AreaItemLevel>& areaItemLevels,
    MultiUnitBonusEvaluation multiUnitEval
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
        areaItemLevels,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        multiUnitEval
    );
}

int AreaItemRecommend::getDeckPower(
    const std::vector<int>& cardIds,
    const std::vector<AreaItemLevel>& areaItemLevels,
    MultiUnitBonusEvaluation multiUnitEval
)
{
    auto cardDetails = getCardDetails(cardIds, areaItemLevels, multiUnitEval);
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
        false,
        multiUnitEval
    );
    if (deckDetails.empty()) {
        throw std::runtime_error("Failed to calculate deck power for area item recommendation");
    }
    return deckDetails.front().power.total;
}

std::vector<RecommendAreaItem> AreaItemRecommend::recommendAreaItem(
    const std::vector<int>& cardIds,
    MultiUnitBonusEvaluation multiUnitEval
)
{
    if (cardIds.empty() || cardIds.size() > 5) {
        throw std::invalid_argument("cardIds must contain 1 to 5 cards");
    }

    auto currentAreaItemLevels = areaItemService.getAreaItemLevels();
    int currentPower = getDeckPower(cardIds, currentAreaItemLevels, multiUnitEval);

    std::vector<RecommendAreaItem> recommend{};
    for (const auto& areaItem : dataProvider.masterData->areaItems) {
        auto currentIt = std::find_if(
            currentAreaItemLevels.begin(),
            currentAreaItemLevels.end(),
            [&](const AreaItemLevel& it) { return it.areaItemId == areaItem.id; }
        );
        std::optional<int> currentLevel = currentIt == currentAreaItemLevels.end()
            ? std::nullopt
            : std::optional<int>(currentIt->level);
        auto nextLevelRows = areaItemService.getAreaItemNextLevel(areaItem, currentLevel);
        int nextLevel = nextLevelRows.front().level;
        if (currentLevel.has_value() && nextLevel <= currentLevel.value()) {
            continue;
        }

        // 同一道具的所有效果行一起替换为下一级
        std::vector<AreaItemLevel> newAreaItemLevels{};
        newAreaItemLevels.reserve(currentAreaItemLevels.size() + nextLevelRows.size());
        bool inserted = false;
        for (const auto& it : currentAreaItemLevels) {
            if (it.areaItemId != areaItem.id) {
                newAreaItemLevels.push_back(it);
            } else if (!inserted) {
                newAreaItemLevels.insert(newAreaItemLevels.end(), nextLevelRows.begin(), nextLevelRows.end());
                inserted = true;
            }
        }
        if (!inserted) {
            newAreaItemLevels.insert(newAreaItemLevels.end(), nextLevelRows.begin(), nextLevelRows.end());
        }

        int power = getDeckPower(cardIds, newAreaItemLevels, multiUnitEval) - currentPower;
        if (power <= 0) {
            continue;
        }

        auto& area = findOrThrow(dataProvider.masterData->areas, [&](const Area& it) {
            return it.id == areaItem.areaId;
        }, [&]() { return "Area not found for areaId=" + std::to_string(areaItem.areaId); });
        auto shopItem = areaItemService.getShopItem(areaItem.id, nextLevel);
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
            .nextLevel = nextLevel,
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
