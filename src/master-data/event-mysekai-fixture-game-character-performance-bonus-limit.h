#ifndef EVENT_MYSEKAI_FIXTURE_GAME_CHARACTER_PERFORMANCE_BONUS_LIMIT_H
#define EVENT_MYSEKAI_FIXTURE_GAME_CHARACTER_PERFORMANCE_BONUS_LIMIT_H

#include "common/collection-utils.h"

struct EventMysekaiFixtureGameCharacterPerformanceBonusLimit {
    int id = 0;
    int eventId = 0;
    int bonusRateLimit = 0;

    static inline std::vector<EventMysekaiFixtureGameCharacterPerformanceBonusLimit> fromJsonList(const json_view& jsonData) {
        std::vector<EventMysekaiFixtureGameCharacterPerformanceBonusLimit> limits;
        for (const auto& item : jsonData) {
            EventMysekaiFixtureGameCharacterPerformanceBonusLimit limit;
            limit.id = item.value("id", 0);
            limit.eventId = item.value("eventId", 0);
            limit.bonusRateLimit = item.value("bonusRateLimit", 0);
            limits.push_back(limit);
        }
        return limits;
    }
};

#endif // EVENT_MYSEKAI_FIXTURE_GAME_CHARACTER_PERFORMANCE_BONUS_LIMIT_H
