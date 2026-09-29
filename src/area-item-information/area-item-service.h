#ifndef AREA_ITEM_SERVICE_H
#define AREA_ITEM_SERVICE_H

#include "data-provider/data-provider.h"

class AreaItemService {

    DataProvider dataProvider;

public:
    AreaItemService(const DataProvider& dataProvider) : dataProvider(dataProvider) {}

    /**
     * 获取用户纳入计算的区域道具效果
     * 同一道具同一等级可能有多行效果（如7.0.0的56号道具：全角色加成+多组合加成），全部返回
     */
    std::vector<AreaItemLevel> getAreaItemLevels();

    /**
     * 获取对应等级的区域道具的所有效果行（等级超过当前master上限时按上限取）
     * @param areaItemId 区域道具ID
     * @param level 等级
     */
    std::vector<AreaItemLevel> getAreaItemLevel(int areaItemId, int level);

    /**
     * 获取区域道具在当前master中的最高等级
     * @param areaItemId 区域道具ID
     */
    int getMaxAreaItemLevel(int areaItemId);

    /**
     * 获取下一级区域道具的所有效果行
     * @param areaItem 区域道具
     * @param currentLevel （可选）当前等级
     */
    std::vector<AreaItemLevel> getAreaItemNextLevel(const AreaItem& areaItem, std::optional<int> currentLevel = std::nullopt);

    /**
     * 获取区域道具等级对应的ShopItem
     * 按理来说应该先去resourceBoxes中找到道具等级对应的ID，再通过resourceBoxId获取ShopItem
     * 但是为了这么简单的需求获取一个11MB的resourceBoxes纯属想不开，所以用对照resourceBoxes验证过的规律推算
     * @param areaItemId 区域道具ID
     * @param level 等级
     */
    ShopItem getShopItem(int areaItemId, int level);

    /**
     * 区域道具等级对应的ShopItem ID（规律推算，见实现注释）
     */
    static int getShopItemId(int areaItemId, int level);

};

#endif // AREA_ITEM_SERVICE_H
