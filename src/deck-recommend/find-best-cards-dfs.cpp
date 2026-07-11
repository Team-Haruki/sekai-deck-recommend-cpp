#include "deck-recommend/base-deck-recommend.h"

#include <limits>
#include <tuple>

template<typename T>
bool containsAny(const std::vector<T>& collection, const std::vector<T>& contains) {
    for (const auto& item : collection) {
        if (std::find(contains.begin(), contains.end(), item) != contains.end()) {
            return true;
        }
    }
    return false;
}

// 候选卡是否与当前卡组冲突（重复卡牌或非挑战下重复角色）
static inline bool conflictsWithDeck(
    const CardDetail* card,
    const std::vector<const CardDetail*>& deckCards,
    const std::bitset<32>& deckCharacters,
    bool isChallengeLive
) {
    if (!isChallengeLive && deckCharacters.test(card->characterId)) {
        return true;
    }
    for (const auto* deckCard : deckCards) {
        if (deckCard->cardId == card->cardId) {
            return true;
        }
    }
    return false;
}

static int calcPowerUpperBound(
    const std::vector<const CardDetail*>& deckCards,
    const std::bitset<32>& deckCharacters,
    const DfsSearchContext& searchContext,
    int member,
    bool isChallengeLive,
    int honorBonus
) {
    int selectedPowerSum = 0;
    int selectedCount = 0;
    for (const auto* deckCard : deckCards) {
        selectedPowerSum += deckCard->power.max;
        selectedCount++;
    }

    int needed = member - selectedCount;
    if (needed < 0) {
        return std::numeric_limits<int>::max();
    }

    int powerUpperBound = honorBonus + selectedPowerSum;
    int got = 0;
    if (searchContext.useCharacterBounds) {
        // 每角色最多1张：取未用角色的最大综合力前needed个
        for (int i = 0; i < searchContext.characterCount && got < needed; ++i) {
            if (deckCharacters.test(searchContext.charPowerOrder[i])) {
                continue;
            }
            powerUpperBound += searchContext.charPowerVals[i];
            got++;
        }
    } else {
        // 挑战live：卡牌已按综合力降序预排序，跳过冲突卡后取前needed张
        for (const auto* card : searchContext.byPowerDesc) {
            if (got >= needed) {
                break;
            }
            if (conflictsWithDeck(card, deckCards, deckCharacters, isChallengeLive)) {
                continue;
            }
            powerUpperBound += card->power.max;
            got++;
        }
    }
    if (got < needed) {
        return std::numeric_limits<int>::max();
    }
    return powerUpperBound;
}

// 收集当前卡组加上最优补位后的member个技能值（降序），返回是否能凑满
static bool collectOptimisticSkills(
    const std::vector<const CardDetail*>& deckCards,
    const std::bitset<32>& deckCharacters,
    const DfsSearchContext& searchContext,
    int member,
    bool isChallengeLive,
    std::array<double, 5>& skills
) {
    int count = 0;
    for (const auto* deckCard : deckCards) {
        skills[count++] = deckCard->skill.max;
    }
    if (searchContext.useCharacterBounds) {
        // 每角色最多1张：取未用角色的最大技能补位
        for (int i = 0; i < searchContext.characterCount && count < member; ++i) {
            if (deckCharacters.test(searchContext.charSkillOrder[i])) {
                continue;
            }
            skills[count++] = searchContext.charSkillVals[i];
        }
    } else {
        // 挑战live：卡牌已按技能降序预排序，跳过冲突卡补位
        for (const auto* card : searchContext.bySkillDesc) {
            if (count >= member) {
                break;
            }
            if (conflictsWithDeck(card, deckCards, deckCharacters, isChallengeLive)) {
                continue;
            }
            skills[count++] = card->skill.max;
        }
    }
    if (count < member) {
        return false;
    }
    std::sort(skills.begin(), skills.begin() + member, std::greater<>());
    return true;
}

