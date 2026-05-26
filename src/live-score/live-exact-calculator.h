#ifndef LIVE_EXACT_CALCULATOR_H
#define LIVE_EXACT_CALCULATOR_H

#include "common/music-score.h"
#include "data-provider/data-provider.h"

struct LiveExactNoteDetail {
    double noteCoefficient = 0.0;
    double comboCoefficient = 0.0;
    double judgeCoefficient = 1.0;
    std::vector<double> effectBonuses;
    double score = 0.0;
};

struct LiveExactDetail {
    double total = 0.0;
    double activeBonus = 0.0;
    std::vector<LiveExactNoteDetail> notes;
};

struct IngameEffectDetail {
    double startTime = 0.0;
    double endTime = 0.0;
    double effect = 0.0;
};

class LiveExactCalculator {
    DataProvider dataProvider;

    std::vector<IngameEffectDetail> getSkillDetails(
        const std::vector<double>& skills,
        const std::vector<MusicNoteBase>& musicSkills
    ) const;

    IngameEffectDetail getFeverDetail(const MusicScore& musicScore) const;

public:
    LiveExactCalculator(const DataProvider& dataProvider) : dataProvider(dataProvider) {}

    /**
     * Calculate note-level score for a parsed chart.
     */
    LiveExactDetail calculate(
        int power,
        const std::vector<double>& skills,
        int liveType,
        const MusicScore& musicScore,
        int multiSumPower = 0,
        const std::optional<MusicScore>& feverMusicScore = std::nullopt
    );
};

#endif // LIVE_EXACT_CALCULATOR_H
