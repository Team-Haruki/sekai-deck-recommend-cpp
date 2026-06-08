#ifndef CARD_BLOOM_EVENT_CALCULATOR_H
#define CARD_BLOOM_EVENT_CALCULATOR_H

#include "data-provider/data-provider.h"
#include "card-information/card-service.h"
#include <optional>


class CardBloomEventCalculator {

    DataProvider dataProvider;
    CardService cardService;

public:

    CardBloomEventCalculator(const DataProvider& dataProvider) : 
        dataProvider(dataProvider),
        cardService(dataProvider) {}

    /**
     * 获取单张卡牌的支援加成
     * 默认要求支援卡牌属于对应团队；仅团主卡筛选时可关闭该限制，避免支援池被主卡筛选间接打空
     * 未指定支援角色时返回值为nullopt
     * @param userCard 用户卡牌
     * @param eventId 活动ID
     * @param specialCharacterId 指定的加成角色
     * @param requireSpecialUnitMatch 是否要求支援卡牌属于指定角色对应团队
     */
    std::optional<double> getCardSupportDeckBonus(
        const UserCard& userCard,
        int eventId,
        int specialCharacterId,
        bool requireSpecialUnitMatch = true
    );

};

#endif // CARD_BLOOM_EVENT_CALCULATOR_H
