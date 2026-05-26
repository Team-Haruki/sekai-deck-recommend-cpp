#include "live-score/live-exact-calculator.h"

#include <algorithm>
#include <cmath>
#include <numeric>

std::vector<IngameEffectDetail> LiveExactCalculator::getSkillDetails(
    const std::vector<double>& skills,
    const std::vector<MusicNoteBase>& musicSkills
) const
{
    std::vector<IngameEffectDetail> effects{};
    int count = std::min(skills.size(), musicSkills.size());
    effects.reserve(count);
    for (int i = 0; i < count; ++i) {
        effects.push_back(IngameEffectDetail{
            .startTime = musicSkills[i].time,
            .endTime = musicSkills[i].time + 5.0,
            .effect = skills[i],
        });
    }
    return effects;
}

IngameEffectDetail LiveExactCalculator::getFeverDetail(const MusicScore& musicScore) const
{
    if (musicScore.fevers.empty() || musicScore.notes.empty()) {
        return {};
    }

    double startTime = 0.0;
    for (const auto& fever : musicScore.fevers) {
        startTime = std::max(startTime, fever.time);
    }

    std::vector<MusicNote> notesAfterFever{};
    for (const auto& note : musicScore.notes) {
        if (note.time >= startTime) {
            notesAfterFever.push_back(note);
        }
    }
    if (notesAfterFever.empty()) {
        return {};
    }
    int feverNoteCount = std::min<int>(notesAfterFever.size(), std::floor(musicScore.notes.size() / 10.0));
    feverNoteCount = std::max(feverNoteCount, 1);
    return IngameEffectDetail{
        .startTime = startTime,
        .endTime = notesAfterFever[feverNoteCount - 1].time,
        .effect = 50.0,
    };
}

LiveExactDetail LiveExactCalculator::calculate(
    int power,
    const std::vector<double>& skills,
    int liveType,
    const MusicScore& musicScore,
    int multiSumPower,
    const std::optional<MusicScore>& feverMusicScore
)
{
    if (musicScore.notes.empty()) {
        throw std::invalid_argument("musicScore.notes must not be empty");
    }
    if (dataProvider.masterData->ingameNotes.empty()) {
        throw std::runtime_error("ingameNotes master data is not loaded");
    }
    if (dataProvider.masterData->ingameCombos.empty()) {
        throw std::runtime_error("ingameCombos master data is not loaded");
    }

    auto effects = getSkillDetails(skills, musicScore.skills);
    if (Enums::LiveType::isMulti(liveType)) {
        effects.push_back(getFeverDetail(feverMusicScore.value_or(musicScore)));
    }

    std::vector<double> noteCoefficients{};
    noteCoefficients.reserve(musicScore.notes.size());
    for (const auto& note : musicScore.notes) {
        auto& ingameNote = findOrThrow(dataProvider.masterData->ingameNotes, [&](const IngameNote& it) {
            return it.id == note.type;
        }, [&]() { return "Ingame note not found for type=" + std::to_string(note.type); });
        noteCoefficients.push_back(ingameNote.scoreCoefficient);
    }
    double coefficientTotal = std::accumulate(noteCoefficients.begin(), noteCoefficients.end(), 0.0);
    if (coefficientTotal <= 0.0) {
        throw std::runtime_error("musicScore note coefficient total must be positive");
    }

    LiveExactDetail detail{};
    detail.notes.reserve(musicScore.notes.size());
    for (int i = 0; i < (int)musicScore.notes.size(); ++i) {
        const auto& note = musicScore.notes[i];
        int combo = i + 1;
        auto& ingameCombo = findOrThrow(dataProvider.masterData->ingameCombos, [&](const IngameCombo& it) {
            return it.fromCount <= combo && combo <= it.toCount;
        }, [&]() { return "Ingame combo not found for combo=" + std::to_string(combo); });

        std::vector<double> effectBonuses{};
        double effectCoefficient = 1.0;
        for (const auto& effect : effects) {
            if (effect.startTime <= note.time && note.time <= effect.endTime) {
                effectBonuses.push_back(effect.effect);
                effectCoefficient *= effect.effect / 100.0;
            }
        }

        double score = noteCoefficients[i]
            * ingameCombo.scoreCoefficient
            * 1.0
            * effectCoefficient
            * double(power)
            * 4.0
            / coefficientTotal;

        detail.notes.push_back(LiveExactNoteDetail{
            .noteCoefficient = noteCoefficients[i],
            .comboCoefficient = ingameCombo.scoreCoefficient,
            .judgeCoefficient = 1.0,
            .effectBonuses = std::move(effectBonuses),
            .score = score,
        });
        detail.total += score;
    }

    if (Enums::LiveType::isMulti(liveType)) {
        int powerSum = multiSumPower > 0 ? multiSumPower : power * 5;
        detail.activeBonus = 5.0 * 0.015 * double(powerSum);
        detail.total += detail.activeBonus;
    }
    return detail;
}
