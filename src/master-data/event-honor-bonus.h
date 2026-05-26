#ifndef EVENT_HONOR_BONUS_H
#define EVENT_HONOR_BONUS_H

#include "common/collection-utils.h"

struct EventHonorBonus {
    int id = 0;
    int eventId = 0;
    int honorId = 0;
    int leaderGameCharacterId = 0;
    double bonusRate = 0.0;

    static inline std::vector<EventHonorBonus> fromJsonList(const json_view& jsonData) {
        std::vector<EventHonorBonus> bonuses;
        for (const auto& item : jsonData) {
            EventHonorBonus bonus;
            bonus.id = item.value("id", 0);
            bonus.eventId = item.value("eventId", 0);
            bonus.honorId = item.value("honorId", 0);
            bonus.leaderGameCharacterId = item.value("leaderGameCharacterId", 0);
            bonus.bonusRate = item.value("bonusRate", 0.0);
            bonuses.push_back(bonus);
        }
        return bonuses;
    }
};

#endif // EVENT_HONOR_BONUS_H
