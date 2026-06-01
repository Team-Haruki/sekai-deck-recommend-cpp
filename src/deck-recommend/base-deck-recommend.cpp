#include "deck-recommend/base-deck-recommend.h"
#include "card-priority/card-priority-filter.h"
#include "common/timer.h"
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>


uint64_t BaseDeckRecommend::calcDeckHash(const std::vector<const CardDetail*>& deck) const {
    int card_num = (int)deck.size();
    std::array<int, 5> v{};
    for (int i = 0; i < card_num; ++i)
        v[i] = deck[i]->cardId;
    std::sort(v.begin() + 1, v.begin() + card_num);
    constexpr uint64_t base = 10007;
    uint64_t hash = 0;
    for (int i = 0; i < card_num; ++i) 
        hash = hash * base + v[i];
    return hash;
};

namespace {

struct RecommendTimings {
    long long cardBuildNs = 0;
    long long supportBuildNs = 0;
    long long searchNs = 0;
    long long finalDecorateNs = 0;
};

struct CardHotFields {
    int cardId = 0;
    int characterId = 0;
    int attr = 0;
    int unitMask = 0;
    int powerMin = 0;
    int powerMax = 0;
    double skillMin = 0.0;
    double skillMax = 0.0;
    double eventBonus = 0.0;
    double supportBonus = 0.0;
    double scoreHeuristic = 0.0;
    uint64_t scoreNoEventKey = 0;
};

int makeUnitMask(const CardDetail& card) {
    int mask = 0;
    for (const auto unit : card.units) {
        if (unit >= 0 && unit < 31) {
            mask |= (1 << unit);
        }
    }
    return mask;
}

CardHotFields buildCardHotField(const CardDetail& card, const DeckRecommendConfig& config) {
    double eventBonus = std::max(
        card.maxEventBonus.value_or(0.0),
        card.limitedEventBonus.value_or(0.0)
    );
    double powerNorm = std::max(0.0, double(card.power.max) / POWER_MAX);
    double skillNorm = std::max(0.0, double(card.skill.max) / SKILL_MAX);
    double eventNorm = std::max(0.0, eventBonus / 70.0);
    double supportNorm = std::max(0.0, card.supportDeckBonus.value_or(0.0) / 50.0);
    double scoreHeuristic = 0.0;
    if (config.target == RecommendTarget::Mysekai) {
        scoreHeuristic = 0.90 * powerNorm + 1.35 * eventNorm + 0.25 * supportNorm;
    } else if (config.target == RecommendTarget::Bonus) {
        scoreHeuristic = 1.30 * eventNorm + 0.35 * supportNorm + 0.10 * powerNorm + 0.05 * skillNorm;
    } else {
        scoreHeuristic = 0.55 * powerNorm + 0.75 * skillNorm + 0.30 * eventNorm + 0.15 * supportNorm;
    }
    return CardHotFields{
            .cardId = card.cardId,
            .characterId = card.characterId,
            .attr = card.attr,
            .unitMask = makeUnitMask(card),
            .powerMin = card.power.min,
            .powerMax = card.power.max,
            .skillMin = static_cast<double>(card.skill.min),
            .skillMax = static_cast<double>(card.skill.max),
            .eventBonus = eventBonus,
            .supportBonus = card.supportDeckBonus.value_or(0.0),
            .scoreHeuristic = scoreHeuristic,
            .scoreNoEventKey = uint64_t(std::max(0, card.power.max)) * (256 + uint64_t(std::max(0.0, double(card.skill.max)))),
        };
}

std::string makeBestPermutationCacheKey(
    uint64_t deckHash,
    int honorBonus,
    std::optional<int> eventType,
    std::optional<int> eventId,
    int liveType,
    const DeckRecommendConfig& config
) {
    std::string key = std::to_string(deckHash);
    key += "|h=" + std::to_string(honorBonus);
    key += "|et=" + std::to_string(eventType.value_or(-1));
    key += "|eid=" + std::to_string(eventId.value_or(-1));
    key += "|lt=" + std::to_string(liveType);
    key += "|target=" + std::to_string(int(config.target));
    key += "|skillRef=" + std::to_string(int(config.skillReferenceChooseStrategy));
    key += "|keepState=" + std::to_string(config.keepAfterTrainingState);
    key += "|bestLeader=" + std::to_string(config.bestSkillAsLeader);
    key += "|lower=" + std::to_string(config.multiScoreUpLowerBound);
    key += "|skillOrder=" + std::to_string(int(config.liveSkillOrder));
    if (config.specificSkillOrder.has_value()) {
        key += "|specific=";
        for (const auto order : config.specificSkillOrder.value()) {
            key += std::to_string(order) + ",";
        }
    }
    key += "|teamSU=" + std::to_string(config.multiTeammateScoreUp.value_or(-1));
    key += "|teamP=" + std::to_string(config.multiTeammatePower.value_or(-1));
    return key;
}

void maybePrintTimings(const RecommendTimings& timings, const RecommendEvalCache& cache) {
    if (std::getenv("SEKAI_DECK_RECOMMEND_TIMING") == nullptr) {
        return;
    }
    auto ms = [](long long ns) { return double(ns) / 1'000'000.0; };
    std::cerr << "[sekai-deck-recommend-cpp] timings"
              << " card_build_ms=" << ms(timings.cardBuildNs)
              << " support_build_ms=" << ms(timings.supportBuildNs)
              << " search_ms=" << ms(timings.searchNs)
              << " final_decorate_ms=" << ms(timings.finalDecorateNs)
              << " best_perm_cache_hits=" << cache.bestPermutationHits
              << " best_perm_cache_misses=" << cache.bestPermutationMisses
              << std::endl;
}

bool applyFixedCardOrder(
    std::vector<const CardDetail*>& deck,
    const std::vector<int>& fixedCardIds
) {
    std::size_t target = 0;
    for (const auto& cardId : fixedCardIds) {
        if (target >= deck.size()) {
            return false;
        }
        auto it = std::find_if(
            deck.begin() + target,
            deck.end(),
            [&](const CardDetail* card) {
                return card && card->cardId == cardId;
            }
        );
        if (it == deck.end()) {
            return false;
        }
        if (it != deck.begin() + target) {
            std::rotate(deck.begin() + target, it, it + 1);
        }
        target++;
    }
    return true;
}

} // namespace


/*
获取当前卡组的最佳排列
*/
BestPermutationResult BaseDeckRecommend::getBestPermutation(
    DeckCalculator& deckCalculator,
    const std::vector<const CardDetail*> &deckCards,
    std::map<int, std::vector<SupportDeckCard>>& supportCards,
    const std::function<Score(const DeckDetail &)> &scoreFunc,
    int honorBonus,
    std::optional<int> eventType,
    std::optional<int> eventId,
    int liveType,
    const DeckRecommendConfig& config,
    RecommendEvalCache* evalCache
) const {
    std::string cacheKey{};
    if (evalCache != nullptr) {
        const uint64_t inputDeckHash = this->calcDeckHash(deckCards);
        cacheKey = makeBestPermutationCacheKey(
            inputDeckHash,
            honorBonus,
            eventType,
            eventId,
            liveType,
            config
        );
        auto it = evalCache->bestPermutationCache.find(cacheKey);
        if (it != evalCache->bestPermutationCache.end()) {
            evalCache->bestPermutationHits++;
            return it->second;
        }
        evalCache->bestPermutationMisses++;
    }

    auto cacheAndReturn = [&](BestPermutationResult result) {
        if (evalCache != nullptr) {
            evalCache->bestPermutationCache.emplace(cacheKey, result);
        }
        return result;
    };

    auto orderedDeckCards = deckCards;
    bool isWorldBloomFinale = eventId.has_value() && this->dataProvider.masterData->isWorldBloomFinale(eventId.value());
    int specialCharacterId = 0;
    if (isWorldBloomFinale && !deckCards.empty()) {
        specialCharacterId = deckCards.front()->characterId;
    }
    {
        std::unordered_set<int> cardIds{};
        std::unordered_set<int> characterIds{};
        for (const auto* card : orderedDeckCards) {
            if (!card) {
                return cacheAndReturn({});
            }
            if (!cardIds.insert(card->cardId).second) {
                return cacheAndReturn({});
            }
            if (!Enums::LiveType::isChallenge(liveType)) {
                if (!characterIds.insert(card->characterId).second) {
                    return cacheAndReturn({});
                }
            } else {
                characterIds.insert(card->characterId);
            }
        }

        for (const auto& fixedCardId : config.fixedCards) {
            if (!cardIds.count(fixedCardId)) {
                return cacheAndReturn({});
            }
        }
        for (const auto& characterId : resolveRequiredCharacters(config, isWorldBloomFinale, specialCharacterId)) {
            if (!characterIds.count(characterId)) {
                return cacheAndReturn({});
            }
        }
    }

    if (!config.fixedCards.empty() && !applyFixedCardOrder(orderedDeckCards, config.fixedCards)) {
        return cacheAndReturn({});
    }

    auto leaderCharacterId = resolveLeaderCharacterId(config, isWorldBloomFinale, specialCharacterId);
    if (leaderCharacterId.has_value()) {
        auto leaderIt = std::find_if(
            orderedDeckCards.begin(),
            orderedDeckCards.end(),
            [&](const CardDetail* card) {
                return card->characterId == leaderCharacterId.value();
            }
        );
        if (leaderIt == orderedDeckCards.end()) {
            return cacheAndReturn({});
        }
        if (leaderIt != orderedDeckCards.begin()) {
            std::rotate(orderedDeckCards.begin(), leaderIt, leaderIt + 1);
        }
    }

    bool bestSkillAsLeader = config.bestSkillAsLeader;
    // 存在固定队长角色则不允许把技能最强的换到队长
    if (config.fixedCharacters.size()) bestSkillAsLeader = false;
    // Finale events have leader-only bonus rules.
    if (isWorldBloomFinale) bestSkillAsLeader = false;
    // 获取当前卡组的详情
    auto deckDetails = deckCalculator.getDeckDetailByCards(
        orderedDeckCards, supportCards, honorBonus, eventType, eventId,
        config.skillReferenceChooseStrategy, config.keepAfterTrainingState, bestSkillAsLeader
    );
    // 获取目标值最高的卡组
    double maxValue = -1e18;
    BestPermutationResult ret{};
    for (auto& deckDetail : deckDetails) {
        auto score = scoreFunc(deckDetail);
        RecommendDeck candidate(deckDetail, config.target, score);
        double value = candidate.targetValue;

        ret.maxTargetValue = std::max(ret.maxTargetValue, value);
        ret.maxMultiLiveScoreUp = std::max(ret.maxMultiLiveScoreUp, deckDetail.multiLiveScoreUp);
        
        // 最低实效限制
        if (deckDetail.multiLiveScoreUp < config.multiScoreUpLowerBound)
            continue;
        
        if (value > maxValue) {
            maxValue = value;
            ret.bestDeck = std::move(candidate);
        }
    }
    return cacheAndReturn(std::move(ret));
}


