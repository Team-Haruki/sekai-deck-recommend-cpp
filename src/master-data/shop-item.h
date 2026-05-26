#ifndef SHOP_ITEM_H
#define SHOP_ITEM_H

#include "common/collection-utils.h"

struct CommonResource {
    int resourceId = 0;
    std::string resourceType;
    int resourceLevel = 0;
    int quantity = 0;

    static inline CommonResource fromJson(const json_view& item) {
        CommonResource resource;
        resource.resourceId = item.value("resourceId", 0);
        resource.resourceType = item.value("resourceType", "");
        resource.resourceLevel = item.value("resourceLevel", 0);
        resource.quantity = item.value("quantity", 0);
        return resource;
    }
};

struct ShopItemCost {
    int shopItemId = 0;
    int seq = 0;
    CommonResource cost;

    static inline std::vector<ShopItemCost> fromJsonList(const json_view& jsonData) {
        std::vector<ShopItemCost> costs;
        for (const auto& item : jsonData) {
            ShopItemCost cost;
            cost.shopItemId = item.value("shopItemId", 0);
            cost.seq = item.value("seq", 0);
            if (item["cost"].is_object()) {
                cost.cost = CommonResource::fromJson(item["cost"]);
            }
            costs.push_back(cost);
        }
        return costs;
    }
};

struct ShopItem {
    int id = 0;
    int shopId = 0;
    int seq = 0;
    int releaseConditionId = 0;
    int resourceBoxId = 0;
    std::vector<ShopItemCost> costs;

    static inline std::vector<ShopItem> fromJsonList(const json_view& jsonData) {
        std::vector<ShopItem> shopItems;
        for (const auto& item : jsonData) {
            ShopItem shopItem;
            shopItem.id = item.value("id", 0);
            shopItem.shopId = item.value("shopId", 0);
            shopItem.seq = item.value("seq", 0);
            shopItem.releaseConditionId = item.value("releaseConditionId", 0);
            shopItem.resourceBoxId = item.value("resourceBoxId", 0);
            shopItem.costs = ShopItemCost::fromJsonList(item.value("costs", json_view::array()));
            shopItems.push_back(shopItem);
        }
        return shopItems;
    }
};

#endif
