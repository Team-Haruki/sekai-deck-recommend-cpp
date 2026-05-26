#ifndef MUSIC_RECOMMEND_H
#define MUSIC_RECOMMEND_H

#include "event-point/event-calculator.h"

struct RecommendMusic {
    int musicId = 0;
    int difficulty = 0;
    int liveScore = 0;
    std::optional<int> eventPoint = std::nullopt;
};

class MusicRecommend {
    DataProvider dataProvider;
    LiveCalculator liveCalculator;
    EventCalculator eventCalculator;

public:
    MusicRecommend(const DataProvider& dataProvider)
        : dataProvider(dataProvider),
          liveCalculator(dataProvider),
          eventCalculator(dataProvider) {}

    /**
     * Score every loaded music meta for an already calculated deck.
     */
    std::vector<RecommendMusic> recommendMusic(
        const DeckDetail& deck,
        int liveType,
        int eventType = 0,
        LiveSkillOrder liveSkillOrder = LiveSkillOrder::average,
        std::optional<std::vector<int>> specificSkillOrder = std::nullopt,
        std::optional<int> multiTeammateScoreUp = std::nullopt,
        std::optional<int> multiTeammatePower = std::nullopt
    );
};

#endif // MUSIC_RECOMMEND_H
