#include "music-recommend/music-recommend.h"

#include <algorithm>

std::vector<RecommendMusic> MusicRecommend::recommendMusic(
    const DeckDetail& deck,
    int liveType,
    int eventType,
    LiveSkillOrder liveSkillOrder,
    std::optional<std::vector<int>> specificSkillOrder,
    std::optional<int> multiTeammateScoreUp,
    std::optional<int> multiTeammatePower
)
{
    if (!dataProvider.musicMetas) {
        throw std::runtime_error("Music metas are not loaded");
    }
    std::vector<RecommendMusic> result{};
    result.reserve(dataProvider.musicMetas->metas.size());
    for (const auto& musicMeta : dataProvider.musicMetas->metas) {
        int liveScore = liveCalculator.getLiveScoreByDeck(
            deck,
            musicMeta,
            liveType,
            liveSkillOrder,
            specificSkillOrder,
            multiTeammateScoreUp,
            multiTeammatePower
        );

        std::optional<int> eventPoint = std::nullopt;
        if (Enums::LiveType::isChallenge(liveType) || deck.eventBonus.has_value()) {
            eventPoint = eventCalculator.getEventPoint(
                liveType,
                eventType,
                liveScore,
                musicMeta.event_rate,
                deck.eventBonus.value_or(0.0) + deck.supportDeckBonus.value_or(0.0)
            );
        }
        result.push_back(RecommendMusic{
            .musicId = musicMeta.music_id,
            .difficulty = musicMeta.difficulty,
            .liveScore = liveScore,
            .eventPoint = eventPoint,
        });
    }
    std::sort(result.begin(), result.end(), [](const RecommendMusic& a, const RecommendMusic& b) {
        return std::tuple(a.eventPoint.value_or(-1), a.liveScore, -a.musicId, -a.difficulty)
            > std::tuple(b.eventPoint.value_or(-1), b.liveScore, -b.musicId, -b.difficulty);
    });
    return result;
}
