#ifndef USER_DATA_H
#define USER_DATA_H

#include "data-provider/user-data-types.h"
#include <mutex>

class UserData {

public:
    UserData() = default;

    UserData(const UserData& other) {
        std::lock_guard<std::mutex> lock(other.finalChapterHonorInitMutex);
        path = other.path;
        userGamedata = other.userGamedata;
        userAreas = other.userAreas;
        userCards = other.userCards;
        userChallengeLiveSoloDecks = other.userChallengeLiveSoloDecks;
        userCharacters = other.userCharacters;
        userDecks = other.userDecks;
        userHonors = other.userHonors;
        userMysekaiCanvases = other.userMysekaiCanvases;
        userMysekaiFixtureGameCharacterPerformanceBonuses = other.userMysekaiFixtureGameCharacterPerformanceBonuses;
        userMysekaiGates = other.userMysekaiGates;
        userWorldBloomSupportDecks = other.userWorldBloomSupportDecks;
        userCharacterFinalChapterHonorEventBonusMap = other.userCharacterFinalChapterHonorEventBonusMap;
        finalChapterHonorInited = other.finalChapterHonorInited;
    }

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
    mutable std::mutex finalChapterHonorInitMutex;
    bool finalChapterHonorInited = false;

    void loadFromJson(const json_view& j);

    void loadFromFile(const std::string& path);

    void loadFromString(const std::string& s);
};


#endif // USER_DATA_H
