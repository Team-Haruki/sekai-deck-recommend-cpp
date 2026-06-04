#include "deck-recommend/base-deck-recommend.h"
#include "card-priority/card-priority-filter.h"
#include "common/timer.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <random>
#include <sstream>


uint64_t BaseDeckRecommend::calcDeckHash(const std::vector<const CardDetail*>& deck) {
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
    const DeckRecommendConfig& config
) const {
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
                return {};
            }
            if (!cardIds.insert(card->cardId).second) {
                return {};
            }
            if (!Enums::LiveType::isChallenge(liveType)) {
                if (!characterIds.insert(card->characterId).second) {
                    return {};
                }
            } else {
                characterIds.insert(card->characterId);
            }
        }

        for (const auto& fixedCardId : config.fixedCards) {
            if (!cardIds.count(fixedCardId)) {
                return {};
            }
        }
        for (const auto& characterId : resolveRequiredCharacters(config, isWorldBloomFinale, specialCharacterId)) {
            if (!characterIds.count(characterId)) {
                return {};
            }
        }
    }

    if (!config.fixedCards.empty() && !applyFixedCardOrder(orderedDeckCards, config.fixedCards)) {
        return {};
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
            return {};
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
    return ret;
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

    auto areaItemLevels = areaItemService.getAreaItemLevels();
    auto& cardEpisodes = this->dataProvider.masterData->cardEpisodes;

    std::optional<double> scoreUpLimit = std::nullopt;
    if (eventConfig.skillScoreUpLimit.has_value() && !Enums::LiveType::isChallenge(liveType))
        scoreUpLimit = eventConfig.skillScoreUpLimit;

    auto cards = cardCalculator.batchGetCardDetail(
        userCards, config.cardConfig, config.singleCardConfig, 
        eventConfig, areaItemLevels, scoreUpLimit,
        config.customBonusCharacterIds, config.customBonusAttr, config.customBonusSupportUnits
    );

    // 归类支援卡组
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
            
            auto& c = findOrThrow(this->dataProvider.masterData->cards, [&](const Card& c) {
                return c.id == card_id;
            }, [&]() { return "Card not found for fixed cardId=" + std::to_string(card_id); });
            bool hasSpecialTraining = c.cardRarityType == Enums::CardRarityType::rarity_3
                                    || c.cardRarityType == Enums::CardRarityType::rarity_4;
            uc.defaultImage = hasSpecialTraining ? Enums::DefaultImage::special_training : Enums::DefaultImage::original;

            for (auto& ep : cardEpisodes) 
                if (ep.cardId == card_id) {
                    UserCardEpisodes uce{};
                    uce.cardEpisodeId = ep.id;
                    uce.scenarioStatus = 0;
                    uc.episodes.push_back(uce);
                }
            auto card = cardCalculator.batchGetCardDetail(
                {uc}, config.cardConfig, config.singleCardConfig, 
                eventConfig, areaItemLevels, scoreUpLimit,
                config.customBonusCharacterIds, config.customBonusAttr, config.customBonusSupportUnits
            );
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
        }
        if (config.target == RecommendTarget::Bonus) {
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
        } else if (
            config.target == RecommendTarget::Score
            || config.target == RecommendTarget::Mysekai
            || config.target == RecommendTarget::Bonus
        ) {
            std::sort(input.begin(), input.end(), [&](const CardDetail& a, const CardDetail& b) {
                return std::make_tuple(scoreHeuristic(a), cardEventBonus(a), a.skill.max, a.power.max, a.cardId)
                    > std::make_tuple(scoreHeuristic(b), cardEventBonus(b), b.skill.max, b.power.max, b.cardId);
            });
        } else {
            std::sort(input.begin(), input.end(), [](const CardDetail& a, const CardDetail& b) {
                return std::make_tuple(a.power.max, a.power.min, a.cardId)
                    > std::make_tuple(b.power.max, b.power.min, b.cardId);
            });
        }
        return input;
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
    auto runDfsExact = [&](const DeckRecommendConfig& runConfig, const std::vector<CardDetail>& sourceCards, RecommendCalcInfo& info) {
        auto sortedCards = sortCardsByStrength(sourceCards);
        initDfsState(info);
        std::optional<DfsScoreUpperBoundContext> scoreUpperBoundContext = std::nullopt;
        if (runConfig.target == RecommendTarget::Score || runConfig.target == RecommendTarget::Skill) {
            scoreUpperBoundContext = DfsScoreUpperBoundContext{ .musicMeta = musicMeta };
        }
        findBestCardsDFS(
            liveType, runConfig, sortedCards, supportCards, sf,
            info,
            runConfig.limit, Enums::LiveType::isChallenge(liveType), runConfig.member, honorBonus,
            eventConfig.eventType, eventConfig.eventId, fixedCards,
            scoreUpperBoundContext.has_value() ? &scoreUpperBoundContext.value() : nullptr
        );
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
        findBestCardsGA(
            liveType, runConfig, rng, sortedCards, supportCards, sf,
            info,
            runConfig.limit, Enums::LiveType::isChallenge(liveType), runConfig.member, honorBonus,
            eventConfig.eventType, eventConfig.eventId, fixedCards, seedDecks
        );
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
        if (supportCards.empty()) {
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
        if (eventConfig.eventType != Enums::EventType::world_bloom) {
            findTargetBonusCardsDFS(
                liveType, config, cards, sf, calcInfo,
                config.limit, config.member, eventConfig.eventType, eventConfig.eventId
            );
        }
        else {
            findWorldBloomTargetBonusCardsDFS(
                liveType, config, cards, sf, calcInfo,
                config.limit, config.member, eventConfig.eventType, eventConfig.eventId
            );
        }

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
        runDfsExact(baseConfig, seedCards, seedInfo);
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
            findBestCardsSA(
                liveType, config, rng, sortedCards, supportCards, sf,
                calcInfo,
                config.limit, Enums::LiveType::isChallenge(liveType), config.member, honorBonus,
                eventConfig.eventType, eventConfig.eventId, fixedCards
            );
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
        struct RlCardRankEntry {
            int cardId = 0;
            double score = 0.0;
            double bestScore = 0.0;
            int visits = 0;
        };
        struct RlCardRankBucket {
            std::unordered_map<int, RlCardRankEntry> cards{};
            int updates = 0;
        };
        static std::unordered_map<std::string, RlPolicyBucket> rlPolicyBuckets{};
        static std::unordered_map<std::string, RlSeedBucket> rlSeedBuckets{};
        static std::unordered_map<std::string, RlCardRankBucket> rlCardRankBuckets{};
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
        auto normalizeCardRankBucket = [](RlCardRankBucket& bucket, std::size_t maxCardCount) {
            std::vector<RlCardRankEntry> entries{};
            entries.reserve(bucket.cards.size());
            for (const auto& [cardId, entry] : bucket.cards) {
                if (entry.cardId > 0 && entry.visits > 0 && entry.score > 0.0) {
                    entries.push_back(entry);
                }
            }
            std::sort(entries.begin(), entries.end(), [](const RlCardRankEntry& a, const RlCardRankEntry& b) {
                auto aScore = a.score + 0.20 * a.bestScore + 0.015 * std::log1p(double(a.visits));
                auto bScore = b.score + 0.20 * b.bestScore + 0.015 * std::log1p(double(b.visits));
                if (std::abs(aScore - bScore) > 1e-9) {
                    return aScore > bScore;
                }
                return a.cardId < b.cardId;
            });
            if (entries.size() > maxCardCount) {
                entries.resize(maxCardCount);
            }
            bucket.cards.clear();
            for (const auto& entry : entries) {
                bucket.cards.emplace(entry.cardId, entry);
            }
        };
        auto mergeCardRankBucket = [&](RlCardRankBucket& target, const RlCardRankBucket& source, std::size_t maxCardCount) {
            for (const auto& [cardId, sourceEntry] : source.cards) {
                if (sourceEntry.cardId <= 0 || sourceEntry.visits <= 0 || sourceEntry.score <= 0.0) {
                    continue;
                }
                auto& targetEntry = target.cards[cardId];
                if (targetEntry.cardId == 0) {
                    targetEntry.cardId = cardId;
                }
                int oldWeight = std::min(targetEntry.visits, 64);
                int newWeight = std::min(sourceEntry.visits, 32);
                if (oldWeight + newWeight > 0) {
                    targetEntry.score = (
                        targetEntry.score * double(oldWeight)
                        + sourceEntry.score * double(newWeight)
                    ) / double(oldWeight + newWeight);
                } else {
                    targetEntry.score = std::max(targetEntry.score, sourceEntry.score);
                }
                targetEntry.bestScore = std::max(targetEntry.bestScore, sourceEntry.bestScore);
                targetEntry.visits = std::min(targetEntry.visits + sourceEntry.visits, 1000000);
            }
            target.updates += source.updates;
            normalizeCardRankBucket(target, maxCardCount);
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
                    if (version == "v1" && key.find(":transfer_seeds") != std::string::npos) {
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
                    } else if (version == "v2" && key.find(":card_ranks") != std::string::npos) {
                        RlCardRankEntry entry{};
                        entry.cardId = std::stoi(bucketBestValue);
                        entry.score = std::stod(seedTargetValue);
                        entry.bestScore = std::stod(sizeValue);
                        entry.visits = std::stoi(cardsValue);
                        if (entry.cardId <= 0 || entry.score <= 0.0 || entry.visits <= 0) {
                            continue;
                        }
                        auto& bucket = rlCardRankBuckets[key];
                        bucket.cards[entry.cardId] = entry;
                        bucket.updates++;
                    }
                } catch (...) {
                    continue;
                }
            }

            for (auto& [key, bucket] : rlSeedBuckets) {
                if (key.find(":transfer_seeds") != std::string::npos) {
                    normalizeSeedBucket(bucket, 32);
                }
            }
            for (auto& [key, bucket] : rlCardRankBuckets) {
                if (key.find(":card_ranks") != std::string::npos) {
                    normalizeCardRankBucket(bucket, 160);
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
            out << "# deck-service RL memory cache v2\n";

            std::size_t seedBucketCount = 0;
            constexpr std::size_t maxPersistentBuckets = 512;
            for (const auto& [key, bucket] : rlSeedBuckets) {
                if (key.find(":transfer_seeds") == std::string::npos || bucket.bestSeeds.empty()) {
                    continue;
                }
                if (seedBucketCount++ >= maxPersistentBuckets) {
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
            std::size_t rankBucketCount = 0;
            for (const auto& [key, bucket] : rlCardRankBuckets) {
                if (key.find(":card_ranks") == std::string::npos || bucket.cards.empty()) {
                    continue;
                }
                if (rankBucketCount++ >= maxPersistentBuckets) {
                    break;
                }
                std::vector<RlCardRankEntry> entries{};
                entries.reserve(bucket.cards.size());
                for (const auto& [cardId, entry] : bucket.cards) {
                    if (entry.cardId > 0 && entry.score > 0.0 && entry.visits > 0) {
                        entries.push_back(entry);
                    }
                }
                std::sort(entries.begin(), entries.end(), [](const RlCardRankEntry& a, const RlCardRankEntry& b) {
                    auto aScore = a.score + 0.20 * a.bestScore + 0.015 * std::log1p(double(a.visits));
                    auto bScore = b.score + 0.20 * b.bestScore + 0.015 * std::log1p(double(b.visits));
                    if (std::abs(aScore - bScore) > 1e-9) {
                        return aScore > bScore;
                    }
                    return a.cardId < b.cardId;
                });
                if (entries.size() > 160) {
                    entries.resize(160);
                }
                for (const auto& entry : entries) {
                    out << "v2\t" << key << '\t'
                        << entry.cardId << '\t'
                        << entry.score << '\t'
                        << entry.bestScore << '\t'
                        << entry.visits << '\n';
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
                honorBonus, eventConfig.eventType, eventConfig.eventId, liveType, runConfig
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
        auto cardRankKey = policyKey;
        appendConstraintKey(cardRankKey, "bonus_attrs", eventBonusAttrs, false);
        appendConstraintKey(cardRankKey, "bonus_units", eventBonusUnits, false);
        appendScalarKey(cardRankKey, "bonus_count_limit", std::to_string(eventConfig.cardBonusCountLimit));
        appendScalarKey(cardRankKey, "filter_other_unit", config.filterOtherUnit ? "1" : "0");
        appendScalarKey(cardRankKey, "card_pool", std::to_string(cardPoolFingerprint));
        cardRankKey += ":card_ranks";
        auto broadCardRankKey = policyKey;
        appendScalarKey(broadCardRankKey, "filter_other_unit", config.filterOtherUnit ? "1" : "0");
        appendScalarKey(broadCardRankKey, "card_pool", std::to_string(cardPoolFingerprint));
        broadCardRankKey += ":card_ranks";
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
        RlCardRankBucket cardRankBucketForRequest{};
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
            auto broadCardRankIt = rlCardRankBuckets.find(broadCardRankKey);
            if (broadCardRankIt != rlCardRankBuckets.end()) {
                cardRankBucketForRequest = broadCardRankIt->second;
            }
            auto cardRankIt = rlCardRankBuckets.find(cardRankKey);
            if (cardRankIt != rlCardRankBuckets.end()) {
                mergeCardRankBucket(cardRankBucketForRequest, cardRankIt->second, 160);
            }
        }
        bool coldStartRequest = (bucket.episodes == 0 && seedBucket.bestSeeds.empty());

        auto rlCards = buildRlCandidateCards(fullSorted);
        bool hasCardRankMemory = !cardRankBucketForRequest.cards.empty();
        std::unordered_map<int, double> memoryCardScores{};
        if (hasCardRankMemory) {
            std::unordered_map<int, const CardDetail*> cardById{};
            for (const auto& card : fullSorted) {
                cardById.emplace(card.cardId, &card);
            }
            std::vector<RlCardRankEntry> entries{};
            entries.reserve(cardRankBucketForRequest.cards.size());
            for (const auto& [cardId, entry] : cardRankBucketForRequest.cards) {
                if (entry.cardId > 0 && entry.score > 0.0 && entry.visits > 0) {
                    entries.push_back(entry);
                }
            }
            std::sort(entries.begin(), entries.end(), [](const RlCardRankEntry& a, const RlCardRankEntry& b) {
                auto aScore = a.score + 0.20 * a.bestScore + 0.015 * std::log1p(double(a.visits));
                auto bScore = b.score + 0.20 * b.bestScore + 0.015 * std::log1p(double(b.visits));
                if (std::abs(aScore - bScore) > 1e-9) {
                    return aScore > bScore;
                }
                return a.cardId < b.cardId;
            });
            for (const auto& entry : entries) {
                memoryCardScores[entry.cardId] = entry.score + 0.20 * entry.bestScore + 0.015 * std::log1p(double(entry.visits));
            }

            std::unordered_set<int> selectedCardIds{};
            for (const auto& card : rlCards) {
                selectedCardIds.insert(card.cardId);
            }
            int addedRankCards = 0;
            int maxRankCardAdds = std::max(config.member * (config.target == RecommendTarget::Score ? 12 : 8), config.target == RecommendTarget::Score ? 60 : 32);
            for (const auto& entry : entries) {
                if (addedRankCards >= maxRankCardAdds) {
                    break;
                }
                auto it = cardById.find(entry.cardId);
                if (it == cardById.end() || selectedCardIds.count(entry.cardId)) {
                    continue;
                }
                rlCards.push_back(*it->second);
                selectedCardIds.insert(entry.cardId);
                addedRankCards++;
            }
        }
        if (hasCardRankMemory) {
            std::sort(rlCards.begin(), rlCards.end(), [&](const CardDetail& a, const CardDetail& b) {
                double aMemoryScore = memoryCardScores.count(a.cardId) ? memoryCardScores[a.cardId] : 0.0;
                double bMemoryScore = memoryCardScores.count(b.cardId) ? memoryCardScores[b.cardId] : 0.0;
                auto aScore = aMemoryScore + 0.15 * scoreHeuristic(a) + 0.03 * cardEventBonus(a);
                auto bScore = bMemoryScore + 0.15 * scoreHeuristic(b) + 0.03 * cardEventBonus(b);
                if (std::abs(aScore - bScore) > 1e-9) {
                    return aScore > bScore;
                }
                return std::make_tuple(a.power.max, a.skill.max, a.cardId)
                    > std::make_tuple(b.power.max, b.skill.max, b.cardId);
            });
        }
        if (!Enums::LiveType::isChallenge(liveType) && rlCards.size() > std::size_t(std::max(config.member * 12, 48))) {
            std::unordered_set<int> fixedCardIdSet{};
            for (const auto& card : fixedCards) {
                fixedCardIdSet.insert(card.cardId);
            }
            auto sameUnits = [](const CardDetail& a, const CardDetail& b) {
                if (a.units.size() != b.units.size()) {
                    return false;
                }
                for (const auto& unit : a.units) {
                    if (std::find(b.units.begin(), b.units.end(), unit) == b.units.end()) {
                        return false;
                    }
                }
                return true;
            };
            auto dominatesCard = [&](const CardDetail& better, const CardDetail& worse) {
                if (better.cardId == worse.cardId
                    || better.characterId != worse.characterId
                    || better.attr != worse.attr
                    || !sameUnits(better, worse)) {
                    return false;
                }
                double betterEvent = cardEventBonus(better);
                double worseEvent = cardEventBonus(worse);
                double betterSupport = better.supportDeckBonus.value_or(0.0);
                double worseSupport = worse.supportDeckBonus.value_or(0.0);
                bool noWorse = better.power.max >= worse.power.max
                    && better.power.min >= worse.power.min
                    && better.skill.max >= worse.skill.max
                    && better.skill.min >= worse.skill.min
                    && betterEvent >= worseEvent
                    && betterSupport >= worseSupport
                    && scoreHeuristic(better) >= scoreHeuristic(worse);
                bool strictlyBetter = better.power.max > worse.power.max
                    || better.power.min > worse.power.min
                    || better.skill.max > worse.skill.max
                    || better.skill.min > worse.skill.min
                    || betterEvent > worseEvent
                    || betterSupport > worseSupport
                    || scoreHeuristic(better) > scoreHeuristic(worse);
                return noWorse && strictlyBetter;
            };

            std::vector<CardDetail> dominancePruned{};
            dominancePruned.reserve(rlCards.size());
            for (const auto& card : rlCards) {
                bool dominated = fixedCardIdSet.count(card.cardId) == 0;
                if (dominated) {
                    dominated = false;
                    for (const auto& kept : dominancePruned) {
                        if (dominatesCard(kept, card)) {
                            dominated = true;
                            break;
                        }
                    }
                }
                if (!dominated) {
                    dominancePruned.push_back(card);
                }
            }
            if (canMakeDeck(liveType, eventConfig.eventType, dominancePruned, config.member)) {
                rlCards = std::move(dominancePruned);
            }
        }
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
        if (coldStartRequest && hasCardRankMemory) {
            policyRatio = config.target == RecommendTarget::Score ? 0.09 : 0.07;
            policyMinMs = config.target == RecommendTarget::Score ? 35 : 25;
            policyMaxMs = config.target == RecommendTarget::Score ? 140 : 100;
        }
        int policyBudgetMs = std::min(config.timeout_ms, resolveBudgetMs(config.timeout_ms, policyRatio, policyMinMs, policyMaxMs));

        auto policyInfo = makeCalcInfo(policyBudgetMs);

        auto sharesAnyUnit = [](const CardDetail& a, const CardDetail& b) {
            for (const auto& unit : a.units) {
                if (std::find(b.units.begin(), b.units.end(), unit) != b.units.end()) {
                    return true;
                }
            }
            return false;
        };

        auto stableHashString = [](const std::string& value) {
            uint64_t hash = 1469598103934665603ULL;
            for (unsigned char ch : value) {
                hash ^= uint64_t(ch);
                hash *= 1099511628211ULL;
            }
            return hash;
        };
        uint64_t stableRlSeed = stableHashString(rlStateKey)
            ^ (cardPoolFingerprint + 0x9e3779b97f4a7c15ULL + (uint64_t(config.musicId) << 6) + (uint64_t(config.musicDiff) >> 2));
        long long rlSeedBase = config.gaSeed == -1 ? static_cast<long long>(stableRlSeed & 0x7fffffffffffffffULL) : config.gaSeed;
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
        auto buildRankSeedDecks = [&](const RlCardRankBucket& sourceBucket, const std::vector<CardDetail>& sourceCards, int maxSeedCount) {
            std::unordered_map<int, const CardDetail*> cardById{};
            for (const auto& card : sourceCards) {
                cardById.emplace(card.cardId, &card);
            }

            std::vector<const CardDetail*> historyRanked{};
            historyRanked.reserve(sourceBucket.cards.size());
            for (const auto& [cardId, entry] : sourceBucket.cards) {
                auto it = cardById.find(cardId);
                if (it != cardById.end() && entry.visits > 0 && entry.score > 0.0) {
                    historyRanked.push_back(it->second);
                }
            }
            std::sort(historyRanked.begin(), historyRanked.end(), [&](const CardDetail* a, const CardDetail* b) {
                const auto& aEntry = sourceBucket.cards.at(a->cardId);
                const auto& bEntry = sourceBucket.cards.at(b->cardId);
                auto aScore = aEntry.score
                    + 0.20 * aEntry.bestScore
                    + 0.015 * std::log1p(double(aEntry.visits))
                    + 0.25 * scoreHeuristic(*a);
                auto bScore = bEntry.score
                    + 0.20 * bEntry.bestScore
                    + 0.015 * std::log1p(double(bEntry.visits))
                    + 0.25 * scoreHeuristic(*b);
                if (std::abs(aScore - bScore) > 1e-9) {
                    return aScore > bScore;
                }
                return a->cardId < b->cardId;
            });

            std::vector<const CardDetail*> currentRanked{};
            currentRanked.reserve(sourceCards.size());
            for (const auto& card : sourceCards) {
                currentRanked.push_back(&card);
            }

            std::vector<std::vector<const CardDetail*>> seeds{};
            std::unordered_set<uint64_t> seedHashes{};
            int maxVariants = std::max(maxSeedCount * 4, 12);
            for (int variant = 0; variant < maxVariants && int(seeds.size()) < maxSeedCount; ++variant) {
                std::vector<const CardDetail*> deck{};
                deck.reserve(config.member);
                std::unordered_set<int> usedCardIds{};
                std::unordered_set<int> usedCharacterIds{};

                auto tryPush = [&](const CardDetail* card) {
                    if (card == nullptr || int(deck.size()) >= config.member) {
                        return false;
                    }
                    if (!usedCardIds.insert(card->cardId).second) {
                        return false;
                    }
                    if (!Enums::LiveType::isChallenge(liveType)
                        && !usedCharacterIds.insert(card->characterId).second) {
                        usedCardIds.erase(card->cardId);
                        return false;
                    }
                    if (Enums::LiveType::isChallenge(liveType)) {
                        usedCharacterIds.insert(card->characterId);
                    }
                    deck.push_back(card);
                    return true;
                };
                auto canUse = [&](const CardDetail* card) {
                    if (card == nullptr || usedCardIds.count(card->cardId)) {
                        return false;
                    }
                    if (!Enums::LiveType::isChallenge(liveType) && usedCharacterIds.count(card->characterId)) {
                        return false;
                    }
                    return true;
                };
                auto chooseForCharacter = [&](int characterId, int skip) -> const CardDetail* {
                    auto scan = [&](const std::vector<const CardDetail*>& list, int& remainingSkip) -> const CardDetail* {
                        for (const auto* card : list) {
                            if (!canUse(card) || card->characterId != characterId) {
                                continue;
                            }
                            if (remainingSkip-- > 0) {
                                continue;
                            }
                            return card;
                        }
                        return nullptr;
                    };
                    int remainingSkip = skip;
                    if (auto* card = scan(historyRanked, remainingSkip)) {
                        return card;
                    }
                    remainingSkip = skip;
                    return scan(currentRanked, remainingSkip);
                };
                auto fillFrom = [&](const std::vector<const CardDetail*>& list, int start) {
                    if (list.empty()) {
                        return;
                    }
                    for (int n = 0; n < int(list.size()) && int(deck.size()) < config.member; ++n) {
                        int idx = (start + n) % int(list.size());
                        tryPush(list[idx]);
                    }
                };

                bool valid = true;
                for (const auto& fixedCardId : config.fixedCards) {
                    auto it = cardById.find(fixedCardId);
                    if (it == cardById.end() || !tryPush(it->second)) {
                        valid = false;
                        break;
                    }
                }
                if (!valid) {
                    continue;
                }

                int requiredSkip = variant / 4;
                if (leaderCharacterId.has_value() && !usedCharacterIds.count(leaderCharacterId.value())) {
                    auto* card = chooseForCharacter(leaderCharacterId.value(), requiredSkip);
                    if (card == nullptr || !tryPush(card)) {
                        continue;
                    }
                }
                for (const auto characterId : remainingRequiredCharacters) {
                    if (!Enums::LiveType::isChallenge(liveType) && usedCharacterIds.count(characterId)) {
                        continue;
                    }
                    auto* card = chooseForCharacter(characterId, requiredSkip);
                    if (card == nullptr || !tryPush(card)) {
                        valid = false;
                        break;
                    }
                }
                if (!valid) {
                    continue;
                }

                int start = historyRanked.empty() ? 0 : variant % int(historyRanked.size());
                if (variant % 3 == 1) {
                    fillFrom(currentRanked, variant % std::max(1, int(currentRanked.size())));
                    fillFrom(historyRanked, start);
                } else {
                    fillFrom(historyRanked, start);
                    fillFrom(currentRanked, variant % std::max(1, int(currentRanked.size())));
                }

                if (int(deck.size()) != config.member || !deckMatchesFixedConstraints(deck)) {
                    continue;
                }
                auto normalizedDeck = normalizeDeckForLeader(deck);
                if (normalizedDeck.empty() || !deckMatchesFixedConstraints(normalizedDeck)) {
                    continue;
                }
                auto hash = this->calcDeckHash(normalizedDeck);
                if (seedHashes.insert(hash).second) {
                    seeds.push_back(std::move(normalizedDeck));
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
        auto rememberCardRanks = [&](RlCardRankBucket& targetBucket, const RecommendCalcInfo& info, int maxDeckCount) {
            auto decks = collectResults(info);
            if (decks.empty()) {
                return;
            }
            double bestValue = std::max(1.0, decks.front().targetValue);
            int usedDecks = 0;
            for (const auto& deck : decks) {
                if (usedDecks++ >= maxDeckCount || deck.targetValue <= -1e17) {
                    break;
                }
                double quality = std::max(0.05, std::min(1.0, deck.targetValue / bestValue));
                int cardIndex = 0;
                for (const auto& card : deck.cards) {
                    auto& entry = targetBucket.cards[card.cardId];
                    if (entry.cardId == 0) {
                        entry.cardId = card.cardId;
                    }
                    double positionWeight = cardIndex == 0 ? 1.15 : 1.0;
                    double contribution = quality * positionWeight / double(usedDecks);
                    int oldWeight = std::min(entry.visits, 64);
                    entry.score = (
                        entry.score * double(oldWeight)
                        + contribution
                    ) / double(oldWeight + 1);
                    entry.bestScore = std::max(entry.bestScore, contribution);
                    entry.visits = std::min(entry.visits + 1, 1000000);
                    cardIndex++;
                }
            }
            targetBucket.updates++;
            normalizeCardRankBucket(targetBucket, 160);
        };
        auto rememberedSeedDecks = loadStoredSeedDecks(seedBucket, fullSorted);
        auto transferSeedDecks = loadStoredSeedDecks(transferSeedBucketForRequest, fullSorted);
        auto rankSeedDecks = buildRankSeedDecks(
            cardRankBucketForRequest,
            fullSorted,
            std::max(config.limit * 4, 12)
        );
        auto monoAttrSeedDecks = collectChallengeMonoAttrSeedDecks(config, &totalInfo, std::max(config.limit, 2));
        std::vector<const CardDetail*> warmStartDeck{};
        if (!rememberedSeedDecks.empty()) {
            warmStartDeck = rememberedSeedDecks.front();
        } else if (!transferSeedDecks.empty()) {
            warmStartDeck = transferSeedDecks.front();
        } else if (!rankSeedDecks.empty()) {
            warmStartDeck = rankSeedDecks.front();
        }
        int warmStartPrefixLimit = std::max(int(fixedCards.size()), config.member - 2);
        if (!rememberedSeedDecks.empty()) {
            int replayLimit = std::min(
                int(rememberedSeedDecks.size()),
                warmStartDeck.empty() ? config.limit : std::min(config.limit, 2)
            );
            for (int i = 0; i < replayLimit && !policyInfo.isTimeout(); ++i) {
                evaluateDeckByCards(config, rememberedSeedDecks[i], policyInfo);
            }
        }
        if (!transferSeedDecks.empty()) {
            int replayLimit = warmStartDeck.empty() ? 1 : std::min(int(transferSeedDecks.size()), 2);
            for (int i = 0; i < replayLimit && !policyInfo.isTimeout(); ++i) {
                evaluateDeckByCards(config, transferSeedDecks[i], policyInfo);
            }
        }
        if (!rankSeedDecks.empty()) {
            int replayLimit = warmStartDeck.empty() ? 1 : std::min(int(rankSeedDecks.size()), 2);
            for (int i = 0; i < replayLimit && !policyInfo.isTimeout(); ++i) {
                evaluateDeckByCards(config, rankSeedDecks[i], policyInfo);
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

        auto runPolicyEpisode = [&](bool allowExploration, bool updateWeights, const std::vector<const CardDetail*>* warmStart = nullptr) {
            std::vector<const CardDetail*> deck{};
            deck.reserve(config.member);
            std::unordered_set<int> usedCardIds{};
            std::unordered_set<int> usedCharacterIds{};
            for (const auto& card : fixedCards) {
                deck.push_back(&card);
                usedCardIds.insert(card.cardId);
                usedCharacterIds.insert(card.characterId);
            }
            if (warmStart != nullptr) {
                for (const auto* card : *warmStart) {
                    if (card == nullptr || int(deck.size()) >= warmStartPrefixLimit || usedCardIds.count(card->cardId)) {
                        continue;
                    }
                    if (!Enums::LiveType::isChallenge(liveType) && usedCharacterIds.count(card->characterId)) {
                        continue;
                    }
                    auto fixedCharacterIndex = int(deck.size()) - int(fixedCards.size());
                    if (fixedCharacterIndex >= 0
                        && remainingRequiredCharacters.size() > std::size_t(fixedCharacterIndex)
                        && remainingRequiredCharacters[fixedCharacterIndex] != card->characterId) {
                        continue;
                    }
                    deck.push_back(card);
                    usedCardIds.insert(card->cardId);
                    usedCharacterIds.insert(card->characterId);
                }
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

        auto runPolicyBeam = [&](int beamWidth, int branchWidth, const std::vector<const CardDetail*>* warmStart = nullptr) {
            struct BeamState {
                std::vector<const CardDetail*> deck{};
                std::unordered_set<int> usedCardIds{};
                std::unordered_set<int> usedCharacterIds{};
                double score = 0.0;
            };

            BeamState initial{};
            initial.deck.reserve(config.member);
            for (const auto& card : fixedCards) {
                initial.deck.push_back(&card);
                initial.usedCardIds.insert(card.cardId);
                initial.usedCharacterIds.insert(card.characterId);
            }
            if (warmStart != nullptr) {
                for (const auto* card : *warmStart) {
                    if (card == nullptr || int(initial.deck.size()) >= warmStartPrefixLimit || initial.usedCardIds.count(card->cardId)) {
                        continue;
                    }
                    if (!Enums::LiveType::isChallenge(liveType) && initial.usedCharacterIds.count(card->characterId)) {
                        continue;
                    }
                    auto fixedCharacterIndex = int(initial.deck.size()) - int(fixedCards.size());
                    if (fixedCharacterIndex >= 0
                        && remainingRequiredCharacters.size() > std::size_t(fixedCharacterIndex)
                        && remainingRequiredCharacters[fixedCharacterIndex] != card->characterId) {
                        continue;
                    }
                    initial.deck.push_back(card);
                    initial.usedCardIds.insert(card->cardId);
                    initial.usedCharacterIds.insert(card->characterId);
                }
            }

            std::vector<BeamState> beams{std::move(initial)};
            while (!beams.empty() && int(beams.front().deck.size()) < config.member && !policyInfo.isTimeout()) {
                std::vector<BeamState> nextBeams{};
                for (const auto& beam : beams) {
                    int nextPos = int(beam.deck.size());
                    std::vector<std::pair<double, const CardDetail*>> options{};
                    options.reserve(rlCards.size());
                    for (const auto& card : rlCards) {
                        if (beam.usedCardIds.count(card.cardId)) {
                            continue;
                        }
                        if (!Enums::LiveType::isChallenge(liveType) && beam.usedCharacterIds.count(card.characterId)) {
                            continue;
                        }
                        auto fixedCharacterIndex = nextPos - int(fixedCards.size());
                        if (fixedCharacterIndex >= 0
                            && remainingRequiredCharacters.size() > std::size_t(fixedCharacterIndex)
                            && remainingRequiredCharacters[fixedCharacterIndex] != card.characterId) {
                            continue;
                        }
                        auto features = calcActionFeatures(beam.deck, card);
                        double actionScore = 0.0;
                        for (int i = 0; i < RL_FEATURE_DIM; ++i) {
                            actionScore += bucket.weights[i] * features[i];
                        }
                        actionScore += 0.08 * scoreHeuristic(card) + 0.015 * cardEventBonus(card);
                        options.emplace_back(actionScore, &card);
                    }
                    std::sort(options.begin(), options.end(), [](const auto& a, const auto& b) {
                        if (std::abs(a.first - b.first) > 1e-9) {
                            return a.first > b.first;
                        }
                        return a.second->cardId < b.second->cardId;
                    });
                    int take = std::min(int(options.size()), branchWidth);
                    for (int i = 0; i < take; ++i) {
                        BeamState next = beam;
                        next.deck.push_back(options[i].second);
                        next.usedCardIds.insert(options[i].second->cardId);
                        next.usedCharacterIds.insert(options[i].second->characterId);
                        next.score += options[i].first;
                        nextBeams.push_back(std::move(next));
                    }
                }
                std::sort(nextBeams.begin(), nextBeams.end(), [](const BeamState& a, const BeamState& b) {
                    if (std::abs(a.score - b.score) > 1e-9) {
                        return a.score > b.score;
                    }
                    auto aLast = a.deck.empty() ? 0 : a.deck.back()->cardId;
                    auto bLast = b.deck.empty() ? 0 : b.deck.back()->cardId;
                    return aLast < bLast;
                });
                if (int(nextBeams.size()) > beamWidth) {
                    nextBeams.resize(beamWidth);
                }
                beams = std::move(nextBeams);
            }

            for (const auto& beam : beams) {
                if (policyInfo.isTimeout()) {
                    break;
                }
                if (int(beam.deck.size()) == config.member) {
                    evaluateDeckByCards(config, beam.deck, policyInfo);
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
            policyEpisodeCap = config.target == RecommendTarget::Score ? 32 : 16;
        }
        if (coldStartRequest && hasCardRankMemory) {
            policyEpisodeCap = std::min(policyEpisodeCap, config.target == RecommendTarget::Score ? 48 : 32);
        }

        int policyEpisodes = 0;
        runPolicyBeam(
            config.target == RecommendTarget::Score ? 12 : 8,
            config.target == RecommendTarget::Score ? 5 : 4,
            !warmStartDeck.empty() ? &warmStartDeck : nullptr
        );
        if (!warmStartDeck.empty() && !policyInfo.isTimeout()) {
            policyEpisodes++;
            runPolicyEpisode(true, true, &warmStartDeck);
        }
        while (!policyInfo.isTimeout() && policyEpisodes < policyEpisodeCap) {
            policyEpisodes++;
            runPolicyEpisode(true, true);
        }

        int greedyEpisodes = bucket.episodes >= 8 ? 2 : 1;
        if (hasRememberedSeeds && maturePolicy) {
            greedyEpisodes = config.target == RecommendTarget::Score ? 2 : 1;
        }
        for (int i = 0; i < greedyEpisodes && !policyInfo.isTimeout(); ++i) {
            runPolicyEpisode(false, false, !warmStartDeck.empty() && i == 0 ? &warmStartDeck : nullptr);
        }

        rememberStoredSeeds(seedBucket, policyInfo, std::max(config.limit * 3, 8));

        mergeCalcInfo(totalInfo, policyInfo);

        if (coldStartRequest && config.target == RecommendTarget::Score && bucket.episodes >= 48) {
            for (int warmRound = 0; warmRound < 2; ++warmRound) {
                auto warmedSeedDecks = loadStoredSeedDecks(seedBucket, fullSorted);
                if (warmedSeedDecks.empty()) {
                    break;
                }

                int warmReplayBudgetMs = std::min(config.timeout_ms, resolveBudgetMs(config.timeout_ms, 0.06, 35, 140));
                auto warmInfo = makeCalcInfo(warmReplayBudgetMs);
                int replayLimit = std::min(int(warmedSeedDecks.size()), std::max(config.limit * 4, 12));
                for (int i = 0; i < replayLimit && !warmInfo.isTimeout(); ++i) {
                    evaluateDeckByCards(config, warmedSeedDecks[i], warmInfo);
                }

                mergeCalcInfo(totalInfo, warmInfo);
                rememberStoredSeeds(seedBucket, totalInfo, std::max(config.limit * 3, 8));
            }
        }

        RlSeedBucket transferSeedBucket{};
        rememberStoredSeeds(transferSeedBucket, totalInfo, std::max(config.limit * 8, 32));
        RlCardRankBucket cardRankBucket{};
        rememberCardRanks(cardRankBucket, totalInfo, std::max(config.limit * 8, 24));
        {
            std::lock_guard<std::mutex> lock(rlMemoryMutex);
            auto& storedPolicyBucket = rlPolicyBuckets[rlStateKey];
            if (bucket.episodes >= storedPolicyBucket.episodes) {
                storedPolicyBucket = bucket;
            }
            mergeSeedBucket(rlSeedBuckets[rlStateKey], seedBucket, 16);
            mergeSeedBucket(rlSeedBuckets[transferSeedKey], transferSeedBucket, 32);
            mergeCardRankBucket(rlCardRankBuckets[broadCardRankKey], cardRankBucket, 160);
            mergeCardRankBucket(rlCardRankBuckets[cardRankKey], cardRankBucket, 160);
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