std::vector<RecommendDeck> BaseDeckRecommend::recommendHighScoreDeck(
    const std::vector<UserCard> &userCards,
    ScoreFunction scoreFunc,
    const DeckRecommendConfig &config,
    int liveType,
    const EventConfig &eventConfig)
{
    this->dataProvider.init();

    std::set<int> configuredFixedCharacterSet{};
    for (const auto& characterId : config.fixedCharacters) {
        if (characterId < 1 || characterId > 26)
            throw std::runtime_error("Invalid fixed character ID: " + std::to_string(characterId));
        configuredFixedCharacterSet.insert(characterId);
    }
    if (configuredFixedCharacterSet.size() != config.fixedCharacters.size())
        throw std::runtime_error("Fixed characters have duplicate characters");
    if (config.forcedLeaderCharacterId.has_value()
        && (config.forcedLeaderCharacterId.value() < 1 || config.forcedLeaderCharacterId.value() > 26))
        throw std::runtime_error("Invalid forced leader character ID: " + std::to_string(config.forcedLeaderCharacterId.value()));

    auto requiredCharacters = resolveRequiredCharacters(
        config,
        eventConfig.isWorldBloomFinale,
        eventConfig.specialCharacterId
    );
    std::set<int> requiredCharacterSet{};
    for (const auto& characterId : requiredCharacters) {
        requiredCharacterSet.insert(characterId);
    }
    // 挑战live不允许指定固定角色
    if (Enums::LiveType::isChallenge(liveType) && requiredCharacters.size())
        throw std::runtime_error("Cannot set fixed characters in challenge live");

    auto musicMeta = this->liveCalculator.getMusicMeta(config.musicId, config.musicDiff);
    RecommendTimings timings{};
    RecommendEvalCache evalCache{};
    auto phaseStart = std::chrono::high_resolution_clock::now();

    auto areaItemLevels = areaItemService.getAreaItemLevels();

    std::optional<double> scoreUpLimit = std::nullopt;
    if (eventConfig.skillScoreUpLimit.has_value() && !Enums::LiveType::isChallenge(liveType))
        scoreUpLimit = eventConfig.skillScoreUpLimit;

    auto cards = cardCalculator.batchGetCardDetail(
        userCards, config.cardConfig, config.singleCardConfig, 
        eventConfig, areaItemLevels, scoreUpLimit,
        config.customBonusCharacterIds, config.customBonusAttr, config.customBonusSupportUnits
    );
    timings.cardBuildNs += std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now() - phaseStart
    ).count();

    // 归类支援卡组
    phaseStart = std::chrono::high_resolution_clock::now();
    std::map<int, std::vector<SupportDeckCard>> supportCards{};
    if (eventConfig.isWorldBloomFinale) {
        // Finale scores a support deck for every possible leader character.
        for (int i = 1; i <= 26; i++) {
            std::vector<SupportDeckCard> sc{};
            for (const auto& card : userCards) 
                sc.push_back(this->cardCalculator.getSupportDeckCard(
                    card,
                    eventConfig.eventId,
                    i,
                    config.supportMasterMax,
                    config.supportSkillMax
                ));
            std::sort(sc.begin(), sc.end(), [](const SupportDeckCard& a, const SupportDeckCard& b) { return a.bonus > b.bonus; });
            supportCards[i] = sc;
        }
    } else if(eventConfig.eventType == Enums::EventType::world_bloom) {
        // 普通wl只算一个支援卡组排序
        std::vector<SupportDeckCard> sc{};
        for (const auto& card : userCards) 
            sc.push_back(this->cardCalculator.getSupportDeckCard(
                card,
                eventConfig.eventId,
                eventConfig.specialCharacterId,
                config.supportMasterMax,
                config.supportSkillMax
            ));
        std::sort(sc.begin(), sc.end(), [](const SupportDeckCard& a, const SupportDeckCard& b) { return a.bonus > b.bonus; });
        supportCards[0] = sc;
    }
    timings.supportBuildNs += std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now() - phaseStart
    ).count();

    int filterUnit = eventConfig.worldBloomSupportUnit ? eventConfig.worldBloomSupportUnit : eventConfig.eventUnit;
    // 过滤箱活/World Bloom应援组合的卡，不上其它组合的
    if (filterUnit && config.filterOtherUnit) {
        std::vector<CardDetail> newCards{};
        for (const auto& card : cards) {
            if ((card.units.size() == 1 && card.units[0] == Enums::Unit::piapro) || 
                std::find(card.units.begin(), card.units.end(), filterUnit) != card.units.end()) {
                newCards.push_back(card);
            }
        }
        cards = std::move(newCards);
    }

    // 获取固定卡牌
    std::vector<CardDetail> fixedCards{};
    for (auto card_id : config.fixedCards) {
        // 从当前卡牌中找到对应的卡牌
        auto it = std::find_if(cards.begin(), cards.end(), [&](const CardDetail& card) {
            return card.cardId == card_id;
        });
        if (it != cards.end()) {
            fixedCards.push_back(*it);
        } else {
            // 找不到的情况下，生成一个初始养成情况的卡牌
            UserCard uc;
            uc.cardId = card_id;
            uc.level = 1;
            uc.skillLevel = 1;
            uc.masterRank = 0;
            uc.specialTrainingStatus = Enums::SpecialTrainingStatus::not_doing;
            
            const Card* c = this->dataProvider.masterData->findCard(card_id);
            if (c == nullptr) {
                throw ElementNoFoundError("Card not found for fixed cardId=" + std::to_string(card_id));
            }
            bool hasSpecialTraining = c->cardRarityType == Enums::CardRarityType::rarity_3
                                    || c->cardRarityType == Enums::CardRarityType::rarity_4;
            uc.defaultImage = hasSpecialTraining ? Enums::DefaultImage::special_training : Enums::DefaultImage::original;

            for (const auto* ep : this->dataProvider.masterData->getCardEpisodesByCardId(card_id)) {
                UserCardEpisodes uce{};
                uce.cardEpisodeId = ep->id;
                uce.scenarioStatus = 0;
                uc.episodes.push_back(uce);
            }
            phaseStart = std::chrono::high_resolution_clock::now();
            auto card = cardCalculator.batchGetCardDetail(
                {uc}, config.cardConfig, config.singleCardConfig, 
                eventConfig, areaItemLevels, scoreUpLimit,
                config.customBonusCharacterIds, config.customBonusAttr, config.customBonusSupportUnits
            );
            timings.cardBuildNs += std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::high_resolution_clock::now() - phaseStart
            ).count();
            if (card.size() > 0) {
                fixedCards.push_back(card[0]);
                cards.push_back(card[0]);
            } else {
                throw std::runtime_error("Failed to generate virtual card for fixed card id: " + std::to_string(card_id));
            }
        }
    }
    // 检查固定卡牌是否有效
    std::set<int> fixedCardIds{};
    std::set<int> fixedCardCharacterIds{};
    if (fixedCards.size()) {
        for (const auto& card : fixedCards) {
            fixedCardIds.insert(card.cardId);
            fixedCardCharacterIds.insert(card.characterId);
        }
        if (int(fixedCards.size()) > config.member) {
            throw std::runtime_error("Fixed cards size is larger than member size");
        }
        if (fixedCardIds.size() != fixedCards.size()) {
            throw std::runtime_error("Fixed cards have duplicate cards");
        }
        if (Enums::LiveType::isChallenge(liveType)) {
            if (fixedCardCharacterIds.size() != 1 || fixedCards[0].characterId != cards[0].characterId) {
                throw std::runtime_error("Fixed cards have invalid characters");
            }
        } else {
            if (fixedCardCharacterIds.size() != fixedCards.size()) {
                throw std::runtime_error("Fixed cards have duplicate characters");
            }
        }
    }
    auto occupiedCharacterIds = fixedCardCharacterIds;
    occupiedCharacterIds.insert(requiredCharacterSet.begin(), requiredCharacterSet.end());
    if (int(occupiedCharacterIds.size()) > config.member) {
        throw std::runtime_error("Fixed cards and fixed characters exceed member count");
    }

    auto honorBonus = deckCalculator.getHonorBonusPower();

    auto sf = [&scoreFunc, &musicMeta](const DeckDetail& deckDetail) { return scoreFunc(musicMeta, deckDetail); };
    auto nowNs = []() -> long long {
        return std::chrono::high_resolution_clock::now().time_since_epoch().count();
    };
    auto timeoutToNs = [](int timeoutMs) -> long long {
        auto clamped = std::max(timeoutMs, 1);
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::milliseconds(clamped)
        ).count();
    };
    auto makeCalcInfo = [&](int timeoutMs) {
        RecommendCalcInfo info{};
        info.start_ts = nowNs();
        info.timeout = timeoutToNs(timeoutMs);
        return info;
    };
    auto resolveBudgetMs = [](int timeoutMs, double ratio, int minMs, int maxMs) {
        if (timeoutMs <= 0 || timeoutMs > 600000) {
            return maxMs;
        }
        auto scaled = int(double(timeoutMs) * ratio);
        return std::max(1, std::min(maxMs, std::max(minMs, scaled)));
    };
    auto cardEventBonus = [](const CardDetail& card) {
        return std::max(
            card.maxEventBonus.value_or(0.0),
            card.limitedEventBonus.value_or(0.0)
        );
    };
    auto scoreHeuristic = [&](const CardDetail& card) {
        double powerNorm = std::max(0.0, double(card.power.max) / POWER_MAX);
        double skillNorm = std::max(0.0, double(card.skill.max) / SKILL_MAX);
        double eventBonus = cardEventBonus(card);
        double eventNorm = std::max(0.0, eventBonus / 70.0);
        double supportNorm = std::max(0.0, card.supportDeckBonus.value_or(0.0) / 50.0);
        if (config.target == RecommendTarget::Mysekai) {
            return 0.90 * powerNorm
                + 1.35 * eventNorm
                + 0.25 * supportNorm;
        } else if (config.target == RecommendTarget::Bonus) {
            return 1.30 * eventNorm
                + 0.35 * supportNorm
                + 0.10 * powerNorm
                + 0.05 * skillNorm;
        }
        return 0.55 * powerNorm
            + 0.75 * skillNorm
            + 0.30 * eventNorm
            + 0.15 * supportNorm;
    };
    auto sortCardsByStrength = [&](std::vector<CardDetail> input) {
        if (config.target == RecommendTarget::Skill) {
            std::sort(input.begin(), input.end(), [](const CardDetail& a, const CardDetail& b) {
                return std::make_tuple(a.skill.max, a.skill.min, a.cardId)
                    > std::make_tuple(b.skill.max, b.skill.min, b.cardId);
            });
            return input;
        }
        if (config.target == RecommendTarget::Score) {
            std::sort(input.begin(), input.end(), [&](const CardDetail& a, const CardDetail& b) {
                return std::make_tuple(scoreHeuristic(a), cardEventBonus(a), a.skill.max, a.power.max, a.cardId)
                    > std::make_tuple(scoreHeuristic(b), cardEventBonus(b), b.skill.max, b.power.max, b.cardId);
            });
            return input;
        }
        if (config.target != RecommendTarget::Mysekai && config.target != RecommendTarget::Bonus) {
            std::sort(input.begin(), input.end(), [](const CardDetail& a, const CardDetail& b) {
                return std::make_tuple(a.power.max, a.power.min, a.cardId)
                    > std::make_tuple(b.power.max, b.power.min, b.cardId);
            });
            return input;
        }

        struct SortableCard {
            CardDetail card;
            CardHotFields hot;
        };
        std::vector<SortableCard> sortable{};
        sortable.reserve(input.size());
        for (auto& card : input) {
            auto hot = buildCardHotField(card, config);
            sortable.push_back(SortableCard{
                .card = std::move(card),
                .hot = hot
            });
        }
        std::sort(sortable.begin(), sortable.end(), [](const SortableCard& a, const SortableCard& b) {
            const auto& ah = a.hot;
            const auto& bh = b.hot;
            return std::make_tuple(ah.scoreHeuristic, ah.eventBonus, ah.skillMax, ah.powerMax, ah.cardId)
                > std::make_tuple(bh.scoreHeuristic, bh.eventBonus, bh.skillMax, bh.powerMax, bh.cardId);
        });
        std::vector<CardDetail> sorted{};
        sorted.reserve(sortable.size());
        for (auto& item : sortable) {
            sorted.push_back(std::move(item.card));
        }
        return sorted;
    };
    auto collectResults = [](const RecommendCalcInfo& info) {
        std::vector<RecommendDeck> decks{};
        auto q = info.deckQueue;
        while (q.size()) {
            decks.emplace_back(q.top());
            q.pop();
        }
        std::reverse(decks.begin(), decks.end());
        return decks;
    };
    auto bestTargetValue = [&](const RecommendCalcInfo& info) {
        auto decks = collectResults(info);
        if (decks.empty()) {
            return -1e18;
        }
        return decks.front().targetValue;
    };
    auto mergeCalcInfo = [&](RecommendCalcInfo& dst, const RecommendCalcInfo& src) {
        for (const auto& [hash, value] : src.deckTargetValueMap) {
            dst.deckTargetValueMap.emplace(hash, value);
        }
        auto q = src.deckQueue;
        while (q.size()) {
            dst.update(q.top(), config.limit);
            q.pop();
        }
    };
    auto initDfsState = [&](RecommendCalcInfo& info) {
        info.deckCards.clear();
        info.deckCharacters = 0;
        for (const auto& card : fixedCards) {
            info.deckCards.push_back(&card);
            info.deckCharacters.flip(card.characterId);
        }
    };
    auto buildDfsScoreUpperBoundContext = [&](const std::vector<CardDetail>& sourceCards) {
        DfsScoreUpperBoundContext context{};
        context.musicMeta = musicMeta;
        context.hasCharacterBounds = true;
        for (const auto& card : sourceCards) {
            if (card.characterId < 0 || card.characterId >= int(context.bestPowerByCharacter.size())) {
                context.hasCharacterBounds = false;
                continue;
            }
            auto index = std::size_t(card.characterId);
            context.bestPowerByCharacter[index] = std::max(context.bestPowerByCharacter[index], card.power.max);
            context.bestSkillByCharacter[index] = std::max(context.bestSkillByCharacter[index], double(card.skill.max));
        }
        return context;
    };
    auto runDfsExact = [&](const DeckRecommendConfig& runConfig, const std::vector<CardDetail>& sourceCards, RecommendCalcInfo& info, bool useCharacterBounds = false) {
        auto sortedCards = sortCardsByStrength(sourceCards);
        initDfsState(info);
        std::optional<DfsScoreUpperBoundContext> scoreUpperBoundContext = std::nullopt;
        if (runConfig.target == RecommendTarget::Score || runConfig.target == RecommendTarget::Skill) {
            scoreUpperBoundContext = useCharacterBounds
                ? buildDfsScoreUpperBoundContext(sortedCards)
                : DfsScoreUpperBoundContext{ .musicMeta = musicMeta };
        }
        auto searchStart = std::chrono::high_resolution_clock::now();
        findBestCardsDFS(
            liveType, runConfig, sortedCards, supportCards, sf,
            info,
            runConfig.limit, Enums::LiveType::isChallenge(liveType), runConfig.member, honorBonus,
            eventConfig.eventType, eventConfig.eventId, fixedCards,
            scoreUpperBoundContext.has_value() ? &scoreUpperBoundContext.value() : nullptr,
            &evalCache
        );
        timings.searchNs += std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now() - searchStart
        ).count();
    };
    auto tuneGaConfig = [&](DeckRecommendConfig runConfig, std::size_t candidateCount, bool warmupOnly, bool seeded) {
        int minPop = warmupOnly ? 600 : 2000;
        int maxPop = warmupOnly ? 2400 : 8000;
        int perCard = warmupOnly ? 6 : (seeded ? 8 : 10);
        int desiredPop = int(candidateCount) * perCard;
        desiredPop = std::max(minPop, std::min(maxPop, desiredPop));
        runConfig.gaPopSize = std::min(runConfig.gaPopSize, desiredPop);
        runConfig.gaParentSize = std::min(runConfig.gaParentSize, std::max(64, runConfig.gaPopSize / 6));
        runConfig.gaEliteSize = std::min(runConfig.gaEliteSize, std::max(0, runConfig.gaPopSize / 20));
        runConfig.gaMaxIter = std::min(runConfig.gaMaxIter, warmupOnly ? 48 : (seeded ? 192 : 256));
        runConfig.gaMaxIterNoImprove = std::min(runConfig.gaMaxIterNoImprove, warmupOnly ? 3 : 6);
        return runConfig;
    };
    auto runGaSearch = [&](const DeckRecommendConfig& runConfig, const std::vector<CardDetail>& sourceCards, RecommendCalcInfo& info, const std::vector<std::vector<const CardDetail*>>* seedDecks = nullptr) {
        auto sortedCards = sortCardsByStrength(sourceCards);
        long long seed = runConfig.gaSeed;
        if (seed == -1) {
            seed = nowNs();
        }
        auto rng = Rng(seed);
        auto searchStart = std::chrono::high_resolution_clock::now();
        findBestCardsGA(
            liveType, runConfig, rng, sortedCards, supportCards, sf,
            info,
            runConfig.limit, Enums::LiveType::isChallenge(liveType), runConfig.member, honorBonus,
            eventConfig.eventType, eventConfig.eventId, fixedCards, seedDecks, nullptr
        );
        timings.searchNs += std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now() - searchStart
        ).count();
    };
    auto collectSeedDecks = [&](const RecommendCalcInfo& info, const std::vector<CardDetail>& sourceCards, int maxSeedCount) {
        std::unordered_map<int, const CardDetail*> cardById{};
        for (const auto& card : sourceCards) {
            cardById.emplace(card.cardId, &card);
        }
        std::vector<std::vector<const CardDetail*>> seeds{};
        auto decks = collectResults(info);
        int count = 0;
        for (const auto& deck : decks) {
            if (count++ >= maxSeedCount) {
                break;
            }
            std::vector<const CardDetail*> seedDeck{};
            seedDeck.reserve(deck.cards.size());
            bool valid = true;
            for (const auto& card : deck.cards) {
                auto it = cardById.find(card.cardId);
                if (it == cardById.end()) {
                    valid = false;
                    break;
                }
                seedDeck.push_back(it->second);
            }
            if (valid && int(seedDeck.size()) == config.member) {
                seeds.push_back(std::move(seedDeck));
            }
        }
        return seeds;
    };
    auto collectChallengeMonoAttrSeedDecks = [&](const DeckRecommendConfig& runConfig, RecommendCalcInfo* warmupInfo, int maxSeedCount) {
        std::vector<std::vector<const CardDetail*>> seeds{};
        if (!Enums::LiveType::isChallenge(liveType) || runConfig.member < 5) {
            return seeds;
        }

        std::optional<int> fixedAttr = std::nullopt;
        for (const auto& fixedCard : fixedCards) {
            if (!fixedAttr.has_value()) {
                fixedAttr = fixedCard.attr;
            } else if (fixedAttr.value() != fixedCard.attr) {
                return seeds;
            }
        }

        std::unordered_map<int, std::vector<CardDetail>> attrBuckets{};
        std::unordered_map<int, const CardDetail*> stableCardById{};
        for (const auto& card : cards) {
            stableCardById.emplace(card.cardId, &card);
            if (fixedAttr.has_value() && card.attr != fixedAttr.value()) {
                continue;
            }
            attrBuckets[card.attr].push_back(card);
        }

        int perAttrBudgetMs = std::min(runConfig.timeout_ms, resolveBudgetMs(runConfig.timeout_ms, 0.05, 12, 60));
        std::unordered_set<uint64_t> seedHashes{};
        for (auto& [attr, bucket] : attrBuckets) {
            if (fixedAttr.has_value() && attr != fixedAttr.value()) {
                continue;
            }
            if (int(bucket.size()) < runConfig.member || !canMakeDeck(liveType, eventConfig.eventType, bucket, runConfig.member)) {
                continue;
            }

            auto attrInfo = makeCalcInfo(perAttrBudgetMs);
            runDfsExact(runConfig, bucket, attrInfo);
            if (warmupInfo != nullptr) {
                mergeCalcInfo(*warmupInfo, attrInfo);
            }
            for (auto& seedDeck : collectSeedDecks(attrInfo, bucket, maxSeedCount)) {
                if (int(seedDeck.size()) != runConfig.member) {
                    continue;
                }
                std::vector<const CardDetail*> stableSeedDeck{};
                stableSeedDeck.reserve(seedDeck.size());
                bool valid = true;
                for (const auto* card : seedDeck) {
                    if (card == nullptr) {
                        valid = false;
                        break;
                    }
                    auto it = stableCardById.find(card->cardId);
                    if (it == stableCardById.end()) {
                        valid = false;
                        break;
                    }
                    stableSeedDeck.push_back(it->second);
                }
                if (!valid) {
                    continue;
                }
                auto hash = this->calcDeckHash(stableSeedDeck);
                if (seedHashes.insert(hash).second) {
                    seeds.push_back(std::move(stableSeedDeck));
                }
            }
        }
        return seeds;
    };
    auto buildGaPrunedCards = [&](const std::vector<CardDetail>& sortedCards, const RecommendCalcInfo& gaInfo) {
        std::unordered_map<int, const CardDetail*> cardById{};
        for (const auto& card : sortedCards) {
            cardById.emplace(card.cardId, &card);
        }

        std::vector<CardDetail> pruned{};
        std::unordered_set<int> selectedIds{};
        auto tryAdd = [&](const CardDetail& card) {
            if (selectedIds.insert(card.cardId).second) {
                pruned.push_back(card);
            }
        };

        for (const auto& card : fixedCards) {
            tryAdd(card);
        }
        for (const auto& card : sortedCards) {
            if (requiredCharacterSet.count(card.characterId)) {
                tryAdd(card);
            }
        }

        auto gaDecks = collectResults(gaInfo);
        int deckLimit = std::max(config.limit * 4, 8);
        int usedDecks = 0;
        for (const auto& deck : gaDecks) {
            if (usedDecks++ >= deckLimit) {
                break;
            }
            for (const auto& card : deck.cards) {
                auto it = cardById.find(card.cardId);
                if (it != cardById.end()) {
                    tryAdd(*it->second);
                }
            }
        }

        std::array<int, 32> perCharacterCount{};
        int perCharacterKeep = Enums::LiveType::isChallenge(liveType) ? std::max(config.member, 3) : 3;
        for (const auto& card : sortedCards) {
            auto& count = perCharacterCount[card.characterId];
            if (count < perCharacterKeep) {
                tryAdd(card);
                count++;
            }
        }

        int globalKeep = std::min(int(sortedCards.size()), std::max(config.member * 12, 60));
        for (int i = 0; i < globalKeep; ++i) {
            tryAdd(sortedCards[i]);
        }

        if (config.target == RecommendTarget::Mysekai || config.target == RecommendTarget::Bonus) {
            auto eventSorted = sortedCards;
            std::sort(eventSorted.begin(), eventSorted.end(), [&](const CardDetail& a, const CardDetail& b) {
                return std::make_tuple(cardEventBonus(a), a.power.max, a.skill.max, a.cardId)
                    > std::make_tuple(cardEventBonus(b), b.power.max, b.skill.max, b.cardId);
            });
            int eventKeep = config.target == RecommendTarget::Bonus
                ? std::min(int(eventSorted.size()), std::max(config.member * 24, 120))
                : std::min(int(eventSorted.size()), std::max(config.member * 18, 90));
            for (int i = 0; i < eventKeep; ++i) {
                tryAdd(eventSorted[i]);
            }

            std::array<int, 32> perCharacterEventCount{};
            int perCharacterEventKeep = config.target == RecommendTarget::Bonus
                ? (Enums::LiveType::isChallenge(liveType) ? std::max(config.member, 3) : 5)
                : (Enums::LiveType::isChallenge(liveType) ? std::max(config.member, 3) : 4);
            for (const auto& card : eventSorted) {
                auto& count = perCharacterEventCount[card.characterId];
                if (count < perCharacterEventKeep) {
                    tryAdd(card);
                    count++;
                }
            }
        }

        auto candidate = pruned;
        for (const auto& card : sortedCards) {
            if (canMakeDeck(liveType, eventConfig.eventType, candidate, config.member)) {
                break;
            }
            tryAdd(card);
            candidate = pruned;
        }

        auto minKeep = std::min(sortedCards.size(), std::size_t(std::max(config.member * 10, 40)));
        for (const auto& card : sortedCards) {
            if (pruned.size() >= minKeep) {
                break;
            }
            tryAdd(card);
        }

        candidate = pruned;
        if (!canMakeDeck(liveType, eventConfig.eventType, candidate, config.member)) {
            return sortedCards;
        }
        return pruned;
    };
    auto buildSeededRefineCards = [&](
        const std::vector<CardDetail>& sortedCards,
        const RecommendCalcInfo& seedInfo,
        const std::vector<std::vector<const CardDetail*>>* extraSeedDecks = nullptr
    ) {
        auto pruned = buildGaPrunedCards(sortedCards, seedInfo);
        std::unordered_set<int> selectedIds{};
        for (const auto& card : pruned) {
            selectedIds.insert(card.cardId);
        }
        std::unordered_map<int, const CardDetail*> cardById{};
        for (const auto& card : sortedCards) {
            cardById.emplace(card.cardId, &card);
        }
        auto tryAdd = [&](const CardDetail& card) {
            if (selectedIds.insert(card.cardId).second) {
                pruned.push_back(card);
            }
        };

        if (extraSeedDecks != nullptr) {
            for (const auto& deck : *extraSeedDecks) {
                for (const auto* card : deck) {
                    auto it = cardById.find(card->cardId);
                    if (it != cardById.end()) {
                        tryAdd(*it->second);
                    }
                }
            }
        }

        if (config.target == RecommendTarget::Score) {
            auto skillSorted = sortedCards;
            std::sort(skillSorted.begin(), skillSorted.end(), [](const CardDetail& a, const CardDetail& b) {
                return std::make_tuple(a.skill.max, a.skill.min, a.cardId)
                    > std::make_tuple(b.skill.max, b.skill.min, b.cardId);
            });

            int skillKeep = std::min(int(skillSorted.size()), std::max(config.member * 14, 64));
            for (int i = 0; i < skillKeep; ++i) {
                tryAdd(skillSorted[i]);
            }

            std::array<int, 32> perCharacterSkillCount{};
            int perCharacterSkillKeep = Enums::LiveType::isChallenge(liveType) ? std::max(config.member, 3) : 3;
            for (const auto& card : skillSorted) {
                auto& count = perCharacterSkillCount[card.characterId];
                if (count < perCharacterSkillKeep) {
                    tryAdd(card);
                    count++;
                }
            }

            auto eventSorted = sortedCards;
            std::sort(eventSorted.begin(), eventSorted.end(), [](const CardDetail& a, const CardDetail& b) {
                return std::make_tuple(
                        a.maxEventBonus.value_or(0.0),
                        a.skill.max,
                        a.power.max,
                        a.cardId
                    )
                    > std::make_tuple(
                        b.maxEventBonus.value_or(0.0),
                        b.skill.max,
                        b.power.max,
                        b.cardId
                    );
            });
            int eventKeep = std::min(int(eventSorted.size()), std::max(config.member * 12, 56));
            for (int i = 0; i < eventKeep; ++i) {
                tryAdd(eventSorted[i]);
            }

            auto supportSorted = sortedCards;
            std::sort(supportSorted.begin(), supportSorted.end(), [](const CardDetail& a, const CardDetail& b) {
                return std::make_tuple(
                        a.supportDeckBonus.value_or(0.0),
                        a.skill.max,
                        a.power.max,
                        a.cardId
                    )
                    > std::make_tuple(
                        b.supportDeckBonus.value_or(0.0),
                        b.skill.max,
                        b.power.max,
                        b.cardId
                    );
            });
            int supportKeep = std::min(int(supportSorted.size()), std::max(config.member * 8, 36));
            for (int i = 0; i < supportKeep; ++i) {
                tryAdd(supportSorted[i]);
            }

            auto minKeep = std::min(sortedCards.size(), std::size_t(std::max(config.member * 18, 80)));
            for (const auto& card : sortedCards) {
                if (pruned.size() >= minKeep) {
                    break;
                }
                tryAdd(card);
            }
        } else if (config.target == RecommendTarget::Mysekai || config.target == RecommendTarget::Bonus) {
            auto eventSorted = sortedCards;
            std::sort(eventSorted.begin(), eventSorted.end(), [&](const CardDetail& a, const CardDetail& b) {
                return std::make_tuple(cardEventBonus(a), a.power.max, a.skill.max, a.cardId)
                    > std::make_tuple(cardEventBonus(b), b.power.max, b.skill.max, b.cardId);
            });
            int eventKeep = config.target == RecommendTarget::Bonus
                ? std::min(int(eventSorted.size()), std::max(config.member * 30, 150))
                : std::min(int(eventSorted.size()), std::max(config.member * 24, 120));
            for (int i = 0; i < eventKeep; ++i) {
                tryAdd(eventSorted[i]);
            }

            std::array<int, 32> perCharacterEventCount{};
            int perCharacterEventKeep = config.target == RecommendTarget::Bonus
                ? (Enums::LiveType::isChallenge(liveType) ? std::max(config.member, 3) : 6)
                : (Enums::LiveType::isChallenge(liveType) ? std::max(config.member, 3) : 5);
            for (const auto& card : eventSorted) {
                auto& count = perCharacterEventCount[card.characterId];
                if (count < perCharacterEventKeep) {
                    tryAdd(card);
                    count++;
                }
            }

            auto heuristicSorted = sortedCards;
            std::sort(heuristicSorted.begin(), heuristicSorted.end(), [&](const CardDetail& a, const CardDetail& b) {
                return std::make_tuple(scoreHeuristic(a), cardEventBonus(a), a.power.max, a.cardId)
                    > std::make_tuple(scoreHeuristic(b), cardEventBonus(b), b.power.max, b.cardId);
            });
            int heuristicKeep = std::min(int(heuristicSorted.size()), std::max(config.member * 28, 140));
            for (int i = 0; i < heuristicKeep; ++i) {
                tryAdd(heuristicSorted[i]);
            }

            auto minKeep = config.target == RecommendTarget::Bonus
                ? std::min(sortedCards.size(), std::size_t(std::max(config.member * 30, 150)))
                : std::min(sortedCards.size(), std::size_t(std::max(config.member * 24, 120)));
            for (const auto& card : sortedCards) {
                if (pruned.size() >= minKeep) {
                    break;
                }
                tryAdd(card);
            }
        }

        auto candidate = pruned;
        for (const auto& card : sortedCards) {
            if (canMakeDeck(liveType, eventConfig.eventType, candidate, config.member)) {
                break;
            }
            tryAdd(card);
            candidate = pruned;
        }
        if (!canMakeDeck(liveType, eventConfig.eventType, candidate, config.member)) {
            return sortedCards;
        }
        return pruned;
    };
    auto attachSupportDeckCards = [&](std::vector<RecommendDeck> decks) {
        auto decorateStart = std::chrono::high_resolution_clock::now();
        if (supportCards.empty()) {
            maybePrintTimings(timings, evalCache);
            return decks;
        }
        std::unordered_map<int, const CardDetail*> cardById{};
        for (const auto& card : cards) {
            cardById.emplace(card.cardId, &card);
        }
        for (auto& deck : decks) {
            std::vector<const CardDetail*> deckCardDetails{};
            deckCardDetails.reserve(deck.cards.size());
            bool valid = true;
            for (const auto& card : deck.cards) {
                auto it = cardById.find(card.cardId);
                if (it == cardById.end()) {
                    valid = false;
                    break;
                }
                deckCardDetails.push_back(it->second);
            }
            if (!valid || deckCardDetails.empty()) {
                continue;
            }

            const std::vector<SupportDeckCard>* pSupportCards = nullptr;
            if (eventConfig.isWorldBloomFinale) {
                auto it = supportCards.find(deckCardDetails[0]->characterId);
                if (it == supportCards.end()) {
                    continue;
                }
                pSupportCards = &it->second;
            } else {
                pSupportCards = &supportCards.begin()->second;
            }

            auto supportDeckBonus = deckCalculator.getSupportDeckBonus(
                deckCardDetails,
                *pSupportCards,
                deckCalculator.getWorldBloomSupportDeckCount(eventConfig.eventId),
                true
            );
            deck.supportDeckBonus = supportDeckBonus.bonus;
            deck.supportDeckCards = std::move(supportDeckBonus.cards);
        }
        timings.finalDecorateNs += std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now() - decorateStart
        ).count();
        maybePrintTimings(timings, evalCache);
        return decks;
    };
    auto ensureResults = [&](std::vector<RecommendDeck> decks) {
        if (decks.empty()) {
            throw std::runtime_error("Cannot recommend any deck in " + std::to_string(cards.size()) + " cards");
        }
        return attachSupportDeckCards(std::move(decks));
    };

    // 指定活动加成组卡
    if (config.target == RecommendTarget::Bonus && !config.bonusList.empty()) {
        auto calcInfo = makeCalcInfo(config.timeout_ms);
        std::vector<RecommendDeck> ans{};
        if (eventConfig.eventType == 0) 
            throw std::runtime_error("Bonus target requires event");
        if (config.algorithm != RecommendAlgorithm::DFS) 
            throw std::runtime_error("Bonus target only supports DFS algorithm");

        // WL和普通活动采用不同代码
        auto searchStart = std::chrono::high_resolution_clock::now();
        if (eventConfig.eventType != Enums::EventType::world_bloom) {
            findTargetBonusCardsDFS(
                liveType, config, cards, sf, calcInfo,
                config.limit, config.member, eventConfig.eventType, eventConfig.eventId, &evalCache
            );
        } else {
            findWorldBloomTargetBonusCardsDFS(
                liveType, config, cards, sf, calcInfo,
                config.limit, config.member, eventConfig.eventType, eventConfig.eventId, &evalCache
            );
        }
        timings.searchNs += std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now() - searchStart
        ).count();

        while (calcInfo.deckQueue.size()) {
            ans.emplace_back(calcInfo.deckQueue.top());
            calcInfo.deckQueue.pop();
        }
        // 按照活动加成从小到大排序，同加成按分数从小到大排序
        std::sort(ans.begin(), ans.end(), [](const RecommendDeck& a, const RecommendDeck& b) {
            return std::tuple(-a.eventBonus.value_or(0), a.targetValue) > std::tuple(-b.eventBonus.value_or(0), b.targetValue);
        });
        return attachSupportDeckCards(std::move(ans));
    }
    auto runDfsGaHybrid = [&](const DeckRecommendConfig& baseConfig) {
        auto hybridInfo = makeCalcInfo(baseConfig.timeout_ms);
        auto fullSorted = sortCardsByStrength(cards);

        int dfsBudgetMs = std::min(baseConfig.timeout_ms, resolveBudgetMs(baseConfig.timeout_ms, 0.15, 20, 150));
        RecommendCalcInfo seedInfo{};
        seedInfo.start_ts = hybridInfo.start_ts;
        seedInfo.timeout = timeoutToNs(dfsBudgetMs);

        std::vector<CardDetail> seedCards = cards;
        std::vector<CardDetail> seedPrev{};
        seedCards = filterCardPriority(liveType, eventConfig.eventType, seedCards, seedPrev, baseConfig.member);
        runDfsExact(baseConfig, seedCards, seedInfo, true);
        mergeCalcInfo(hybridInfo, seedInfo);

        auto seedDecks = collectSeedDecks(seedInfo, fullSorted, std::max(baseConfig.limit * 3, 8));
        auto gaConfig = tuneGaConfig(baseConfig, fullSorted.size(), false, true);
        runGaSearch(gaConfig, fullSorted, hybridInfo, seedDecks.empty() ? nullptr : &seedDecks);
        return ensureResults(collectResults(hybridInfo));
    };

    if (config.algorithm == RecommendAlgorithm::SA) {
        auto calcInfo = makeCalcInfo(config.timeout_ms);
        auto sortedCards = sortCardsByStrength(cards);
        long long seed = config.saSeed;
        if (seed == -1) {
            seed = nowNs();
        }
        auto rng = Rng(seed);
        for (int i = 0; i < config.saRunCount && !calcInfo.isTimeout(); ++i) {
            auto searchStart = std::chrono::high_resolution_clock::now();
            findBestCardsSA(
                liveType, config, rng, sortedCards, supportCards, sf,
                calcInfo,
                config.limit, Enums::LiveType::isChallenge(liveType), config.member, honorBonus,
                eventConfig.eventType, eventConfig.eventId, fixedCards, nullptr
            );
            timings.searchNs += std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::high_resolution_clock::now() - searchStart
            ).count();
        }
        return ensureResults(collectResults(calcInfo));
    }

    if (config.algorithm == RecommendAlgorithm::GA) {
        auto calcInfo = makeCalcInfo(config.timeout_ms);
        auto monoAttrSeedDecks = collectChallengeMonoAttrSeedDecks(config, &calcInfo, std::max(config.limit, 2));
        runGaSearch(config, cards, calcInfo, monoAttrSeedDecks.empty() ? nullptr : &monoAttrSeedDecks);
        return ensureResults(collectResults(calcInfo));
    }

    if (config.algorithm == RecommendAlgorithm::DFS_GA) {
        return runDfsGaHybrid(config);
    }

    if (config.algorithm == RecommendAlgorithm::RL) {
        constexpr int RL_FEATURE_DIM = 13;
        struct RlStepTrace {
            std::array<double, RL_FEATURE_DIM> chosen{};
            std::array<double, RL_FEATURE_DIM> mean{};
        };
        struct RlStoredSeed {
            std::array<int, 5> cardIds{};
            int size = 0;
            double targetValue = -1e18;
            uint64_t hash = 0;
        };
        struct RlPolicyBucket {
            std::array<double, RL_FEATURE_DIM> weights = {
                0.25, 0.90, 1.10, 0.45, 0.35,
                0.90, 0.30, 0.25, -0.12, 0.75,
                0.55, 0.25, 0.95
            };
            double baselineReward = 0.0;
            int episodes = 0;
        };
        struct RlSeedBucket {
            double bestTargetValue = -1e18;
            std::vector<RlStoredSeed> bestSeeds{};
        };
        static std::unordered_map<std::string, RlPolicyBucket> rlPolicyBuckets{};
        static std::unordered_map<std::string, RlSeedBucket> rlSeedBuckets{};
        static std::mutex rlMemoryMutex;
        static bool rlSeedCacheLoaded = false;

        auto calcStoredSeedHash = [](const std::array<int, 5>& cardIds, int size) {
            auto ids = cardIds;
            std::sort(ids.begin() + 1, ids.begin() + size);
            constexpr uint64_t base = 10007;
            uint64_t hash = 0;
            for (int i = 0; i < size; ++i) {
                hash = hash * base + ids[i];
            }
            return hash;
        };
        auto normalizeSeedBucket = [](RlSeedBucket& bucket, std::size_t maxSeedCount) {
            std::sort(bucket.bestSeeds.begin(), bucket.bestSeeds.end(), [](const RlStoredSeed& a, const RlStoredSeed& b) {
                if (a.targetValue != b.targetValue) {
                    return a.targetValue > b.targetValue;
                }
                return a.hash < b.hash;
            });
            std::vector<RlStoredSeed> uniqueSeeds{};
            uniqueSeeds.reserve(bucket.bestSeeds.size());
            for (const auto& seed : bucket.bestSeeds) {
                auto it = std::find_if(uniqueSeeds.begin(), uniqueSeeds.end(), [&](const RlStoredSeed& existing) {
                    return existing.hash == seed.hash;
                });
                if (it == uniqueSeeds.end()) {
                    uniqueSeeds.push_back(seed);
                } else if (seed.targetValue > it->targetValue) {
                    *it = seed;
                }
            }
            bucket.bestSeeds = std::move(uniqueSeeds);
            if (bucket.bestSeeds.size() > maxSeedCount) {
                bucket.bestSeeds.resize(maxSeedCount);
            }
            bucket.bestTargetValue = -1e18;
            for (const auto& seed : bucket.bestSeeds) {
                bucket.bestTargetValue = std::max(bucket.bestTargetValue, seed.targetValue);
            }
        };
        auto mergeSeedBucket = [&](RlSeedBucket& target, const RlSeedBucket& source, std::size_t maxSeedCount) {
            for (const auto& seed : source.bestSeeds) {
                if (seed.size <= 0 || seed.targetValue <= -1e17) {
                    continue;
                }
                auto it = std::find_if(target.bestSeeds.begin(), target.bestSeeds.end(), [&](const RlStoredSeed& existing) {
                    return existing.hash == seed.hash;
                });
                if (it == target.bestSeeds.end()) {
                    target.bestSeeds.push_back(seed);
                } else if (seed.targetValue > it->targetValue) {
                    *it = seed;
                }
            }
            target.bestTargetValue = std::max(target.bestTargetValue, source.bestTargetValue);
            normalizeSeedBucket(target, maxSeedCount);
        };
        auto rlSeedCachePath = []() {
            const char* disabled = std::getenv("DECK_RL_SEED_CACHE_DISABLE");
            if (disabled && std::string(disabled) == "1") {
                return std::string();
            }
            const char* explicitPath = std::getenv("DECK_RL_SEED_CACHE_FILE");
            if (explicitPath && explicitPath[0] != '\0') {
                return std::string(explicitPath);
            }
            const char* dataDir = std::getenv("DECK_DATA_DIR");
            if (dataDir && dataDir[0] != '\0') {
                return std::string(dataDir) + "/rl_seed_cache.tsv";
            }
            return std::string();
        };
        auto loadPersistentSeedBuckets = [&](const std::string& path) {
            if (path.empty()) {
                return;
            }
            std::ifstream in(path);
            if (!in) {
                return;
            }

            std::string line;
            while (std::getline(in, line)) {
                if (line.empty() || line[0] == '#') {
                    continue;
                }
                try {
                    std::stringstream ss(line);
                    std::string version;
                    std::string key;
                    std::string bucketBestValue;
                    std::string seedTargetValue;
                    std::string sizeValue;
                    std::string cardsValue;
                    if (!std::getline(ss, version, '\t')
                        || !std::getline(ss, key, '\t')
                        || !std::getline(ss, bucketBestValue, '\t')
                        || !std::getline(ss, seedTargetValue, '\t')
                        || !std::getline(ss, sizeValue, '\t')
                        || !std::getline(ss, cardsValue, '\t')) {
                        continue;
                    }
                    if (version != "v1" || key.find(":transfer_seeds") == std::string::npos) {
                        continue;
                    }

                    RlStoredSeed seed{};
                    seed.targetValue = std::stod(seedTargetValue);
                    seed.size = std::stoi(sizeValue);
                    if (seed.size <= 0 || seed.size > int(seed.cardIds.size())) {
                        continue;
                    }

                    std::stringstream cardStream(cardsValue);
                    std::string cardIdValue;
                    int index = 0;
                    while (std::getline(cardStream, cardIdValue, ',') && index < seed.size) {
                        seed.cardIds[index++] = std::stoi(cardIdValue);
                    }
                    if (index != seed.size) {
                        continue;
                    }
                    seed.hash = calcStoredSeedHash(seed.cardIds, seed.size);

                    auto& bucket = rlSeedBuckets[key];
                    bucket.bestTargetValue = std::max(bucket.bestTargetValue, std::stod(bucketBestValue));
                    bucket.bestSeeds.push_back(seed);
                } catch (...) {
                    continue;
                }
            }

            for (auto& [key, bucket] : rlSeedBuckets) {
                if (key.find(":transfer_seeds") != std::string::npos) {
                    normalizeSeedBucket(bucket, 32);
                }
            }
        };
        auto savePersistentSeedBuckets = [&](const std::string& path) {
            if (path.empty()) {
                return;
            }

            std::string tmpPath = path + ".tmp";
            std::ofstream out(tmpPath, std::ios::trunc);
            if (!out) {
                return;
            }
            out << "# deck-service RL transfer seed cache v1\n";

            std::size_t bucketCount = 0;
            constexpr std::size_t maxPersistentBuckets = 512;
            for (const auto& [key, bucket] : rlSeedBuckets) {
                if (key.find(":transfer_seeds") == std::string::npos || bucket.bestSeeds.empty()) {
                    continue;
                }
                if (bucketCount++ >= maxPersistentBuckets) {
                    break;
                }
                for (const auto& seed : bucket.bestSeeds) {
                    if (seed.size <= 0 || seed.targetValue <= -1e17) {
                        continue;
                    }
                    out << "v1\t" << key << '\t'
                        << bucket.bestTargetValue << '\t'
                        << seed.targetValue << '\t'
                        << seed.size << '\t';
                    for (int i = 0; i < seed.size; ++i) {
                        if (i) {
                            out << ',';
                        }
                        out << seed.cardIds[i];
                    }
                    out << '\n';
                }
            }
            out.close();
            if (!out) {
                std::remove(tmpPath.c_str());
                return;
            }
            std::remove(path.c_str());
            std::rename(tmpPath.c_str(), path.c_str());
        };

        auto fullSorted = sortCardsByStrength(cards);
        auto remainingRequiredCharacters = resolveRemainingFixedCharacters(
            config,
            fixedCards,
            eventConfig.isWorldBloomFinale,
            eventConfig.specialCharacterId
        );
        auto leaderCharacterId = resolveLeaderCharacterId(
            config,
            eventConfig.isWorldBloomFinale,
            eventConfig.specialCharacterId
        );
        auto appendConstraintKey = [](std::string& key, const char* name, const std::vector<int>& values, bool sortValues) {
            key += ":";
            key += name;
            key += "=";
            if (values.empty()) {
                key += "-";
                return;
            }
            auto normalized = values;
            if (sortValues) {
                std::sort(normalized.begin(), normalized.end());
            }
            for (std::size_t i = 0; i < normalized.size(); ++i) {
                if (i) {
                    key += ",";
                }
                key += std::to_string(normalized[i]);
            }
        };
        auto appendScalarKey = [](std::string& key, const char* name, const std::string& value) {
            key += ":";
            key += name;
            key += "=";
            key += value;
        };
        auto deckMatchesFixedConstraints = [&](const std::vector<const CardDetail*>& deck) {
            if (int(deck.size()) != config.member) {
                return false;
            }

            std::unordered_set<int> cardIds{};
            std::unordered_set<int> characterIds{};
            for (const auto* card : deck) {
                if (!card) {
                    return false;
                }
                if (!cardIds.insert(card->cardId).second) {
                    return false;
                }
                if (!Enums::LiveType::isChallenge(liveType) && !characterIds.insert(card->characterId).second) {
                    return false;
                }
            }

            for (const auto& fixedCard : fixedCards) {
                if (!cardIds.count(fixedCard.cardId)) {
                    return false;
                }
            }
            if (leaderCharacterId.has_value()) {
                if (!characterIds.count(leaderCharacterId.value())) {
                    return false;
                }
            }
            for (const auto characterId : requiredCharacters) {
                if (!characterIds.count(characterId)) {
                    return false;
                }
            }
            return true;
        };
        auto normalizeDeckForLeader = [&](std::vector<const CardDetail*> deck) {
            if (!config.fixedCards.empty()) {
                if (!applyFixedCardOrder(deck, config.fixedCards)) {
                    return std::vector<const CardDetail*>{};
                }
            }
            if (leaderCharacterId.has_value()) {
                auto leaderIt = std::find_if(
                    deck.begin(),
                    deck.end(),
                    [&](const CardDetail* card) {
                        return card && card->characterId == leaderCharacterId.value();
                    }
                );
                if (leaderIt != deck.end() && leaderIt != deck.begin()) {
                    std::rotate(deck.begin(), leaderIt, leaderIt + 1);
                }
            }
            return deck;
        };
        auto buildRlCandidateCards = [&](const std::vector<CardDetail>& sortedCards) {
            std::vector<CardDetail> pruned{};
            std::unordered_set<int> selectedIds{};
            auto tryAdd = [&](const CardDetail& card) {
                if (selectedIds.insert(card.cardId).second) {
                    pruned.push_back(card);
                }
            };

            for (const auto& card : fixedCards) {
                tryAdd(card);
            }
            for (const auto& card : sortedCards) {
                if (requiredCharacterSet.count(card.characterId)) {
                    tryAdd(card);
                }
            }

            std::array<int, 32> perCharacterCount{};
            int perCharacterKeep = Enums::LiveType::isChallenge(liveType)
                ? std::max(config.member + 1, 4)
                : (config.target == RecommendTarget::Score ? 6 : 4);
            for (const auto& card : sortedCards) {
                auto& count = perCharacterCount[card.characterId];
                if (count < perCharacterKeep) {
                    tryAdd(card);
                    count++;
                }
            }

            int globalKeep = std::min(
                int(sortedCards.size()),
                std::max(config.member * (config.target == RecommendTarget::Score ? 28 : 20), config.target == RecommendTarget::Score ? 140 : 96)
            );
            for (int i = 0; i < globalKeep; ++i) {
                tryAdd(sortedCards[i]);
            }

            if (config.target == RecommendTarget::Mysekai) {
                auto eventSorted = sortedCards;
                std::sort(eventSorted.begin(), eventSorted.end(), [&](const CardDetail& a, const CardDetail& b) {
                    return std::make_tuple(cardEventBonus(a), a.power.max, a.cardId)
                        > std::make_tuple(cardEventBonus(b), b.power.max, b.cardId);
                });
                int eventKeep = std::min(int(eventSorted.size()), std::max(config.member * 26, 130));
                for (int i = 0; i < eventKeep; ++i) {
                    tryAdd(eventSorted[i]);
                }

                std::array<int, 32> perCharacterEventCount{};
                int perCharacterEventKeep = Enums::LiveType::isChallenge(liveType) ? std::max(config.member + 1, 4) : 6;
                for (const auto& card : eventSorted) {
                    auto& count = perCharacterEventCount[card.characterId];
                    if (count < perCharacterEventKeep) {
                        tryAdd(card);
                        count++;
                    }
                }

                auto heuristicSorted = sortedCards;
                std::sort(heuristicSorted.begin(), heuristicSorted.end(), [&](const CardDetail& a, const CardDetail& b) {
                    return std::make_tuple(scoreHeuristic(a), cardEventBonus(a), a.power.max, a.cardId)
                        > std::make_tuple(scoreHeuristic(b), cardEventBonus(b), b.power.max, b.cardId);
                });
                int heuristicKeep = std::min(int(heuristicSorted.size()), std::max(config.member * 30, 150));
                for (int i = 0; i < heuristicKeep; ++i) {
                    tryAdd(heuristicSorted[i]);
                }
            }

            if (config.target != RecommendTarget::Skill && config.target != RecommendTarget::Mysekai) {
                auto skillSorted = sortedCards;
                std::sort(skillSorted.begin(), skillSorted.end(), [](const CardDetail& a, const CardDetail& b) {
                    return std::make_tuple(a.skill.max, a.skill.min, a.cardId)
                        > std::make_tuple(b.skill.max, b.skill.min, b.cardId);
                });
                int skillKeep = std::min(
                    int(skillSorted.size()),
                    std::max(config.member * (config.target == RecommendTarget::Score ? 16 : 12), config.target == RecommendTarget::Score ? 72 : 48)
                );
                for (int i = 0; i < skillKeep; ++i) {
                    tryAdd(skillSorted[i]);
                }
            }

            if (config.target == RecommendTarget::Score) {
                auto eventSorted = sortedCards;
                std::sort(eventSorted.begin(), eventSorted.end(), [](const CardDetail& a, const CardDetail& b) {
                    return std::make_tuple(
                            a.maxEventBonus.value_or(0.0),
                            a.skill.max,
                            a.power.max,
                            a.cardId
                        )
                        > std::make_tuple(
                            b.maxEventBonus.value_or(0.0),
                            b.skill.max,
                            b.power.max,
                            b.cardId
                        );
                });
                int eventKeep = std::min(int(eventSorted.size()), std::max(config.member * 14, 56));
                for (int i = 0; i < eventKeep; ++i) {
                    tryAdd(eventSorted[i]);
                }

                auto supportSorted = sortedCards;
                std::sort(supportSorted.begin(), supportSorted.end(), [](const CardDetail& a, const CardDetail& b) {
                    return std::make_tuple(
                            a.supportDeckBonus.value_or(0.0),
                            a.skill.max,
                            a.power.max,
                            a.cardId
                        )
                        > std::make_tuple(
                            b.supportDeckBonus.value_or(0.0),
                            b.skill.max,
                            b.power.max,
                            b.cardId
                        );
                });
                int supportKeep = std::min(int(supportSorted.size()), std::max(config.member * 10, 40));
                for (int i = 0; i < supportKeep; ++i) {
                    tryAdd(supportSorted[i]);
                }
            }

            std::vector<CardDetail> candidate = pruned;
            for (const auto& card : sortedCards) {
                if (canMakeDeck(liveType, eventConfig.eventType, candidate, config.member)) {
                    break;
                }
                tryAdd(card);
                candidate = pruned;
            }

            if (!canMakeDeck(liveType, eventConfig.eventType, candidate, config.member)) {
                return sortedCards;
            }
            return pruned;
        };
        auto evaluateDeckByCards = [&](const DeckRecommendConfig& runConfig, const std::vector<const CardDetail*>& deck, RecommendCalcInfo& info) {
            auto normalizedDeck = normalizeDeckForLeader(deck);
            if (normalizedDeck.empty()) {
                return -1e18;
            }
            if (!deckMatchesFixedConstraints(normalizedDeck)) {
                return -1e18;
            }
            auto deckHash = this->calcDeckHash(normalizedDeck);
            if (info.deckTargetValueMap.count(deckHash)) {
                return info.deckTargetValueMap[deckHash];
            }
            auto ret = getBestPermutation(
                this->deckCalculator, normalizedDeck, supportCards, sf,
                honorBonus, eventConfig.eventType, eventConfig.eventId, liveType, runConfig, &evalCache
            );
            double targetValue = -1e18;
            if (ret.bestDeck.has_value()) {
                targetValue = ret.bestDeck.value().targetValue;
                info.update(ret.bestDeck.value(), runConfig.limit);
            } else {
                targetValue = -1e9 + ret.maxMultiLiveScoreUp;
            }
            info.deckTargetValueMap[deckHash] = targetValue;
            return targetValue;
        };

        auto sizeBucket = cards.size() <= 80 ? "s"
            : cards.size() <= 160 ? "m"
            : cards.size() <= 320 ? "l"
            : "xl";
        uint64_t cardFingerprint = 0;
        constexpr uint64_t fingerprintBase = 10007;
        int fingerprintCount = std::min(int(fullSorted.size()), 24);
        for (int i = 0; i < fingerprintCount; ++i) {
            cardFingerprint = cardFingerprint * fingerprintBase + fullSorted[i].cardId;
        }
        cardFingerprint = cardFingerprint * fingerprintBase + cards.size();
        std::vector<int> cardPoolIds{};
        cardPoolIds.reserve(cards.size());
        for (const auto& card : cards) {
            cardPoolIds.push_back(card.cardId);
        }
        std::sort(cardPoolIds.begin(), cardPoolIds.end());
        uint64_t cardPoolFingerprint = 0;
        for (const auto& cardId : cardPoolIds) {
            cardPoolFingerprint = cardPoolFingerprint * fingerprintBase + cardId;
        }
        cardPoolFingerprint = cardPoolFingerprint * fingerprintBase + cards.size();

        auto policyKey = std::to_string(int(this->dataProvider.region))
            + ":" + std::to_string(eventConfig.eventType)
            + ":" + std::to_string(int(config.target))
            + ":" + std::to_string(config.member)
            + ":" + std::to_string(liveType)
            + ":" + sizeBucket;
        appendConstraintKey(policyKey, "fixed_cards", config.fixedCards, true);
        appendConstraintKey(policyKey, "fixed_characters", config.fixedCharacters, false);
        if (leaderCharacterId.has_value()) {
            policyKey += ":leader=" + std::to_string(leaderCharacterId.value());
        }
        auto sortUnique = [](std::vector<int>& values) {
            std::sort(values.begin(), values.end());
            values.erase(std::unique(values.begin(), values.end()), values.end());
        };
        std::vector<int> eventBonusAttrs{};
        std::vector<int> eventBonusUnits{};
        for (const auto& bonus : this->dataProvider.masterData->eventDeckBonuses) {
            if (bonus.eventId != eventConfig.eventId) {
                continue;
            }
            if (bonus.cardAttr != Enums::Attr::null) {
                eventBonusAttrs.push_back(bonus.cardAttr);
            }
            if (bonus.gameCharacterUnitId != 0) {
                auto unitIt = std::find_if(
                    this->dataProvider.masterData->gameCharacterUnits.begin(),
                    this->dataProvider.masterData->gameCharacterUnits.end(),
                    [&](const GameCharacterUnit& unit) {
                        return unit.id == bonus.gameCharacterUnitId;
                    }
                );
                if (unitIt != this->dataProvider.masterData->gameCharacterUnits.end()) {
                    eventBonusUnits.push_back(unitIt->unit);
                }
            }
        }
        if (config.customBonusAttr.has_value()) {
            eventBonusAttrs.push_back(config.customBonusAttr.value());
        }
        if (eventConfig.eventUnit != 0) {
            eventBonusUnits.push_back(eventConfig.eventUnit);
        }
        if (eventConfig.worldBloomSupportUnit != 0) {
            eventBonusUnits.push_back(eventConfig.worldBloomSupportUnit);
        }
        sortUnique(eventBonusAttrs);
        sortUnique(eventBonusUnits);

        auto transferSeedKey = policyKey;
        appendConstraintKey(transferSeedKey, "bonus_attrs", eventBonusAttrs, false);
        appendConstraintKey(transferSeedKey, "bonus_units", eventBonusUnits, false);
        appendScalarKey(transferSeedKey, "bonus_count_limit", std::to_string(eventConfig.cardBonusCountLimit));
        appendScalarKey(
            transferSeedKey,
            "skill_limit",
            eventConfig.skillScoreUpLimit.has_value()
                ? std::to_string(std::llround(eventConfig.skillScoreUpLimit.value() * 1000.0))
                : "-"
        );
        appendScalarKey(
            transferSeedKey,
            "fixture_limit",
            eventConfig.mysekaiFixtureLimit.has_value()
                ? std::to_string(eventConfig.mysekaiFixtureLimit.value())
                : "-"
        );
        appendScalarKey(transferSeedKey, "filter_other_unit", config.filterOtherUnit ? "1" : "0");
        if (config.customBonusCharacterIds.has_value()) {
            appendConstraintKey(transferSeedKey, "custom_bonus_chars", config.customBonusCharacterIds.value(), true);
        } else {
            appendConstraintKey(transferSeedKey, "custom_bonus_chars", {}, true);
        }
        if (config.customBonusSupportUnits.has_value()) {
            std::vector<int> customSupportUnits{};
            for (const auto& [characterId, unit] : config.customBonusSupportUnits.value()) {
                customSupportUnits.push_back(characterId * 100 + unit);
            }
            appendConstraintKey(transferSeedKey, "custom_support_units", customSupportUnits, true);
        } else {
            appendConstraintKey(transferSeedKey, "custom_support_units", {}, true);
        }
        appendScalarKey(transferSeedKey, "card_pool", std::to_string(cardPoolFingerprint));
        transferSeedKey += ":transfer_seeds";
        auto rlStateKey = policyKey;
        appendScalarKey(rlStateKey, "event_id", std::to_string(eventConfig.eventId));
        appendScalarKey(rlStateKey, "event_chara", std::to_string(eventConfig.specialCharacterId));
        appendScalarKey(rlStateKey, "music_id", std::to_string(config.musicId));
        appendScalarKey(rlStateKey, "music_diff", std::to_string(config.musicDiff));
        appendScalarKey(rlStateKey, "skill_ref", std::to_string(int(config.skillReferenceChooseStrategy)));
        appendScalarKey(rlStateKey, "skill_order", std::to_string(int(config.liveSkillOrder)));
        if (config.specificSkillOrder.has_value()) {
            appendConstraintKey(rlStateKey, "specific_skill_order", config.specificSkillOrder.value(), false);
        }
        appendScalarKey(rlStateKey, "keep_after_training", config.keepAfterTrainingState ? "1" : "0");
        appendScalarKey(rlStateKey, "best_skill_leader", config.bestSkillAsLeader ? "1" : "0");
        appendScalarKey(rlStateKey, "teammate_score", std::to_string(config.multiTeammateScoreUp.value_or(-1)));
        appendScalarKey(rlStateKey, "teammate_power", std::to_string(config.multiTeammatePower.value_or(-1)));
        appendScalarKey(rlStateKey, "score_up_lb", std::to_string(std::llround(config.multiScoreUpLowerBound * 1000.0)));
        appendScalarKey(rlStateKey, "fingerprint", std::to_string(cardFingerprint));

        const auto persistentSeedCachePath = rlSeedCachePath();
        RlPolicyBucket bucket{};
        RlSeedBucket seedBucket{};
        RlSeedBucket transferSeedBucketForRequest{};
        {
            std::lock_guard<std::mutex> lock(rlMemoryMutex);
            if (!rlSeedCacheLoaded) {
                loadPersistentSeedBuckets(persistentSeedCachePath);
                rlSeedCacheLoaded = true;
            }

            auto policyIt = rlPolicyBuckets.find(rlStateKey);
            if (policyIt != rlPolicyBuckets.end()) {
                bucket = policyIt->second;
            }
            if (config.target == RecommendTarget::Mysekai && bucket.episodes == 0) {
                bucket.weights = {
                    0.10, 1.20, 1.45, 0.15, 0.10,
                    0.90, 0.35, 0.20, -0.05, 1.10,
                    1.55, 0.25, 1.50
                };
            }

            auto seedIt = rlSeedBuckets.find(rlStateKey);
            if (seedIt != rlSeedBuckets.end()) {
                seedBucket = seedIt->second;
            }
            auto transferSeedIt = rlSeedBuckets.find(transferSeedKey);
            if (seedBucket.bestSeeds.empty()
                && transferSeedIt != rlSeedBuckets.end()
                && !transferSeedIt->second.bestSeeds.empty()) {
                transferSeedBucketForRequest = transferSeedIt->second;
                transferSeedBucketForRequest.bestTargetValue = -1e18;
                for (auto& seed : transferSeedBucketForRequest.bestSeeds) {
                    seed.targetValue = -1e18;
                }
            }
        }
        bool coldStartRequest = (bucket.episodes == 0 && seedBucket.bestSeeds.empty());

        auto rlCards = buildRlCandidateCards(fullSorted);
        bool hasRememberedSeeds = !seedBucket.bestSeeds.empty();
        bool maturePolicy = bucket.episodes >= 48;
        double totalRatio = maturePolicy ? 0.28 : 0.40;
        int totalMinMs = maturePolicy ? 80 : 120;
        int totalMaxMs = maturePolicy ? 280 : 420;
        if (config.target == RecommendTarget::Score && hasRememberedSeeds && maturePolicy) {
            totalRatio = 0.36;
            totalMinMs = 140;
            totalMaxMs = 520;
        }
        auto totalInfo = makeCalcInfo(std::min(config.timeout_ms, resolveBudgetMs(config.timeout_ms, totalRatio, totalMinMs, totalMaxMs)));

        double policyRatio = 0.12;
        int policyMinMs = 50;
        int policyMaxMs = 180;
        if (maturePolicy) {
            policyRatio = 0.06;
            policyMinMs = 20;
            policyMaxMs = 90;
        }
        if (hasRememberedSeeds && maturePolicy) {
            policyRatio = 0.03;
            policyMinMs = 10;
            policyMaxMs = config.target == RecommendTarget::Score ? 40 : 35;
        }
        if (config.target == RecommendTarget::Score && hasRememberedSeeds && maturePolicy) {
            policyRatio = 0.06;
            policyMinMs = 35;
            policyMaxMs = 140;
        }
        int policyBudgetMs = std::min(config.timeout_ms, resolveBudgetMs(config.timeout_ms, policyRatio, policyMinMs, policyMaxMs));

        double refineRatio = 0.16;
        int refineMinMs = 60;
        int refineMaxMs = 220;
        if (maturePolicy) {
            refineRatio = config.target == RecommendTarget::Score ? 0.12 : 0.10;
            refineMinMs = config.target == RecommendTarget::Score ? 35 : 25;
            refineMaxMs = config.target == RecommendTarget::Score ? 140 : 110;
        }
        if (hasRememberedSeeds && maturePolicy) {
            refineRatio = config.target == RecommendTarget::Score ? 0.08 : 0.08;
            refineMinMs = config.target == RecommendTarget::Score ? 24 : 20;
            refineMaxMs = config.target == RecommendTarget::Score ? 90 : 90;
        }
        if (config.target == RecommendTarget::Score && hasRememberedSeeds && maturePolicy) {
            refineRatio = 0.16;
            refineMinMs = 80;
            refineMaxMs = 260;
        }
        int refineBudgetMs = std::min(config.timeout_ms, resolveBudgetMs(config.timeout_ms, refineRatio, refineMinMs, refineMaxMs));
        auto policyInfo = makeCalcInfo(policyBudgetMs);

        auto sharesAnyUnit = [](const CardDetail& a, const CardDetail& b) {
            for (const auto& unit : a.units) {
                if (std::find(b.units.begin(), b.units.end(), unit) != b.units.end()) {
                    return true;
                }
            }
            return false;
        };

        long long rlSeedBase = config.gaSeed == -1 ? nowNs() : config.gaSeed;
        Rng rlRng(rlSeedBase);
        double rlMaxPower = 1.0;
        double rlMaxSkill = 1.0;
        for (const auto& card : fullSorted) {
            rlMaxPower = std::max(rlMaxPower, double(card.power.max));
            rlMaxSkill = std::max(rlMaxSkill, double(card.skill.max));
        }
        struct RlCardFeatureCache {
            double eventNorm = 0.0;
            double powerNorm = 0.0;
            double skillNorm = 0.0;
            double supportNorm = 0.0;
            double scoreHeuristicValue = 0.0;
        };
        std::unordered_map<int, RlCardFeatureCache> rlCardFeatures{};
        rlCardFeatures.reserve(rlCards.size());
        for (const auto& card : rlCards) {
            rlCardFeatures.emplace(card.cardId, RlCardFeatureCache{
                .eventNorm = cardEventBonus(card) / 70.0,
                .powerNorm = double(card.power.max) / rlMaxPower,
                .skillNorm = double(card.skill.max) / rlMaxSkill,
                .supportNorm = card.supportDeckBonus.value_or(0.0) / 50.0,
                .scoreHeuristicValue = scoreHeuristic(card),
            });
        }

        auto loadStoredSeedDecks = [&](const RlSeedBucket& sourceBucket, const std::vector<CardDetail>& sourceCards) {
            std::unordered_map<int, const CardDetail*> cardById{};
            for (const auto& card : sourceCards) {
                cardById.emplace(card.cardId, &card);
            }

            std::vector<std::vector<const CardDetail*>> seeds{};
            for (const auto& stored : sourceBucket.bestSeeds) {
                std::vector<const CardDetail*> deck{};
                deck.reserve(stored.size);
                bool valid = true;
                for (int i = 0; i < stored.size; ++i) {
                    auto it = cardById.find(stored.cardIds[i]);
                    if (it == cardById.end()) {
                        valid = false;
                        break;
                    }
                    deck.push_back(it->second);
                }
                if (valid && deckMatchesFixedConstraints(deck)) {
                    seeds.push_back(std::move(deck));
                }
            }
            return seeds;
        };
        auto rememberStoredSeeds = [&](RlSeedBucket& targetBucket, const RecommendCalcInfo& info, int maxSeedCount) {
            std::unordered_map<int, const CardDetail*> cardById{};
            for (const auto& card : fullSorted) {
                cardById.emplace(card.cardId, &card);
            }
            auto decks = collectResults(info);
            int used = 0;
            for (const auto& deck : decks) {
                if (used++ >= maxSeedCount) {
                    break;
                }
                RlStoredSeed seed{};
                seed.size = std::min(int(deck.cards.size()), config.member);
                for (int i = 0; i < seed.size; ++i) {
                    seed.cardIds[i] = deck.cards[i].cardId;
                }
                seed.targetValue = deck.targetValue;
                seed.hash = calcStoredSeedHash(seed.cardIds, seed.size);
                std::vector<const CardDetail*> seedDeck{};
                seedDeck.reserve(seed.size);
                for (int i = 0; i < seed.size; ++i) {
                    auto it = cardById.find(deck.cards[i].cardId);
                    if (it == cardById.end()) {
                        seedDeck.clear();
                        break;
                    }
                    seedDeck.push_back(it->second);
                }
                if (!deckMatchesFixedConstraints(seedDeck)) {
                    continue;
                }
                targetBucket.bestTargetValue = std::max(targetBucket.bestTargetValue, seed.targetValue);

                auto it = std::find_if(targetBucket.bestSeeds.begin(), targetBucket.bestSeeds.end(), [&](const RlStoredSeed& existing) {
                    return existing.hash == seed.hash;
                });
                if (it == targetBucket.bestSeeds.end()) {
                    targetBucket.bestSeeds.push_back(seed);
                } else if (seed.targetValue > it->targetValue) {
                    *it = seed;
                }
            }
            std::sort(targetBucket.bestSeeds.begin(), targetBucket.bestSeeds.end(), [](const RlStoredSeed& a, const RlStoredSeed& b) {
                if (a.targetValue != b.targetValue) {
                    return a.targetValue > b.targetValue;
                }
                return a.hash < b.hash;
            });
            if (targetBucket.bestSeeds.size() > std::size_t(maxSeedCount)) {
                targetBucket.bestSeeds.resize(maxSeedCount);
            }
        };
        auto rememberedSeedDecks = loadStoredSeedDecks(seedBucket, fullSorted);
        auto transferSeedDecks = loadStoredSeedDecks(transferSeedBucketForRequest, fullSorted);
        auto monoAttrSeedDecks = collectChallengeMonoAttrSeedDecks(config, &totalInfo, std::max(config.limit, 2));
        if (!rememberedSeedDecks.empty()) {
            int replayLimit = std::min(
                int(rememberedSeedDecks.size()),
                hasRememberedSeeds && maturePolicy
                    ? (config.target == RecommendTarget::Score ? std::max(config.limit * 4, 12) : std::max(config.limit * 2, 8))
                    : config.limit
            );
            for (int i = 0; i < replayLimit && !policyInfo.isTimeout(); ++i) {
                evaluateDeckByCards(config, rememberedSeedDecks[i], policyInfo);
            }
        }
        if (!transferSeedDecks.empty()) {
            int replayLimit = std::min(int(transferSeedDecks.size()), std::max(config.limit, 4));
            for (int i = 0; i < replayLimit && !policyInfo.isTimeout(); ++i) {
                evaluateDeckByCards(config, transferSeedDecks[i], policyInfo);
            }
        }

        auto calcActionFeatures = [&](const std::vector<const CardDetail*>& deck, const CardDetail& candidate) {
            std::array<double, RL_FEATURE_DIM> features{};
            const auto featureIt = rlCardFeatures.find(candidate.cardId);
            const auto cached = featureIt != rlCardFeatures.end()
                ? featureIt->second
                : RlCardFeatureCache{
                    .eventNorm = cardEventBonus(candidate) / 70.0,
                    .powerNorm = double(candidate.power.max) / rlMaxPower,
                    .skillNorm = double(candidate.skill.max) / rlMaxSkill,
                    .supportNorm = candidate.supportDeckBonus.value_or(0.0) / 50.0,
                    .scoreHeuristicValue = scoreHeuristic(candidate),
                };
            int nextPos = int(deck.size());
            int sameAttrCount = 0;
            int sharedUnitCount = 0;
            for (const auto* existing : deck) {
                sameAttrCount += (existing->attr == candidate.attr);
                sharedUnitCount += sharesAnyUnit(*existing, candidate) ? 1 : 0;
            }
            const CardDetail* leader = deck.empty() ? nullptr : deck.front();
            if (leaderCharacterId.has_value()) {
                auto leaderIt = std::find_if(
                    deck.begin(),
                    deck.end(),
                    [&](const CardDetail* card) {
                        return card && card->characterId == leaderCharacterId.value();
                    }
                );
                if (leaderIt != deck.end()) {
                    leader = *leaderIt;
                }
            }
            auto fixedCharacterIndex = nextPos - int(fixedCards.size());
            bool requiredCharacter = fixedCharacterIndex >= 0
                && remainingRequiredCharacters.size() > std::size_t(fixedCharacterIndex);

            features[0] = 1.0;
            features[1] = cached.powerNorm;
            features[2] = config.target == RecommendTarget::Mysekai ? cached.eventNorm : cached.skillNorm;
            features[3] = leader ? double(candidate.attr == leader->attr) : 0.0;
            features[4] = leader ? double(sharesAnyUnit(*leader, candidate)) : 0.0;
            features[5] = requiredCharacter
                ? double(candidate.characterId == remainingRequiredCharacters[fixedCharacterIndex])
                : 0.0;
            features[6] = double(sameAttrCount + 1) / double(std::max(1, config.member));
            features[7] = double(sharedUnitCount + 1) / double(std::max(1, config.member));
            features[8] = double(nextPos) / double(std::max(1, config.member));
            features[9] = nextPos == int(fixedCards.size())
                ? (config.target == RecommendTarget::Mysekai ? cached.scoreHeuristicValue : cached.skillNorm)
                : 0.0;
            features[10] = cached.eventNorm;
            features[11] = cached.supportNorm;
            features[12] = cached.scoreHeuristicValue;
            return features;
        };

        auto runPolicyEpisode = [&](bool allowExploration, bool updateWeights) {
            std::vector<const CardDetail*> deck{};
            deck.reserve(config.member);
            std::unordered_set<int> usedCardIds{};
            std::unordered_set<int> usedCharacterIds{};
            for (const auto& card : fixedCards) {
                deck.push_back(&card);
                usedCardIds.insert(card.cardId);
                usedCharacterIds.insert(card.characterId);
            }

            std::vector<RlStepTrace> traces{};
            traces.reserve(config.member);
            bool valid = true;
            auto episodeStarted = std::chrono::steady_clock::now();

            while (int(deck.size()) < config.member) {
                int nextPos = int(deck.size());
                std::vector<const CardDetail*> options{};
                std::vector<std::array<double, RL_FEATURE_DIM>> optionFeatures{};
                options.reserve(rlCards.size());
                optionFeatures.reserve(rlCards.size());

                for (const auto& card : rlCards) {
                    if (usedCardIds.count(card.cardId)) {
                        continue;
                    }
                    if (!Enums::LiveType::isChallenge(liveType) && usedCharacterIds.count(card.characterId)) {
                        continue;
                    }
                    auto fixedCharacterIndex = nextPos - int(fixedCards.size());
                    if (fixedCharacterIndex >= 0
                        && remainingRequiredCharacters.size() > std::size_t(fixedCharacterIndex)
                        && remainingRequiredCharacters[fixedCharacterIndex] != card.characterId) {
                        continue;
                    }
                    options.push_back(&card);
                    optionFeatures.push_back(calcActionFeatures(deck, card));
                }

                if (options.empty()) {
                    valid = false;
                    break;
                }

                std::array<double, RL_FEATURE_DIM> meanFeatures{};
                for (const auto& features : optionFeatures) {
                    for (int i = 0; i < RL_FEATURE_DIM; ++i) {
                        meanFeatures[i] += features[i];
                    }
                }
                for (int i = 0; i < RL_FEATURE_DIM; ++i) {
                    meanFeatures[i] /= double(options.size());
                }

                double epsilon = allowExploration
                    ? std::max(0.02, 0.18 / std::sqrt(double(bucket.episodes) + 1.0))
                    : 0.0;
                int chosenIndex = 0;
                if (allowExploration && std::uniform_real_distribution<double>(0.0, 1.0)(rlRng) < epsilon) {
                    chosenIndex = std::uniform_int_distribution<int>(0, int(options.size()) - 1)(rlRng);
                } else {
                    std::vector<double> logits(options.size(), 0.0);
                    double maxLogit = -1e18;
                    for (int idx = 0; idx < int(options.size()); ++idx) {
                        double score = 0.0;
                        for (int i = 0; i < RL_FEATURE_DIM; ++i) {
                            score += bucket.weights[i] * optionFeatures[idx][i];
                        }
                        logits[idx] = score;
                        maxLogit = std::max(maxLogit, score);
                    }

                    if (!allowExploration) {
                        for (int idx = 1; idx < int(options.size()); ++idx) {
                            if (logits[idx] > logits[chosenIndex] + 1e-9
                                || (std::abs(logits[idx] - logits[chosenIndex]) <= 1e-9
                                    && options[idx]->cardId < options[chosenIndex]->cardId)) {
                                chosenIndex = idx;
                            }
                        }
                    } else {
                        double temperature = std::max(0.20, 0.75 / std::sqrt(double(bucket.episodes) + 1.0));
                        std::vector<double> probs(options.size(), 0.0);
                        double sum = 0.0;
                        for (int idx = 0; idx < int(options.size()); ++idx) {
                            probs[idx] = std::exp((logits[idx] - maxLogit) / temperature);
                            sum += probs[idx];
                        }
                        double rand = std::uniform_real_distribution<double>(0.0, sum)(rlRng);
                        double acc = 0.0;
                        for (int idx = 0; idx < int(options.size()); ++idx) {
                            acc += probs[idx];
                            if (rand <= acc) {
                                chosenIndex = idx;
                                break;
                            }
                        }
                    }
                }

                if (updateWeights) {
                    traces.push_back(RlStepTrace{
                        .chosen = optionFeatures[chosenIndex],
                        .mean = meanFeatures,
                    });
                }
                auto* chosen = options[chosenIndex];
                deck.push_back(chosen);
                usedCardIds.insert(chosen->cardId);
                usedCharacterIds.insert(chosen->characterId);
            }

            double reward = -2.5;
            double targetValue = -1e18;
            if (valid) {
                targetValue = evaluateDeckByCards(config, deck, policyInfo);
                double elapsedMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - episodeStarted
                ).count();
                double referenceBest = std::max(seedBucket.bestTargetValue, bestTargetValue(policyInfo));
                if (referenceBest <= -1e17) {
                    referenceBest = targetValue;
                }
                double qualityGap = 0.0;
                if (referenceBest > 0.0 && targetValue > -1e17) {
                    qualityGap = std::max(0.0, (referenceBest - targetValue) / referenceBest);
                } else if (targetValue <= -1e17) {
                    qualityGap = 1.0;
                }
                if (config.target == RecommendTarget::Score) {
                    reward = 3.0
                        - 9.0 * std::sqrt(qualityGap)
                        - 0.18 * std::log1p(elapsedMs);
                } else {
                    reward = 2.5
                        - 6.0 * qualityGap
                        - 0.30 * std::log1p(elapsedMs)
                        - 0.05 * deck.size();
                }
                if (targetValue > -1e17) {
                    seedBucket.bestTargetValue = std::max(seedBucket.bestTargetValue, targetValue);
                }
            }

            if (updateWeights) {
                double learningRate = 0.08 / std::sqrt(double(bucket.episodes) + 4.0);
                double advantage = reward - bucket.baselineReward;
                bucket.baselineReward = 0.9 * bucket.baselineReward + 0.1 * reward;
                bucket.episodes++;
                for (const auto& trace : traces) {
                    for (int i = 0; i < RL_FEATURE_DIM; ++i) {
                        bucket.weights[i] += learningRate * advantage * (trace.chosen[i] - trace.mean[i]);
                        bucket.weights[i] = std::max(-6.0, std::min(6.0, bucket.weights[i]));
                    }
                }
            }
        };

        int policyEpisodeCap = 96;
        if (bucket.episodes >= 128) {
            policyEpisodeCap = 12;
        } else if (bucket.episodes >= 48) {
            policyEpisodeCap = 24;
        }
        if (hasRememberedSeeds && bucket.episodes >= 48) {
            policyEpisodeCap = config.target == RecommendTarget::Score ? 10 : 3;
        }

        int policyEpisodes = 0;
        while (!policyInfo.isTimeout() && policyEpisodes < policyEpisodeCap) {
            policyEpisodes++;
            runPolicyEpisode(true, true);
        }

        int greedyEpisodes = bucket.episodes >= 8 ? 2 : 1;
        if (hasRememberedSeeds && maturePolicy) {
            greedyEpisodes = config.target == RecommendTarget::Score ? 2 : 1;
        }
        for (int i = 0; i < greedyEpisodes && !policyInfo.isTimeout(); ++i) {
            runPolicyEpisode(false, false);
        }

        rememberStoredSeeds(seedBucket, policyInfo, std::max(config.limit * 3, 8));

        mergeCalcInfo(totalInfo, policyInfo);

        auto seedDecks = collectSeedDecks(policyInfo, fullSorted, std::max(config.limit * 4, 12));
        std::unordered_set<uint64_t> seedHashes{};
        std::vector<std::vector<const CardDetail*>> mergedSeedDecks{};
        auto tryAddSeedDeck = [&](const std::vector<const CardDetail*>& seedDeck) {
            if (!deckMatchesFixedConstraints(seedDeck)) {
                return;
            }
            auto normalizedSeedDeck = normalizeDeckForLeader(seedDeck);
            if (normalizedSeedDeck.empty()) {
                return;
            }
            auto hash = this->calcDeckHash(normalizedSeedDeck);
            if (seedHashes.insert(hash).second) {
                mergedSeedDecks.push_back(std::move(normalizedSeedDeck));
            }
        };
        for (const auto& seedDeck : seedDecks) {
            tryAddSeedDeck(seedDeck);
        }
        for (const auto& seedDeck : monoAttrSeedDecks) {
            tryAddSeedDeck(seedDeck);
        }
        for (const auto& seedDeck : rememberedSeedDecks) {
            tryAddSeedDeck(seedDeck);
        }
        for (const auto& seedDeck : transferSeedDecks) {
            tryAddSeedDeck(seedDeck);
        }

        if (!mergedSeedDecks.empty()) {
            auto refineCards = config.target == RecommendTarget::Score
                ? (hasRememberedSeeds && maturePolicy
                    ? buildSeededRefineCards(fullSorted, totalInfo, &mergedSeedDecks)
                    : fullSorted)
                : buildSeededRefineCards(fullSorted, totalInfo, &mergedSeedDecks);
            auto gaConfig = tuneGaConfig(config, refineCards.size(), false, true);
            if (config.target == RecommendTarget::Score && hasRememberedSeeds && maturePolicy) {
                gaConfig.gaPopSize = std::min(gaConfig.gaPopSize, std::max(1400, std::min(3200, int(refineCards.size()) * 10)));
                gaConfig.gaParentSize = std::min(gaConfig.gaParentSize, std::max(96, gaConfig.gaPopSize / 7));
                gaConfig.gaEliteSize = std::min(gaConfig.gaEliteSize, std::max(0, gaConfig.gaPopSize / 20));
                gaConfig.gaMaxIter = std::min(gaConfig.gaMaxIter, 128);
                gaConfig.gaMaxIterNoImprove = std::min(gaConfig.gaMaxIterNoImprove, 6);
            }
            auto refineInfo = makeCalcInfo(refineBudgetMs);
            runGaSearch(gaConfig, refineCards, refineInfo, &mergedSeedDecks);
            mergeCalcInfo(totalInfo, refineInfo);
            rememberStoredSeeds(seedBucket, totalInfo, std::max(config.limit * 3, 8));
        }

        if (coldStartRequest && config.target == RecommendTarget::Score && bucket.episodes >= 48) {
            for (int warmRound = 0; warmRound < 2; ++warmRound) {
                auto warmedSeedDecks = loadStoredSeedDecks(seedBucket, fullSorted);
                if (warmedSeedDecks.empty()) {
                    break;
                }
                std::unordered_set<uint64_t> warmedSeedHashes{};
                std::vector<std::vector<const CardDetail*>> warmedMergedSeedDecks{};
                auto tryAddWarmedSeedDeck = [&](const std::vector<const CardDetail*>& seedDeck) {
                    if (!deckMatchesFixedConstraints(seedDeck)) {
                        return;
                    }
                    auto normalizedSeedDeck = normalizeDeckForLeader(seedDeck);
                    if (normalizedSeedDeck.empty()) {
                        return;
                    }
                    auto hash = this->calcDeckHash(normalizedSeedDeck);
                    if (warmedSeedHashes.insert(hash).second) {
                        warmedMergedSeedDecks.push_back(std::move(normalizedSeedDeck));
                    }
                };
                for (const auto& seedDeck : collectSeedDecks(totalInfo, fullSorted, std::max(config.limit * 4, 12))) {
                    tryAddWarmedSeedDeck(seedDeck);
                }
                for (const auto& seedDeck : monoAttrSeedDecks) {
                    tryAddWarmedSeedDeck(seedDeck);
                }
                for (const auto& seedDeck : warmedSeedDecks) {
                    tryAddWarmedSeedDeck(seedDeck);
                }
                for (const auto& seedDeck : transferSeedDecks) {
                    tryAddWarmedSeedDeck(seedDeck);
                }

                int warmReplayBudgetMs = std::min(config.timeout_ms, resolveBudgetMs(config.timeout_ms, 0.06, 35, 140));
                auto warmInfo = makeCalcInfo(warmReplayBudgetMs);
                int replayLimit = std::min(int(warmedSeedDecks.size()), std::max(config.limit * 4, 12));
                for (int i = 0; i < replayLimit && !warmInfo.isTimeout(); ++i) {
                    evaluateDeckByCards(config, warmedSeedDecks[i], warmInfo);
                }

                if (!warmedMergedSeedDecks.empty()) {
                    auto warmRefineCards = buildSeededRefineCards(fullSorted, totalInfo, &warmedMergedSeedDecks);
                    auto warmGaConfig = tuneGaConfig(config, warmRefineCards.size(), false, true);
                    warmGaConfig.gaPopSize = std::min(warmGaConfig.gaPopSize, std::max(1400, std::min(3200, int(warmRefineCards.size()) * 10)));
                    warmGaConfig.gaParentSize = std::min(warmGaConfig.gaParentSize, std::max(96, warmGaConfig.gaPopSize / 7));
                    warmGaConfig.gaEliteSize = std::min(warmGaConfig.gaEliteSize, std::max(0, warmGaConfig.gaPopSize / 20));
                    warmGaConfig.gaMaxIter = std::min(warmGaConfig.gaMaxIter, 128);
                    warmGaConfig.gaMaxIterNoImprove = std::min(warmGaConfig.gaMaxIterNoImprove, 6);

                    int warmRefineBudgetMs = std::min(config.timeout_ms, resolveBudgetMs(config.timeout_ms, 0.16, 80, 260));
                    warmInfo.timeout = timeoutToNs(warmRefineBudgetMs);
                    warmInfo.start_ts = nowNs();
                    runGaSearch(warmGaConfig, warmRefineCards, warmInfo, &warmedMergedSeedDecks);
                }

                mergeCalcInfo(totalInfo, warmInfo);
                rememberStoredSeeds(seedBucket, totalInfo, std::max(config.limit * 3, 8));
            }
        }

        RlSeedBucket transferSeedBucket{};
        rememberStoredSeeds(transferSeedBucket, totalInfo, std::max(config.limit * 8, 32));
        {
            std::lock_guard<std::mutex> lock(rlMemoryMutex);
            auto& storedPolicyBucket = rlPolicyBuckets[rlStateKey];
            if (bucket.episodes >= storedPolicyBucket.episodes) {
                storedPolicyBucket = bucket;
            }
            mergeSeedBucket(rlSeedBuckets[rlStateKey], seedBucket, 16);
            mergeSeedBucket(rlSeedBuckets[transferSeedKey], transferSeedBucket, 32);
            savePersistentSeedBuckets(persistentSeedCachePath);
        }

        auto result = collectResults(totalInfo);
        if (result.empty()) {
            auto fallbackInfo = makeCalcInfo(std::min(config.timeout_ms, resolveBudgetMs(config.timeout_ms, 0.12, 60, 180)));
            runGaSearch(config, cards, fallbackInfo);
            result = collectResults(fallbackInfo);
        }
        return ensureResults(result);
    }

    if (config.algorithm == RecommendAlgorithm::DFS) {
        std::vector<RecommendDeck> ans{};
        std::vector<CardDetail> cardDetails{};
        std::vector<CardDetail> preCardDetails{};
        auto calcInfo = makeCalcInfo(config.timeout_ms);
        if (!calcInfo.isTimeout()) {
            RecommendCalcInfo warmupInfo{};
            warmupInfo.start_ts = calcInfo.start_ts;
            warmupInfo.timeout = timeoutToNs(std::min(config.timeout_ms, resolveBudgetMs(config.timeout_ms, 0.08, 20, 120)));
            auto gaWarmupConfig = tuneGaConfig(config, cards.size(), true, false);
            runGaSearch(gaWarmupConfig, cards, warmupInfo);
            mergeCalcInfo(calcInfo, warmupInfo);
        }
        while (true) {
            cardDetails = filterCardPriority(liveType, eventConfig.eventType, cards, preCardDetails, config.member);
            if (cardDetails.size() == preCardDetails.size()) {
                if (ans.empty()) {
                    throw std::runtime_error("Cannot recommend any deck in " + std::to_string(cards.size()) + " cards");
                }
                break;
            }
            preCardDetails = cardDetails;
            runDfsExact(config, cardDetails, calcInfo);
            ans = collectResults(calcInfo);
            if (int(ans.size()) >= config.limit || calcInfo.isTimeout()) {
                break;
            }
        }
        return attachSupportDeckCards(std::move(ans));
    }

    throw std::runtime_error("Unknown algorithm: " + std::to_string(int(config.algorithm)));
}