static int calcOptimisticScore(
    LiveCalculator& liveCalculator,
    const DfsScoreUpperBoundContext& scoreUpperBoundContext,
    int liveType,
    const DeckRecommendConfig& cfg,
    const std::array<double, 5>& skills,
    int member,
    int powerUpperBound
) {
    // 乐观卡组每个结点都会构建，复用thread_local缓冲区
    static thread_local DeckDetail optimisticDeck{};
    optimisticDeck.power = {};
    optimisticDeck.power.total = powerUpperBound;
    optimisticDeck.cards.clear();
    optimisticDeck.cards.reserve(member);
    for (int i = 0; i < member; ++i) {
        optimisticDeck.cards.push_back(DeckCardDetail{
            .cardId = 0,
            .level = 0,
            .skillLevel = 0,
            .masterRank = 0,
            .power = {},
            .eventBonus = std::nullopt,
            .skill = DeckCardSkillDetail{
                .scoreUp = skills[i],
            },
            .episode1Read = false,
            .episode2Read = false,
            .afterTraining = false,
            .defaultImage = 0,
            .hasCanvasBonus = false,
        });
    }

    return liveCalculator.getLiveScoreByDeck(
        optimisticDeck,
        scoreUpperBoundContext.musicMeta,
        liveType,
        cfg.liveSkillOrder,
        cfg.specificSkillOrder,
        cfg.multiTeammateScoreUp,
        cfg.multiTeammatePower
    );
}

static double calcScoreUpperBound(
    LiveCalculator& liveCalculator,
    const DfsScoreUpperBoundContext& scoreUpperBoundContext,
    int liveType,
    const DeckRecommendConfig& cfg,
    const std::vector<const CardDetail*>& deckCards,
    const std::bitset<32>& deckCharacters,
    const DfsSearchContext& searchContext,
    int member,
    bool isChallengeLive,
    int honorBonus
) {
    auto powerUpperBound = calcPowerUpperBound(
        deckCards,
        deckCharacters,
        searchContext,
        member,
        isChallengeLive,
        honorBonus
    );
    if (powerUpperBound == std::numeric_limits<int>::max()) {
        return std::numeric_limits<double>::infinity();
    }

    std::array<double, 5> skills{};
    if (!collectOptimisticSkills(deckCards, deckCharacters, searchContext, member, isChallengeLive, skills)) {
        return std::numeric_limits<double>::infinity();
    }

    auto optimisticScore = calcOptimisticScore(
        liveCalculator, scoreUpperBoundContext, liveType, cfg, skills, member, powerUpperBound
    );
    return optimisticScore + double(optimisticScore) / SCORE_MAX;
}

static double calcSkillTargetUpperBound(
    LiveCalculator& liveCalculator,
    const DfsScoreUpperBoundContext& scoreUpperBoundContext,
    int liveType,
    const DeckRecommendConfig& cfg,
    const std::vector<const CardDetail*>& deckCards,
    const std::bitset<32>& deckCharacters,
    const DfsSearchContext& searchContext,
    int member,
    bool isChallengeLive,
    int honorBonus,
    std::optional<int> eventType
) {
    // 活动场景下不做技能上界估计，直接返回，避免无用功
    if (eventType.has_value()) {
        return std::numeric_limits<double>::infinity();
    }

    std::array<double, 5> skills{};
    if (!collectOptimisticSkills(deckCards, deckCharacters, searchContext, member, isChallengeLive, skills)) {
        return std::numeric_limits<double>::infinity();
    }

    double optimisticSkill = skills[0];
    for (int i = 1; i < member; ++i) {
        optimisticSkill += skills[i] * 0.2;
    }

    auto powerUpperBound = calcPowerUpperBound(
        deckCards,
        deckCharacters,
        searchContext,
        member,
        isChallengeLive,
        honorBonus
    );
    if (powerUpperBound == std::numeric_limits<int>::max()) {
        return std::numeric_limits<double>::infinity();
    }

    auto optimisticScore = calcOptimisticScore(
        liveCalculator, scoreUpperBoundContext, liveType, cfg, skills, member, powerUpperBound
    );
    return optimisticSkill + double(optimisticScore) / SCORE_MAX;
}


