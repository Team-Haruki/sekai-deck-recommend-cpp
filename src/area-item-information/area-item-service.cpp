#include "area-item-information/area-item-service.h"

#include <algorithm>

std::vector<AreaItemLevel> AreaItemService::getAreaItemLevels()
{
    auto& userAreas = this->dataProvider.userData->userAreas;
    std::vector<AreaItemLevel> areaItemLevels{};
    for (const auto& userArea : userAreas) {
        for (const auto& areaItem : userArea.areaItems) {
            areaItemLevels.push_back(this->getAreaItemLevel(areaItem.areaItemId, areaItem.level));
        }
    }
    return areaItemLevels;
}

AreaItemLevel AreaItemService::getAreaItemLevel(int areaItemId, int level)
{
    auto& areaItemLevels = this->dataProvider.masterData->areaItemLevels;
    int clampedLevel = std::min(level, this->getMaxAreaItemLevel(areaItemId));
    return findOrThrow(areaItemLevels, [&](const AreaItemLevel& it) {
        return it.areaItemId == areaItemId && it.level == clampedLevel;
    }, [&]() { return "Area item level not found for areaItemId=" + std::to_string(areaItemId) + " level=" + std::to_string(level); });
}

int AreaItemService::getMaxAreaItemLevel(int areaItemId)
{
    auto& areaItemLevels = this->dataProvider.masterData->areaItemLevels;
    int maxLevel = 0;
    for (const auto& areaItemLevel : areaItemLevels) {
        if (areaItemLevel.areaItemId == areaItemId) {
            maxLevel = std::max(maxLevel, areaItemLevel.level);
        }
    }
    if (maxLevel == 0) {
        throw ElementNoFoundError("Area item levels not found for areaItemId=" + std::to_string(areaItemId));
    }
    return maxLevel;
}

AreaItemLevel AreaItemService::getAreaItemNextLevel(const AreaItem &areaItem, std::optional<AreaItemLevel> areaItemLevel)
{
    // 如果没有给定当前等级，就按未购买算；如果已到当前master上限，下个等级仍为上限。
    int maxLevel = this->getMaxAreaItemLevel(areaItem.id);
    int level = areaItemLevel.has_value() ? std::min(areaItemLevel->level + 1, maxLevel) : 1;
    return this->getAreaItemLevel(areaItem.id, level);
}

ShopItem AreaItemService::getShopItem(const AreaItemLevel &areaItemLevel)
{
    auto& shopItems = this->dataProvider.masterData->shopItems;
    // 目前的规律是1-10级按顺序在1001～1550、11-15级在1551～1825
    int idOffset = areaItemLevel.level <= 10
        ? (1000 + (areaItemLevel.areaItemId - 1) * 10)
        : (1550 - 10 + (areaItemLevel.areaItemId - 1) * 5);
    int id = idOffset + areaItemLevel.level;
    return findOrThrow(shopItems, [&](const ShopItem& it) {
        return it.id == id;
    }, [&]() { return "Shop item not found for areaItemId=" + std::to_string(areaItemLevel.areaItemId) + " level=" + std::to_string(areaItemLevel.level); } );
}
