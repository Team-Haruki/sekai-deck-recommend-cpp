#include "deck-recommend/base-deck-recommend.h"
#include "card-priority/card-priority-filter.h"
#include "common/timer.h"
#include <chrono>
#include <cmath>
#include <random>


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

        for (const auto& characterId : resolveRequiredCharacters(config, eventId)) {
            if (!characterIds.count(characterId)) {
                return {};
            }
        }
    }

    if (auto leaderCharacterId = resolveLeaderCharacterId(config, eventId)) {
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
    // 终章活动不允许把技能最强的换到队长
    if (eventId.has_value() && eventId.value() == finalChapterEventId) bestSkillAsLeader = false;
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

    auto requiredCharacters = resolveRequiredCharacters(config, eventConfig.eventId);
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
    // 终章技能加分上限为140
    if (eventConfig.eventId == finalChapterEventId && !Enums::LiveType::isChallenge(liveType))
        scoreUpLimit = 140.0;

    auto cards = cardCalculator.batchGetCardDetail(
        userCards, config.cardConfig, config.singleCardConfig, 
        eventConfig, areaItemLevels, scoreUpLimit,
        config.customBonusCharacterIds, config.customBonusAttr, config.customBonusSupportUnits
    );

    // 归类支援卡组
    std::map<int, std::vector<SupportDeckCard>> supportCards{};
    if (eventConfig.eventId == finalChapterEventId) {
        // 终章对每个角色都算一个支援卡组排序
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

    // 过滤箱活的卡，不上其它组合的
    if (eventConfig.eventUnit && config.filterOtherUnit) {
        std::vector<CardDetail> newCards{};
        for (const auto& card : cards) {
            if ((card.units.size() == 1 && card.units[0] == Enums::Unit::piapro) || 
                std::find(card.units.begin(), card.units.end(), eventConfig.eventUnit) != card.units.end()) {
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
        } else if (config.target == RecommendTarget::Mysekai) {
            std::sort(input.begin(), input.end(), [&](const CardDetail& a, const CardDetail& b) {
                return std::make_tuple(scoreHeuristic(a), cardEventBonus(a), a.power.max, a.cardId)
                    > std::make_tuple(scoreHeuristic(b), cardEventBonus(b), b.power.max, b.cardId);
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
        if (runConfig.target == RecommendTarget::Score) {
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

        if (config.target == RecommendTarget::Mysekai) {
            auto eventSorted = sortedCards;
            std::sort(eventSorted.begin(), eventSorted.end(), [&](const CardDetail& a, const CardDetail& b) {
                return std::make_tuple(cardEventBonus(a), a.power.max, a.cardId)
                    > std::make_tuple(cardEventBonus(b), b.power.max, b.cardId);
            });
            int eventKeep = std::min(int(eventSorted.size()), std::max(config.member * 18, 90));
            for (int i = 0; i < eventKeep; ++i) {
                tryAdd(eventSorted[i]);
            }

            std::array<int, 32> perCharacterEventCount{};
            int perCharacterEventKeep = Enums::LiveType::isChallenge(liveType) ? std::max(config.member, 3) : 4;
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
        } else if (config.target == RecommendTarget::Mysekai) {
            auto eventSorted = sortedCards;
            std::sort(eventSorted.begin(), eventSorted.end(), [&](const CardDetail& a, const CardDetail& b) {
                return std::make_tuple(cardEventBonus(a), a.power.max, a.cardId)
                    > std::make_tuple(cardEventBonus(b), b.power.max, b.cardId);
            });
            int eventKeep = std::min(int(eventSorted.size()), std::max(config.member * 24, 120));
            for (int i = 0; i < eventKeep; ++i) {
                tryAdd(eventSorted[i]);
            }

            std::array<int, 32> perCharacterEventCount{};
            int perCharacterEventKeep = Enums::LiveType::isChallenge(liveType) ? std::max(config.member, 3) : 5;
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

            auto minKeep = std::min(sortedCards.size(), std::size_t(std::max(config.member * 24, 120)));
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
    auto ensureResults = [&](std::vector<RecommendDeck> decks) {
        if (decks.empty()) {
            throw std::runtime_error("Cannot recommend any deck in " + std::to_string(cards.size()) + " cards");
        }
        return decks;
    };

    // 指定活动加成组卡
    if (config.target == RecommendTarget::Bonus) {
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
        return ans;
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
        runGaSearch(config, cards, calcInfo);
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

        auto fullSorted = sortCardsByStrength(cards);
        auto remainingRequiredCharacters = resolveRemainingFixedCharacters(config, fixedCards, eventConfig.eventId);
        auto leaderCharacterId = resolveLeaderCharacterId(config, eventConfig.eventId);
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
        auto seedKey = policyKey + ":" + std::to_string(cardFingerprint);
        auto& bucket = rlPolicyBuckets[policyKey];
        if (config.target == RecommendTarget::Mysekai && bucket.episodes == 0) {
            bucket.weights = {
                0.10, 1.20, 1.45, 0.15, 0.10,
                0.90, 0.35, 0.20, -0.05, 1.10,
                1.55, 0.25, 1.50
            };
        }
        auto& seedBucket = rlSeedBuckets[seedKey];

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

        auto calcStoredSeedHash = [&](const std::array<int, 5>& cardIds, int size) {
            auto ids = cardIds;
            std::sort(ids.begin() + 1, ids.begin() + size);
            constexpr uint64_t base = 10007;
            uint64_t hash = 0;
            for (int i = 0; i < size; ++i) {
                hash = hash * base + ids[i];
            }
            return hash;
        };
        auto loadStoredSeedDecks = [&](const std::vector<CardDetail>& sourceCards) {
            std::unordered_map<int, const CardDetail*> cardById{};
            for (const auto& card : sourceCards) {
                cardById.emplace(card.cardId, &card);
            }

            std::vector<std::vector<const CardDetail*>> seeds{};
            for (const auto& stored : seedBucket.bestSeeds) {
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
        auto rememberStoredSeeds = [&](const RecommendCalcInfo& info, int maxSeedCount) {
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

                auto it = std::find_if(seedBucket.bestSeeds.begin(), seedBucket.bestSeeds.end(), [&](const RlStoredSeed& existing) {
                    return existing.hash == seed.hash;
                });
                if (it == seedBucket.bestSeeds.end()) {
                    seedBucket.bestSeeds.push_back(seed);
                } else if (seed.targetValue > it->targetValue) {
                    *it = seed;
                }
            }
            std::sort(seedBucket.bestSeeds.begin(), seedBucket.bestSeeds.end(), [](const RlStoredSeed& a, const RlStoredSeed& b) {
                if (a.targetValue != b.targetValue) {
                    return a.targetValue > b.targetValue;
                }
                return a.hash < b.hash;
            });
            if (seedBucket.bestSeeds.size() > 16) {
                seedBucket.bestSeeds.resize(16);
            }
        };
        auto rememberedSeedDecks = loadStoredSeedDecks(fullSorted);
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

        auto calcActionFeatures = [&](const std::vector<const CardDetail*>& deck, const CardDetail& candidate) {
            std::array<double, RL_FEATURE_DIM> features{};
            double maxPower = std::max(1, fullSorted.empty() ? 1 : fullSorted.front().power.max);
            double maxSkill = std::max(1, fullSorted.empty() ? 1 : fullSorted.front().skill.max);
            double eventNorm = cardEventBonus(candidate) / 70.0;
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
            features[1] = candidate.power.max / maxPower;
            features[2] = config.target == RecommendTarget::Mysekai ? eventNorm : candidate.skill.max / maxSkill;
            features[3] = leader ? double(candidate.attr == leader->attr) : 0.0;
            features[4] = leader ? double(sharesAnyUnit(*leader, candidate)) : 0.0;
            features[5] = requiredCharacter
                ? double(candidate.characterId == remainingRequiredCharacters[fixedCharacterIndex])
                : 0.0;
            features[6] = double(sameAttrCount + 1) / double(std::max(1, config.member));
            features[7] = double(sharedUnitCount + 1) / double(std::max(1, config.member));
            features[8] = double(nextPos) / double(std::max(1, config.member));
            features[9] = nextPos == int(fixedCards.size())
                ? (config.target == RecommendTarget::Mysekai ? scoreHeuristic(candidate) : candidate.skill.max / maxSkill)
                : 0.0;
            features[10] = eventNorm;
            features[11] = candidate.supportDeckBonus.value_or(0.0) / 50.0;
            features[12] = scoreHeuristic(candidate);
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

        rememberStoredSeeds(policyInfo, std::max(config.limit * 3, 8));

        mergeCalcInfo(totalInfo, policyInfo);

        auto seedDecks = collectSeedDecks(policyInfo, fullSorted, std::max(config.limit * 4, 12));
        std::unordered_set<uint64_t> seedHashes{};
        std::vector<std::vector<const CardDetail*>> mergedSeedDecks{};
        auto tryAddSeedDeck = [&](const std::vector<const CardDetail*>& seedDeck) {
            if (!deckMatchesFixedConstraints(seedDeck)) {
                return;
            }
            auto normalizedSeedDeck = normalizeDeckForLeader(seedDeck);
            auto hash = this->calcDeckHash(normalizedSeedDeck);
            if (seedHashes.insert(hash).second) {
                mergedSeedDecks.push_back(std::move(normalizedSeedDeck));
            }
        };
        for (const auto& seedDeck : seedDecks) {
            tryAddSeedDeck(seedDeck);
        }
        for (const auto& seedDeck : rememberedSeedDecks) {
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
            rememberStoredSeeds(totalInfo, std::max(config.limit * 3, 8));
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
        return ans;
    }

    throw std::runtime_error("Unknown algorithm: " + std::to_string(int(config.algorithm)));
}
