#include "card-information/card-service.h"

#include <algorithm>
#include <tuple>

namespace {

void applyEpisodeReadCount(UserCard& userCard, const std::vector<const CardEpisode*>& episodes, int readCount)
{
    std::vector<UserCardEpisodes> userEpisodes{};
    userEpisodes.reserve(episodes.size());
    for (int i = 0; i < (int)episodes.size(); i++) {
        UserCardEpisodes episode{};
        episode.cardEpisodeId = episodes[i]->id;
        episode.scenarioStatus = i < readCount
            ? Enums::ScenarioStatus::already_read
            : 0;
        userEpisodes.push_back(episode);
    }
    userCard.episodes = std::move(userEpisodes);
}

} // namespace

std::vector<int> CardService::getCardUnits(const Card &card)
{
    // 组合（V家支援组合、角色原始组合）
    std::vector<int> cardUnits{};
    if (card.supportUnit != Enums::Unit::none) {
        cardUnits.push_back(card.supportUnit);
    }
    const auto* gameCharacter = dataProvider.masterData->findGameCharacter(card.characterId);
    if (gameCharacter == nullptr) {
        throw ElementNoFoundError("Game character not found for characterId=" + std::to_string(card.characterId));
    }
    cardUnits.push_back(gameCharacter->unit);
    return cardUnits;
}

UserCard CardService::applyCardConfig(const UserCard &userCard, const Card &card, const CardConfig &cardConfig)
{   
    bool rankMax = cardConfig.rankMax;
    bool episodeRead = cardConfig.episodeRead;
    bool masterMax = cardConfig.masterMax;
    bool skillMax = cardConfig.skillMax;
    bool hasPreciseConfig = cardConfig.level.has_value()
        || cardConfig.episodeReadCount.has_value()
        || cardConfig.masterRank.has_value()
        || cardConfig.skillLevel.has_value();

    // 都按原样，那就什么都无需调整
    if (!rankMax && !episodeRead && !masterMax && !skillMax && !hasPreciseConfig)
        return userCard;

    const auto* cardRarity = dataProvider.masterData->findCardRarity(card.cardRarityType);
    if (cardRarity == nullptr) {
        throw ElementNoFoundError("Card rarity not found for cardRarityType=" + std::to_string(card.cardRarityType));
    }

    UserCard ret = userCard;

    // 处理最大等级 
    if (rankMax) {
        // 是否可以觉醒
        if (cardRarity->trainingMaxLevel != 0) {
            ret.level = cardRarity->trainingMaxLevel;
            ret.specialTrainingStatus = Enums::SpecialTrainingStatus::done;
            ret.defaultImage = Enums::DefaultImage::special_training;
        } else {
            ret.level = cardRarity->maxLevel;
            ret.defaultImage = Enums::DefaultImage::original;
        }
    }

    if (cardConfig.level.has_value()) {
        if (cardConfig.level.value() <= 0) {
            throw std::invalid_argument("Invalid card config level: " + std::to_string(cardConfig.level.value()));
        }
        int maxLevel = cardRarity->trainingMaxLevel != 0 ? cardRarity->trainingMaxLevel : cardRarity->maxLevel;
        ret.level = std::clamp(cardConfig.level.value(), 1, maxLevel);
        if (cardRarity->trainingMaxLevel != 0 && ret.level > cardRarity->maxLevel) {
            ret.specialTrainingStatus = Enums::SpecialTrainingStatus::done;
            ret.defaultImage = Enums::DefaultImage::special_training;
        } else {
            ret.specialTrainingStatus = Enums::SpecialTrainingStatus::not_doing;
            ret.defaultImage = Enums::DefaultImage::original;
        }
    }

    // 处理前后篇解锁
    if (episodeRead) {
        applyEpisodeReadCount(ret, dataProvider.masterData->getCardEpisodesByCardId(card.id), 2);
    }

    if (cardConfig.episodeReadCount.has_value()) {
        if (cardConfig.episodeReadCount.value() < 0 || cardConfig.episodeReadCount.value() > 2) {
            throw std::invalid_argument("Invalid card config episode_read_count: " + std::to_string(cardConfig.episodeReadCount.value()));
        }
        int readCount = std::clamp(cardConfig.episodeReadCount.value(), 0, 2);
        applyEpisodeReadCount(ret, dataProvider.masterData->getCardEpisodesByCardId(card.id), readCount);
    }

    // 突破
    if (masterMax) {
        ret.masterRank = 5;
    }

    if (cardConfig.masterRank.has_value()) {
        if (cardConfig.masterRank.value() < 0 || cardConfig.masterRank.value() > 5) {
            throw std::invalid_argument("Invalid card config master_rank: " + std::to_string(cardConfig.masterRank.value()));
        }
        ret.masterRank = std::clamp(cardConfig.masterRank.value(), 0, 5);
    }

    // 技能
    if (skillMax) {
        ret.skillLevel = cardRarity->maxSkillLevel;
    }

    if (cardConfig.skillLevel.has_value()) {
        if (cardConfig.skillLevel.value() <= 0) {
            throw std::invalid_argument("Invalid card config skill_level: " + std::to_string(cardConfig.skillLevel.value()));
        }
        ret.skillLevel = std::clamp(cardConfig.skillLevel.value(), 1, cardRarity->maxSkillLevel);
    }

    return ret;
}
