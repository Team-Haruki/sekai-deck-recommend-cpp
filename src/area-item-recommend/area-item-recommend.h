#ifndef AREA_ITEM_RECOMMEND_H
#define AREA_ITEM_RECOMMEND_H

#include "deck-information/deck-calculator.h"
#include "area-item-information/area-item-service.h"

struct RecommendAreaItemCost {
    int coin = 0;
    int seed = 0;
    int szk = 0;
};

struct RecommendAreaItem {
    int areaId = 0;
    int areaType = 0;
    int areaViewType = 0;
    int areaItemId = 0;
    int nextLevel = 0;
    int shopItemId = 0;
    RecommendAreaItemCost cost;
    int power = 0;
    double powerPerCoin = 0.0;
};

class AreaItemRecommend {
    DataProvider dataProvider;
    AreaItemService areaItemService;
    DeckCalculator deckCalculator;
    CardCalculator cardCalculator;

    int findCost(const ShopItem& shopItem, const std::string& resourceType, int resourceId) const;

    std::vector<CardDetail> getCardDetails(
        const std::vector<int>& cardIds,
        const std::vector<AreaItemLevel>& areaItemLevels
    );

    int getDeckPower(
        const std::vector<int>& cardIds,
        const std::vector<AreaItemLevel>& areaItemLevels
    );

public:
    AreaItemRecommend(const DataProvider& dataProvider)
        : dataProvider(dataProvider),
          areaItemService(dataProvider),
          deckCalculator(dataProvider),
          cardCalculator(dataProvider) {}

    /**
     * Recommend the next area item upgrades for a fixed deck, sorted by power gain per coin.
     */
    std::vector<RecommendAreaItem> recommendAreaItem(const std::vector<int>& cardIds);
};

#endif // AREA_ITEM_RECOMMEND_H