void BaseDeckRecommend::findBestCardsDFS(
    int liveType,
    const DeckRecommendConfig& cfg,
    const std::vector<CardDetail> &cardDetails,
    std::map<int, std::vector<SupportDeckCard>>& supportCards,
    const std::function<Score(const DeckDetail &)> &scoreFunc,
    RecommendCalcInfo& dfsInfo,
    int limit,
    bool isChallengeLive,
    int member,
    int honorBonus,
    std::optional<int> eventType,
    std::optional<int> eventId,
    const std::vector<CardDetail>& fixedCards,
    const DfsScoreUpperBoundContext* scoreUpperBoundContext
)
{
    // 递归中的所有不变量在入口预计算一次
    DfsSearchContext searchContext{};
    bool isWorldBloomFinale = eventId.has_value() && this->dataProvider.masterData->isWorldBloomFinale(eventId.value());
    searchContext.remainingFixedCharacters = resolveRemainingFixedCharacters(
        cfg,
        fixedCards,
        isWorldBloomFinale
    );

    if (scoreUpperBoundContext) {
        if (!isChallengeLive) {
            // 非挑战每角色最多1张：按角色最大值估上界，比逐卡top-k更紧
            searchContext.useCharacterBounds = true;
            std::array<int, 32> charMaxPower{};
            std::array<double, 32> charMaxSkill{};
            std::array<bool, 32> hasCard{};
            for (const auto& card : cardDetails) {
                auto ch = card.characterId;
                hasCard[ch] = true;
                charMaxPower[ch] = std::max(charMaxPower[ch], card.power.max);
                charMaxSkill[ch] = std::max(charMaxSkill[ch], double(card.skill.max));
            }
            for (int ch = 0; ch < 32; ++ch) {
                if (hasCard[ch]) {
                    searchContext.charPowerOrder[searchContext.characterCount] = ch;
                    searchContext.charSkillOrder[searchContext.characterCount] = ch;
                    searchContext.characterCount++;
                }
            }
            auto powerBegin = searchContext.charPowerOrder.begin();
            std::sort(powerBegin, powerBegin + searchContext.characterCount, [&](int a, int b) {
                return charMaxPower[a] > charMaxPower[b];
            });
            auto skillBegin = searchContext.charSkillOrder.begin();
            std::sort(skillBegin, skillBegin + searchContext.characterCount, [&](int a, int b) {
                return charMaxSkill[a] > charMaxSkill[b];
            });
            for (int i = 0; i < searchContext.characterCount; ++i) {
                searchContext.charPowerVals[i] = charMaxPower[searchContext.charPowerOrder[i]];
                searchContext.charSkillVals[i] = charMaxSkill[searchContext.charSkillOrder[i]];
            }
        } else {
            // 挑战live角色可重复上场，仍按卡降序取top-k
            searchContext.byPowerDesc.reserve(cardDetails.size());
            for (const auto& card : cardDetails) {
                searchContext.byPowerDesc.push_back(&card);
            }
            searchContext.bySkillDesc = searchContext.byPowerDesc;
            std::sort(searchContext.byPowerDesc.begin(), searchContext.byPowerDesc.end(), [](const CardDetail* a, const CardDetail* b) {
                return a->power.max > b->power.max;
            });
            std::sort(searchContext.bySkillDesc.begin(), searchContext.bySkillDesc.end(), [](const CardDetail* a, const CardDetail* b) {
                return a->skill.max > b->skill.max;
            });
        }
    }

    findBestCardsDFSImpl(
        liveType, cfg, cardDetails, supportCards, scoreFunc, dfsInfo,
        limit, isChallengeLive, member, honorBonus, eventType, eventId, fixedCards,
        scoreUpperBoundContext, searchContext
    );
}

