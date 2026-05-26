#ifndef EVENT_CARD_BONUS_LIMIT_H
#define EVENT_CARD_BONUS_LIMIT_H

#include "common/collection-utils.h"

struct EventCardBonusLimit {
    int id = 0;
    int eventId = 0;
    int memberCountLimit = 5;

    static inline std::vector<EventCardBonusLimit> fromJsonList(const json_view& jsonData) {
        std::vector<EventCardBonusLimit> limits;
        for (const auto& item : jsonData) {
            EventCardBonusLimit limit;
            limit.id = item.value("id", 0);
            limit.eventId = item.value("eventId", 0);
            limit.memberCountLimit = item.value("memberCountLimit", 5);
            limits.push_back(limit);
        }
        return limits;
    }
};

#endif // EVENT_CARD_BONUS_LIMIT_H
