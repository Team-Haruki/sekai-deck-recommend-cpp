#ifndef USER_DATA_H
#define USER_DATA_H

#include "data-provider/user-data-types.h"
#include <memory>
#include <mutex>

class UserData {

public:
    std::string path;

    UserGameData userGamedata;
    std::vector<UserArea> userAreas;
    std::vector<UserCard> userCards;
    std::vector<UserChallengeLiveSoloDeck> userChallengeLiveSoloDecks;
    std::vector<UserCharacter> userCharacters;
    std::vector<UserDeck> userDecks;
    std::vector<UserHonor> userHonors;
    std::vector<UserMysekaiCanvas> userMysekaiCanvases;
    std::vector<UserMysekaiFixtureGameCharacterPerformanceBonus> userMysekaiFixtureGameCharacterPerformanceBonuses;
    std::vector<UserMysekaiGate> userMysekaiGates;
    std::vector<UserWorldBloomSupportDeck> userWorldBloomSupportDecks;

    // 预处理终章用户哪些角色有称号活动加成，在dataProvider中计算。
    // 多个并发recommend共享同一份UserData，初始化状态必须挂在共享对象上
    //（DataProvider按值拷贝，其inited标志不跨请求共享）并加锁保护
    std::map<int, double> userCharacterFinalChapterHonorEventBonusMap;
    std::shared_ptr<std::mutex> finalChapterHonorInitMutex = std::make_shared<std::mutex>();
    bool finalChapterHonorInited = false;

    void loadFromJson(const json_view& j);

    void loadFromFile(const std::string& path);

    void loadFromString(const std::string& s);
};


#endif // USER_DATA_H
