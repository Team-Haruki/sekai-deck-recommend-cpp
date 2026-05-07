#ifndef USER_CARD_H
#define USER_CARD_H

#include "common/collection-utils.h"

struct UserCardEpisodes {
    int cardEpisodeId = 0;
    int scenarioStatus = 0;

    static inline std::vector<UserCardEpisodes> fromJsonList(const json& jsonData) {
        std::vector<UserCardEpisodes> episodes;
        for (const auto& item : jsonData) {
            UserCardEpisodes episode;
            if (item.is_array()) {
                episode.cardEpisodeId = item.size() > 0 && item[0].is_number_integer() ? item[0].get<int>() : 0;
                episode.scenarioStatus = mapEnum(EnumMap::scenarioStatus, item.size() > 1 && item[1].is_string() ? item[1].get<std::string>() : "");
            } else {
                episode.cardEpisodeId = item.value("cardEpisodeId", 0);
                episode.scenarioStatus = mapEnum(EnumMap::scenarioStatus, item.value("scenarioStatus", ""));
            }
            episodes.push_back(episode);
        }
        return episodes;
    }
};

struct UserCard {
    int userId;
    int cardId;
    int level = 0;
    int exp = 0;
    int totalExp = 0;
    int skillLevel = 0;
    int skillExp = 0;
    int totalSkillExp = 0;
    int masterRank = 0;
    int specialTrainingStatus = 0;
    int defaultImage = 0;
    std::vector<UserCardEpisodes> episodes;

    static inline std::vector<UserCard> fromJsonList(const json& jsonData) {
        std::vector<UserCard> userCards;
        for (const auto& item : jsonData) {
            UserCard userCard;
            if (item.is_array()) {
                userCard.userId = 0;
                userCard.cardId = item.size() > 0 && item[0].is_number_integer() ? item[0].get<int>() : 0;
                userCard.level = item.size() > 1 && item[1].is_number_integer() ? item[1].get<int>() : 0;
                userCard.exp = item.size() > 2 && item[2].is_number_integer() ? item[2].get<int>() : 0;
                userCard.totalExp = item.size() > 3 && item[3].is_number_integer() ? item[3].get<int>() : 0;
                userCard.skillLevel = item.size() > 4 && item[4].is_number_integer() ? item[4].get<int>() : 0;
                userCard.skillExp = item.size() > 5 && item[5].is_number_integer() ? item[5].get<int>() : 0;
                userCard.totalSkillExp = item.size() > 6 && item[6].is_number_integer() ? item[6].get<int>() : 0;
                userCard.masterRank = item.size() > 7 && item[7].is_number_integer() ? item[7].get<int>() : 0;
                userCard.specialTrainingStatus = mapEnum(EnumMap::specialTrainingStatus, item.size() > 8 && item[8].is_string() ? item[8].get<std::string>() : "");
                userCard.defaultImage = mapEnum(EnumMap::defaultImage, item.size() > 9 && item[9].is_string() ? item[9].get<std::string>() : "");
                userCard.episodes = UserCardEpisodes::fromJsonList(item.size() > 12 && item[12].is_array() ? item[12] : json::array());
            } else {
                userCard.userId = item.value("userId", 0);
                userCard.cardId = item.value("cardId", 0);
                userCard.level = item.value("level", 0);
                userCard.exp = item.value("exp", 0);
                userCard.totalExp = item.value("totalExp", 0);
                userCard.skillLevel = item.value("skillLevel", 0);
                userCard.skillExp = item.value("skillExp", 0);
                userCard.totalSkillExp = item.value("totalSkillExp", 0);
                userCard.masterRank = item.value("masterRank", 0);
                userCard.specialTrainingStatus = mapEnum(EnumMap::specialTrainingStatus, item.value("specialTrainingStatus", ""));
                userCard.episodes = UserCardEpisodes::fromJsonList(item.value("episodes", json::array()));
                userCard.defaultImage = mapEnum(EnumMap::defaultImage, item.value("defaultImage", ""));
            }
            userCards.push_back(userCard);
        }
        return userCards;
    }
};

#endif  // USER_CARD_H


