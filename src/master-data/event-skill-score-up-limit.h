#ifndef EVENT_SKILL_SCORE_UP_LIMIT_H
#define EVENT_SKILL_SCORE_UP_LIMIT_H

#include "common/collection-utils.h"

struct EventSkillScoreUpLimit {
    int id = 0;
    int eventId = 0;
    double scoreUpRateLimit = 0.0;

    static inline std::vector<EventSkillScoreUpLimit> fromJsonList(const json_view& jsonData) {
        std::vector<EventSkillScoreUpLimit> limits;
        for (const auto& item : jsonData) {
            EventSkillScoreUpLimit limit;
            limit.id = item.value("id", 0);
            limit.eventId = item.value("eventId", 0);
            limit.scoreUpRateLimit = item.value("scoreUpRateLimit", 0.0);
            limits.push_back(limit);
        }
        return limits;
    }
};

#endif // EVENT_SKILL_SCORE_UP_LIMIT_H
