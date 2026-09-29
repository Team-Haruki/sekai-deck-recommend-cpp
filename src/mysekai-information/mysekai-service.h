#ifndef MYSEKAI_SERVICE_H
#define MYSEKAI_SERVICE_H

#include "data-provider/data-provider.h"
#include <set>


struct MysekaiGateBonus {
    int gateId;
    // 大门对应的组合；shuffle类型大门（如JP的6号门）为none
    int unit;
    int level;
    // 没有对应等级行时为0（客户端同样按0处理，而不是报错）
    double powerBonusRate;
};


class MySekaiService {

    DataProvider dataProvider;

public:

    MySekaiService(DataProvider dataProvider) : dataProvider(dataProvider) {}

    /**
     * 获得带有自定义世界画布加成的卡牌
     * 看上去一个卡牌只有加成和不加成两种状态，直接返回卡牌ID列表
     * 计算逻辑：根据稀有度确定固定加成，不享受区域道具、角色等级加成，享受家具、大门加成
     */
    std::unordered_set<int> getMysekaiCanvasBonusCards();

    /**
     * 获得自定义世界的家具加成
     * 很贴心地已经由服务器算好了，直接返回就行
     * 计算逻辑：totalBonusRate的单位看上去是0.1%
     */
    std::vector<UserMysekaiFixtureGameCharacterPerformanceBonus> getMysekaiFixtureBonuses();

    /**
     * 获得用户所有大门的加成（按用户数据顺序）
     * 缺少大门或等级定义时加成为0，不抛异常
     */
    std::vector<MysekaiGateBonus> getMysekaiGateBonuses();

};


#endif  // MYSEKAI_SERVICE_H