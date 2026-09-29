#include "mysekai-information/mysekai-service.h"

#include <algorithm>

std::unordered_set<int> MySekaiService::getMysekaiCanvasBonusCards()
{
    auto& userMysekaiCanvas = this->dataProvider.userData->userMysekaiCanvases;
    std::unordered_set<int> result = {};
    for (auto& it : userMysekaiCanvas)
        result.insert(it.cardId);
    return result;
}

std::vector<UserMysekaiFixtureGameCharacterPerformanceBonus> MySekaiService::getMysekaiFixtureBonuses()
{
    return this->dataProvider.userData->userMysekaiFixtureGameCharacterPerformanceBonuses;
}

std::vector<MysekaiGateBonus> MySekaiService::getMysekaiGateBonuses()
{
    auto& userMysekaiGates = this->dataProvider.userData->userMysekaiGates;
    auto& mysekaiGates = this->dataProvider.masterData->mysekaiGates;
    auto& mysekaiGateLevels = this->dataProvider.masterData->mysekaiGateLevels;
    std::vector<MysekaiGateBonus> result = {};
    for (auto& it : userMysekaiGates) {
        // 与客户端CardUtility.GetGateBonus一致：找不到大门或等级行时加成为0。
        // 7.0.0起JP的6号门（mysekaiGateType=shuffle, unit=none）没有任何等级行。
        auto gate = std::find_if(mysekaiGates.begin(), mysekaiGates.end(), [&](const MysekaiGate& g) {
            return g.id == it.mysekaiGateId;
        });
        auto gateLevel = std::find_if(mysekaiGateLevels.begin(), mysekaiGateLevels.end(), [&](const MysekaiGateLevel& l) {
            return l.mysekaiGateId == it.mysekaiGateId && l.level == it.mysekaiGateLevel;
        });
        result.push_back(MysekaiGateBonus{
            it.mysekaiGateId,
            gate != mysekaiGates.end() ? gate->unit : Enums::Unit::none,
            it.mysekaiGateLevel,
            gateLevel != mysekaiGateLevels.end() ? gateLevel->powerBonusRate : 0.0
        });
    }
    return result;
}