void BaseDeckRecommend::findBestCardsDFSImpl(
    int liveType,
    const DeckRecommendConfig& cfg,
    const std::vector<CardDetail> &cardDetails,
    std::map<int, std::vector<SupportDeckCard>>& supportCards,
    const std::function<Score(const DeckDetail &)> &scoreFunc,
    RecommendCalcInfo& dfsInfo,
    int limit,
    bool isChallengeLive,
    int member,
    int honorBonus,
    std::optional<int> eventType,
    std::optional<int> eventId,
    const std::vector<CardDetail>& fixedCards,
    const DfsScoreUpperBoundContext* scoreUpperBoundContext,
    const DfsSearchContext& searchContext
)
{
    // 超时
    if (dfsInfo.isTimeout()) {
        return;
    }

    auto& deckCards = dfsInfo.deckCards;
    auto& deckCharacters = dfsInfo.deckCharacters;
    auto& remainingFixedCharacters = searchContext.remainingFixedCharacters;

    // 防止挑战Live卡的数量小于允许上场的数量导致无法组队
    if (isChallengeLive) {
        member = std::min(member, int(cardDetails.size()));
    }
    // 已经是完整卡组，计算当前卡组的值
    if (int(deckCards.size()) == member) {
        auto ret = getBestPermutation(
            this->deckCalculator, deckCards, supportCards, scoreFunc,
            honorBonus, eventType, eventId, liveType, cfg
        );
        if (ret.bestDeck.has_value())
            dfsInfo.update(ret.bestDeck.value(), limit);
        return;
    }

    // 非完整卡组，继续遍历所有情况
    const CardDetail* preCard = nullptr;

    for (const auto& card : cardDetails) {
        // 跳过已经重复出现过的卡牌
        bool has_card = false;
        for (const auto& deckCard : deckCards) {
            if (deckCard->cardId == card.cardId) {
                has_card = true;
                break;
            }
        }
        if (has_card) continue;

        // 跳过重复角色
        if (!isChallengeLive && deckCharacters.test(card.characterId)) continue;
        // 固定角色中已经由 fixedCards 满足的部分不再额外占卡位。
        auto fixedCharacterIndex = int(deckCards.size()) - int(fixedCards.size());
        if (fixedCharacterIndex >= 0
            && remainingFixedCharacters.size() > std::size_t(fixedCharacterIndex)
            && remainingFixedCharacters[fixedCharacterIndex] != card.characterId) {
            continue;
        }

        // C位相关优化，如果使用固定卡牌，则认为C位是第一个不固定的位置，后面的同理（即固定卡牌不参加剪枝）
        auto cIndex = fixedCards.size() + remainingFixedCharacters.size();
        if (cfg.target != RecommendTarget::Skill && cfg.target != RecommendTarget::Bonus) {
            // C位一定是技能最好的卡牌，跳过技能比C位还好的
            if (deckCards.size() >= cIndex + 1 && deckCards[cIndex]->skill.isCertainlyLessThan(card.skill)) continue;
            // 为了优化性能，必须和C位同色或同组
            if (deckCards.size() >= cIndex + 1 && card.attr != deckCards[cIndex]->attr && !containsAny(deckCards[cIndex]->units, card.units)) {
                continue;
            }
        }
        else if (cfg.target == RecommendTarget::Bonus && deckCards.size() >= cIndex + 1) {
            auto& last = *deckCards.back();
            auto candidateKey = std::make_tuple(
                card.maxEventBonus.value_or(0.0),
                card.power.max,
                card.skill.max,
                card.cardId
            );
            auto lastKey = std::make_tuple(
                last.maxEventBonus.value_or(0.0),
                last.power.max,
                last.skill.max,
                last.cardId
            );
            if (candidateKey > lastKey) continue;
        }
        else if (deckCards.size() >= cIndex + 1) {
            auto& last = *deckCards.back();
            auto candidateKey = std::make_tuple(card.skill.max, card.skill.min, card.cardId);
            auto lastKey = std::make_tuple(last.skill.max, last.skill.min, last.cardId);
            // 实效目标下排列本身不改变最终最佳队长，只保留一种稳定组合顺序，避免同卡组排列爆炸。
            if (candidateKey > lastKey) continue;
        }

        if (cfg.target != RecommendTarget::Skill && cfg.target != RecommendTarget::Bonus && deckCards.size() >= cIndex + 2) {
            auto& last = *deckCards.back();
            bool lessThan = false;
            bool greaterThan = false;
            if (cfg.target == RecommendTarget::Score) {
                lessThan = this->cardCalculator.isCertainlyLessThan(last, card);
                greaterThan = this->cardCalculator.isCertainlyLessThan(card, last);
            } else if(cfg.target == RecommendTarget::Power) {
                lessThan = last.power.isCertainlyLessThan(card.power);
                greaterThan = card.power.isCertainlyLessThan(last.power);
            } else if (cfg.target == RecommendTarget::Skill) {
                lessThan = last.skill.isCertainlyLessThan(card.skill);
                greaterThan = card.skill.isCertainlyLessThan(last.skill);
            }
            // 要求生成的卡组后面4个位置按强弱排序、同强度按卡牌ID排序
            // 如果上一张卡肯定小，那就不符合顺序；
            if (lessThan) continue;
            // 在旗鼓相当的前提下（因为两两组合有四种情况，再排除掉这张卡肯定小的情况，就是旗鼓相当），要ID小
            if (!greaterThan && card.cardId > last.cardId) continue;
        }

        if (cfg.target != RecommendTarget::Skill && preCard) {
            auto& pre = *preCard;
            bool lessThan = false;

            if (cfg.target == RecommendTarget::Score) {
                lessThan = this->cardCalculator.isCertainlyLessThan(card, pre);
            } else if (cfg.target == RecommendTarget::Power) {
                lessThan = card.power.isCertainlyLessThan(pre.power);
            } else if (cfg.target == RecommendTarget::Skill) {
                lessThan = card.skill.isCertainlyLessThan(pre.skill);
            } else if (cfg.target == RecommendTarget::Mysekai) {
                lessThan = this->cardCalculator.isCertainlyLessThan(card, pre, true, false, true);
            } else if (cfg.target == RecommendTarget::Bonus) {
                lessThan = this->cardCalculator.isCertainlyLessThan(card, pre, true, false, true);
            }

            if (cfg.target == RecommendTarget::Score) {
                // 如果肯定比上一次选定的卡牌要弱，那么舍去，让这张卡去后面再选
                // 该优化较为激进，未考虑卡的协同效应，在计算分数最优的情况下才使用
                if (lessThan) continue;
            } else {
                // 计算实效或综合力最优时性能够用，使用较温和的优化
                if (lessThan && deckCards.size() != member - 1) continue;
            }
        }
        preCard = &card;

        // 递归，寻找所有情况
        deckCards.push_back(&card);
        deckCharacters.flip(card.characterId);

        bool prunedByBound = false;

        if (!prunedByBound) {
            if (cfg.target == RecommendTarget::Score
                && scoreUpperBoundContext
                && dfsInfo.deckQueue.size() >= std::size_t(limit)) {
                auto optimistic = calcScoreUpperBound(
                    this->liveCalculator,
                    *scoreUpperBoundContext,
                    liveType,
                    cfg,
                    deckCards,
                    deckCharacters,
                    searchContext,
                    member,
                    isChallengeLive,
                    honorBonus
                );
                if (optimistic <= dfsInfo.deckQueue.top().targetValue) {
                    prunedByBound = true;
                }
            }
            else if (cfg.target == RecommendTarget::Skill
                && scoreUpperBoundContext
                && dfsInfo.deckQueue.size() >= std::size_t(limit)) {
                auto optimistic = calcSkillTargetUpperBound(
                    this->liveCalculator,
                    *scoreUpperBoundContext,
                    liveType,
                    cfg,
                    deckCards,
                    deckCharacters,
                    searchContext,
                    member,
                    isChallengeLive,
                    honorBonus,
                    eventType
                );
                if (optimistic <= dfsInfo.deckQueue.top().targetValue) {
                    prunedByBound = true;
                }
            }
        }

        if (!prunedByBound) {
            findBestCardsDFSImpl(
                liveType, cfg, cardDetails, supportCards, scoreFunc, dfsInfo,
                limit, isChallengeLive, member, honorBonus, eventType, eventId, fixedCards,
                scoreUpperBoundContext, searchContext
            );
        }

        deckCards.pop_back();
        deckCharacters.flip(card.characterId);
    }
}
