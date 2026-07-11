#include "deck-recommend/base-deck-recommend.h"

#include <limits>
#include <tuple>
#include <unordered_map>

// 支配裁剪的判定（做法来自allium-deck）：
// A支配B ⇒ 任何含B的卡组把B换成A都不会变差，因此B可以先从搜索池剔除，
// 搜索完成后再通过替代展开把被裁卡代回结果卡组补全Top-N。
// 只有同角色（保证A/B不能同队）、同属性、同组合（保证换卡不影响其他卡的加成上下文）
// 且各维度区间严格更差时才成立；含期间限定/队长加成的卡与加成上限交互复杂，不参与。
static bool sameUnitSet(const CardDetail& a, const CardDetail& b) {
    if (a.units.size() != b.units.size()) {
        return false;
    }
    for (const auto unit : a.units) {
        if (std::find(b.units.begin(), b.units.end(), unit) == b.units.end()) {
            return false;
        }
    }
    return true;
}

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

// 构建乐观卡组（每个结点都会构建，复用thread_local缓冲区）
static DeckDetail& buildOptimisticDeck(
    const std::array<double, 5>& skills,
    int member,
    int powerUpperBound
) {
    static thread_local DeckDetail optimisticDeck{};
    optimisticDeck.power = {};
    optimisticDeck.power.total = powerUpperBound;
    optimisticDeck.eventBonus = std::nullopt;
    optimisticDeck.supportDeckBonus = std::nullopt;
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
    return optimisticDeck;
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
    auto& optimisticDeck = buildOptimisticDeck(skills, member, powerUpperBound);
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

// 活动加成上界：卡组内已选卡的加成 + 未用角色的最大加成前needed个 + 支援/异色加成上界
static double calcBonusUpperBound(
    const std::vector<const CardDetail*>& deckCards,
    const std::bitset<32>& deckCharacters,
    const DfsSearchContext& searchContext,
    int member
) {
    double bonusUpperBound = searchContext.diffAttrBonusUpperBound;
    int selectedCount = 0;
    for (const auto* deckCard : deckCards) {
        bonusUpperBound += deckCard->maxEventBonus.value_or(0.0);
        selectedCount++;
    }
    int needed = member - selectedCount;
    int got = 0;
    for (int i = 0; i < searchContext.characterCount && got < needed; ++i) {
        if (deckCharacters.test(searchContext.charBonusOrder[i])) {
            continue;
        }
        bonusUpperBound += searchContext.charBonusVals[i];
        got++;
    }
    return bonusUpperBound;
}

static double calcScoreUpperBound(
    LiveCalculator& liveCalculator,
    const DfsScoreUpperBoundContext& scoreUpperBoundContext,
    int liveType,
    const DeckRecommendConfig& cfg,
    const std::function<Score(const DeckDetail &)>& scoreFunc,
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

    if (searchContext.useEventPointBound) {
        // 活动卡组：乐观卡组带上加成上界直接过scoreFunc，得到活动PT量级的上界，
        // 与deckQueue中的targetValue同量级比较（活动点数公式对各输入单调，上界成立）
        auto& optimisticDeck = buildOptimisticDeck(skills, member, powerUpperBound);
        optimisticDeck.eventBonus = calcBonusUpperBound(deckCards, deckCharacters, searchContext, member);
        optimisticDeck.supportDeckBonus = searchContext.supportDeckBonusUpperBound;
        auto score = scoreFunc(optimisticDeck);
        return score.score + double(score.liveScore) / SCORE_MAX;
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

    // 逐角色支配裁剪（做法来自allium-deck）：先从搜索池剔除被同角色卡全面支配的卡，
    // 搜索后再把被裁卡代回结果卡组补全Top-N。
    // 限定在Score目标、非挑战（挑战同角色可同队，换卡论证不成立）、
    // 非WL（主队卡会从支援卡组中排除，纯主队维度的支配不完备）。
    std::vector<CardDetail> prunedPool{};
    std::unordered_map<int, std::vector<const CardDetail*>> alternativesByRootCardId{};
    const std::vector<CardDetail>* searchPool = &cardDetails;
    if (!isChallengeLive
        && cfg.target == RecommendTarget::Score
        && eventType != Enums::EventType::world_bloom
        && !isWorldBloomFinale) {
        int cardCount = int(cardDetails.size());
        std::vector<int> dominatedBy(cardCount, -1);
        std::array<std::vector<int>, 32> indicesByCharacter{};
        for (int i = 0; i < cardCount; ++i) {
            indicesByCharacter[cardDetails[i].characterId].push_back(i);
        }

        auto isFixedCard = [&](const CardDetail& card) {
            for (const auto& fixedCard : fixedCards) {
                if (fixedCard.cardId == card.cardId) {
                    return true;
                }
            }
            return false;
        };
        // A支配B：同属性、同组合（换卡不改变其他卡的加成上下文），
        // 且综合力/技能/活动加成的区间严格全面更优；
        // 期间限定加成与生效数量上限交互、终章队长加成与队长位交互，含这些加成的卡不参与
        auto dominates = [&](const CardDetail& a, const CardDetail& b) {
            if (a.attr != b.attr || !sameUnitSet(a, b)) {
                return false;
            }
            if (a.limitedEventBonus.value_or(0.0) > 0.0 || b.limitedEventBonus.value_or(0.0) > 0.0) {
                return false;
            }
            if (a.leaderHonorEventBonus.value_or(0.0) > 0.0 || b.leaderHonorEventBonus.value_or(0.0) > 0.0
                || a.leaderLimitEventBonus.value_or(0.0) > 0.0 || b.leaderLimitEventBonus.value_or(0.0) > 0.0) {
                return false;
            }
            return this->cardCalculator.isCertainlyLessThan(b, a);
        };

        for (const auto& group : indicesByCharacter) {
            for (const auto bi : group) {
                if (isFixedCard(cardDetails[bi])) {
                    continue;
                }
                for (const auto ai : group) {
                    if (ai == bi) {
                        continue;
                    }
                    if (dominates(cardDetails[ai], cardDetails[bi])) {
                        dominatedBy[bi] = ai;
                        break;
                    }
                }
            }
        }

        bool anyDominated = false;
        for (int i = 0; i < cardCount; ++i) {
            if (dominatedBy[i] >= 0) {
                anyDominated = true;
                break;
            }
        }
        if (anyDominated) {
            prunedPool.reserve(cardCount);
            for (int i = 0; i < cardCount; ++i) {
                if (dominatedBy[i] < 0) {
                    prunedPool.push_back(cardDetails[i]);
                } else {
                    // 支配关系严格且可传递：沿链找到存活根，被裁卡登记为根的替代
                    int root = dominatedBy[i];
                    while (dominatedBy[root] >= 0) {
                        root = dominatedBy[root];
                    }
                    alternativesByRootCardId[cardDetails[root].cardId].push_back(&cardDetails[i]);
                }
            }
            searchPool = &prunedPool;
        }
    }
    const auto& pool = *searchPool;

    if (scoreUpperBoundContext) {
        if (!isChallengeLive) {
            // 非挑战每角色最多1张：按角色最大值估上界，比逐卡top-k更紧
            searchContext.useCharacterBounds = true;
            std::array<int, 32> charMaxPower{};
            std::array<double, 32> charMaxSkill{};
            std::array<bool, 32> hasCard{};
            for (const auto& card : pool) {
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

            // 活动卡组的上界必须换算到活动PT量级才能与targetValue比较，
            // 这里预计算加成上界所需数据（活动加成按角色取最大、支援/异色加成取全局上界）
            if (cfg.target == RecommendTarget::Score && eventType.has_value()) {
                searchContext.useEventPointBound = true;
                std::array<double, 32> charMaxBonus{};
                for (const auto& card : pool) {
                    charMaxBonus[card.characterId] = std::max(
                        charMaxBonus[card.characterId],
                        card.maxEventBonus.value_or(0.0)
                    );
                }
                searchContext.charBonusOrder = searchContext.charPowerOrder;
                auto bonusBegin = searchContext.charBonusOrder.begin();
                std::sort(bonusBegin, bonusBegin + searchContext.characterCount, [&](int a, int b) {
                    return charMaxBonus[a] > charMaxBonus[b];
                });
                for (int i = 0; i < searchContext.characterCount; ++i) {
                    searchContext.charBonusVals[i] = charMaxBonus[searchContext.charBonusOrder[i]];
                }

                if (eventType == Enums::EventType::world_bloom) {
                    // 支援卡组加成上界：不考虑主队互斥，直接取加成最高的前N张之和
                    int supportCount = this->deckCalculator.getWorldBloomSupportDeckCount(eventId.value_or(0));
                    for (const auto& [characterId, supportList] : supportCards) {
                        double sum = 0.0;
                        int used = 0;
                        for (const auto& supportCard : supportList) {
                            sum += supportCard.bonus;
                            if (++used >= supportCount) {
                                break;
                            }
                        }
                        searchContext.supportDeckBonusUpperBound = std::max(searchContext.supportDeckBonusUpperBound, sum);
                    }
                    // WL异色加成上界
                    for (const auto& bonus : this->dataProvider.masterData->worldBloomDifferentAttributeBonuses) {
                        searchContext.diffAttrBonusUpperBound = std::max(searchContext.diffAttrBonusUpperBound, bonus.bonusRate);
                    }
                }
            }
        } else {
            // 挑战live角色可重复上场，仍按卡降序取top-k
            searchContext.byPowerDesc.reserve(pool.size());
            for (const auto& card : pool) {
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
        liveType, cfg, pool, supportCards, scoreFunc, dfsInfo,
        limit, isChallengeLive, member, honorBonus, eventType, eventId, fixedCards,
        scoreUpperBoundContext, searchContext
    );

    // 替代展开：把被支配裁掉的卡代回Top-K结果卡组重新评估，补全同分/次优名次。
    // 替代卡与根卡同角色，代入不会产生新的角色/卡牌冲突。
    if (!alternativesByRootCardId.empty()) {
        std::unordered_map<int, const CardDetail*> cardById{};
        cardById.reserve(cardDetails.size());
        for (const auto& card : cardDetails) {
            cardById.emplace(card.cardId, &card);
        }

        std::vector<RecommendDeck> rootDecks{};
        {
            auto queueCopy = dfsInfo.deckQueue;
            while (!queueCopy.empty()) {
                rootDecks.push_back(queueCopy.top());
                queueCopy.pop();
            }
        }

        for (const auto& rootDeck : rootDecks) {
            // member<5的结果会用队长卡补位，按唯一卡ID还原卡组
            std::vector<const CardDetail*> baseDeck{};
            std::vector<const std::vector<const CardDetail*>*> alternativeLists{};
            bool valid = true;
            bool hasAlternative = false;
            for (const auto& deckCard : rootDeck.cards) {
                bool duplicated = false;
                for (const auto* existing : baseDeck) {
                    if (existing->cardId == deckCard.cardId) {
                        duplicated = true;
                        break;
                    }
                }
                if (duplicated) {
                    continue;
                }
                auto it = cardById.find(deckCard.cardId);
                if (it == cardById.end()) {
                    valid = false;
                    break;
                }
                baseDeck.push_back(it->second);
                auto altIt = alternativesByRootCardId.find(deckCard.cardId);
                alternativeLists.push_back(altIt != alternativesByRootCardId.end() ? &altIt->second : nullptr);
                hasAlternative |= alternativeLists.back() != nullptr;
            }
            if (!valid || !hasAlternative) {
                continue;
            }

            // 逐位置枚举替代（含多位置组合），设上限防爆
            int evalBudget = 128;
            std::vector<const CardDetail*> candidate = baseDeck;
            auto expand = [&](auto&& self, std::size_t pos, bool substituted) -> void {
                if (evalBudget <= 0 || dfsInfo.isTimeout()) {
                    return;
                }
                if (pos == baseDeck.size()) {
                    if (!substituted) {
                        return;
                    }
                    evalBudget--;
                    auto ret = getBestPermutation(
                        this->deckCalculator, candidate, supportCards, scoreFunc,
                        honorBonus, eventType, eventId, liveType, cfg
                    );
                    if (ret.bestDeck.has_value()) {
                        dfsInfo.update(ret.bestDeck.value(), limit);
                    }
                    return;
                }
                candidate[pos] = baseDeck[pos];
                self(self, pos + 1, substituted);
                if (alternativeLists[pos] != nullptr) {
                    for (const auto* alternative : *alternativeLists[pos]) {
                        candidate[pos] = alternative;
                        self(self, pos + 1, true);
                    }
                    candidate[pos] = baseDeck[pos];
                }
            };
            expand(expand, 0, false);
        }
    }
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
                    scoreFunc,
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
