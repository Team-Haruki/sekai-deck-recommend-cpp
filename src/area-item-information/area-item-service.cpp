#include "area-item-information/area-item-service.h"

#include <algorithm>

std::vector<AreaItemLevel> AreaItemService::getAreaItemLevels()
{
    auto& userAreas = this->dataProvider.userData->userAreas;
    std::vector<AreaItemLevel> areaItemLevels{};
    for (const auto& userArea : userAreas) {
        for (const auto& areaItem : userArea.areaItems) {
            auto rows = this->getAreaItemLevel(areaItem.areaItemId, areaItem.level);
            areaItemLevels.insert(areaItemLevels.end(), rows.begin(), rows.end());
        }
    }
    return areaItemLevels;
}

std::vector<AreaItemLevel> AreaItemService::getAreaItemLevel(int areaItemId, int level)
{
    auto& areaItemLevels = this->dataProvider.masterData->areaItemLevels;
    int clampedLevel = std::min(level, this->getMaxAreaItemLevel(areaItemId));
    // 客户端按(areaItemId, level)取出所有效果行后逐行结算，这里保持master中的顺序
    std::vector<AreaItemLevel> rows{};
    for (const auto& it : areaItemLevels) {
        if (it.areaItemId == areaItemId && it.level == clampedLevel) {
            rows.push_back(it);
        }
    }
    if (rows.empty()) {
        throw ElementNoFoundError("Area item level not found for areaItemId=" + std::to_string(areaItemId) + " level=" + std::to_string(level));
    }
    return rows;
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

std::vector<AreaItemLevel> AreaItemService::getAreaItemNextLevel(const AreaItem &areaItem, std::optional<int> currentLevel)
{
    // 如果没有给定当前等级，就按未购买算；如果已到当前master上限，下个等级仍为上限。
    int maxLevel = this->getMaxAreaItemLevel(areaItem.id);
    int level = currentLevel.has_value() ? std::min(currentLevel.value() + 1, maxLevel) : 1;
    return this->getAreaItemLevel(areaItem.id, level);
}

int AreaItemService::getShopItemId(int areaItemId, int level)
{
    // 对照JP 7.0.0、JP 6.6.0、CN的shopItems/resourceBoxes验证过的规律：
    // - 1-10级：1001起，每个道具10个
    // - 11-15级：1551起，每个道具5个
    // - 16-20级（6.6.0起）：1826起，每个道具5个
    // - 56号道具（7.0.0起）单独编号：2101起
    if (areaItemId == 56)
        return 2100 + level;
    if (level <= 10)
        return 1000 + (areaItemId - 1) * 10 + level;
    if (level <= 15)
        return 1550 + (areaItemId - 1) * 5 + (level - 10);
    return 1825 + (areaItemId - 1) * 5 + (level - 15);
}

ShopItem AreaItemService::getShopItem(int areaItemId, int level)
{
    auto& shopItems = this->dataProvider.masterData->shopItems;
    int id = getShopItemId(areaItemId, level);
    return findOrThrow(shopItems, [&](const ShopItem& it) {
        return it.id == id;
    }, [&]() { return "Shop item not found for areaItemId=" + std::to_string(areaItemId) + " level=" + std::to_string(level) + " shopItemId=" + std::to_string(id); } );
}
