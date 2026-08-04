#include "deck-recommend/base-deck-recommend.h"

#include <array>
#include <cmath>
#include <cstdint>

static int getCharaBonusKey(int chara, int bonus) {
    return bonus * 100 + chara;
}
static int getBonus(int key) {
    return key / 100;
}
static int getChara(int key) {
    return key % 100;
}
static std::pair<int, int> getBonusChara(int key) {
    return {getBonus(key), getChara(key)};
}

struct BonusSearchEntry {
    int key;
    int bonus;
    int chara;
    const CardDetail *card;
};

using BonusDeck = std::array<const CardDetail *, 5>;

// 分层过滤加成
using BonusFilter = std::function<bool(int key)>;
static const std::vector<BonusFilter> bonusFilters = {
    // 各个组合各自组卡
    [](int key) {
        int chara = getChara(key);
        return (chara - 1) / 4 == 0 || chara > 20;
    },
    [](int key) {
        int chara = getChara(key);
        return (chara - 1) / 4 == 1 || chara > 20;
    },
    [](int key) {
        int chara = getChara(key);
        return (chara - 1) / 4 == 2 || chara > 20;
    },
    [](int key) {
        int chara = getChara(key);
        return (chara - 1) / 4 == 3 || chara > 20;
    },
    [](int key) {
        int chara = getChara(key);
        return (chara - 1) / 4 == 4 || chara > 20;
    },
    // 最后一级：全部
    [](int) {
        return true; 
    },
};
static std::vector<BonusSearchEntry> applyFilter(
    const BonusFilter &filter,
    const std::vector<BonusSearchEntry> &entries
) {
    std::vector<BonusSearchEntry> ret;
    ret.reserve(entries.size());
    for (const auto &entry : entries) {
        if (filter(entry.key))
            ret.push_back(entry);
    }
    return ret;
}


static bool dfsBonus(
    const DeckRecommendConfig &config, 
    RecommendCalcInfo &dfsInfo, 
    std::vector<int> &targets,
    int currentBonus,
    int depth,
    std::size_t startIndex,
    BonusDeck &current,
    std::map<int, std::vector<BonusDeck>> &result,
    const std::vector<BonusSearchEntry> &entries,
    std::uint64_t charaMask
)
{
    if (depth == config.member) {
        auto target = std::lower_bound(targets.begin(), targets.end(), currentBonus);
        if (target != targets.end() && *target == currentBonus) {
            result[currentBonus].push_back(current);
            if (result[currentBonus].size() == static_cast<std::size_t>(config.limit))
                targets.erase(target);
        }
        return !targets.empty();
    }

    // 超过时间，退出
    if (dfsInfo.isTimeout()) 
        return false;

    // 加成超过目标，剪枝
    if (currentBonus > *targets.rbegin())
        return true;

    // 获取剩下的卡中能取的member-current.size()个最低和最高加成，用于剪枝
    int lowestBonus = 0, highestBonus = 0;
    for (
        int rest = config.member - depth, i = static_cast<int>(startIndex);
        rest > 0 && i < static_cast<int>(entries.size());
        ++i
    ) {
        const auto &entry = entries[i];
        if (charaMask & (std::uint64_t{1} << entry.chara))
            continue;
        lowestBonus += entry.bonus;
        --rest;
    }
    for (
        int rest = config.member - depth, i = static_cast<int>(entries.size()) - 1;
        rest > 0 && i >= static_cast<int>(startIndex);
        --i
    ) {
        const auto &entry = entries[i];
        if (charaMask & (std::uint64_t{1} << entry.chara))
            continue;
        highestBonus += entry.bonus;
        --rest;
    }
    if(currentBonus + lowestBonus > *targets.rbegin() || currentBonus + highestBonus < *targets.begin()) 
        return true;

    // 搜索剩下卡牌
    for (std::size_t i = startIndex; i < entries.size(); ++i) {
        const auto &entry = entries[i];
        const auto charaBit = std::uint64_t{1} << entry.chara;
        if (charaMask & charaBit)
            continue;

        current[depth] = entry.card;
        bool cont = dfsBonus(
            config, dfsInfo, targets, currentBonus + entry.bonus, depth + 1, i + 1,
            current, result, entries, charaMask | charaBit
        );
        if (!cont)
            return false;
    }   
    return true;
}


void BaseDeckRecommend::findTargetBonusCardsDFS(
    int liveType, 
    const DeckRecommendConfig &config, 
    const std::vector<CardDetail> &cardDetails, 
    const std::function<Score(const DeckDetail &)> &scoreFunc, 
    RecommendCalcInfo &dfsInfo, 
    int limit, 
    int member, 
    std::optional<int> eventType, 
    std::optional<int> eventId
)
{
    std::map<int, std::vector<SupportDeckCard>> emptySupportCards{};

    std::vector<int> bonusList = config.bonusList;
    for (auto& x : bonusList) x *= 2;
    std::sort(bonusList.begin(), bonusList.end());
    if (bonusList.empty()) 
        throw std::runtime_error("Bonus list is empty");

    if (eventType.value_or(0) == Enums::EventType::world_bloom) {
        // 该函数只用于非WL活动
        throw std::runtime_error("this func is not used for world bloom event");
    }

    // 按照加成*2和角色类型归类
    std::map<int, std::vector<const CardDetail *>> bonusCharaCards;
    for (const auto &card : cardDetails) {
        if (card.maxEventBonus.has_value() && card.maxEventBonus.value() > 0) {
            if (std::abs(std::round(card.maxEventBonus.value() * 2) - card.maxEventBonus.value() * 2) > 1e-6)
                continue;
            if (card.characterId < 0 || card.characterId >= 64) {
                throw std::runtime_error(
                    "Unsupported character ID in bonus search: "
                    + std::to_string(card.characterId)
                );
            }
            int bonus = std::round(card.maxEventBonus.value() * 2);
            int chara = card.characterId;
            int key = getCharaBonusKey(chara, bonus);
            bonusCharaCards[key].push_back(&card);
        }
    }
    for(auto& [key, cards] : bonusCharaCards) {
        std::sort(cards.begin(), cards.end(), [](const CardDetail *a, const CardDetail *b) {
            return std::tuple(a->power.max, a->cardId) < std::tuple(b->power.max, b->cardId);
        });
    }
    std::vector<BonusSearchEntry> bonusEntries;
    bonusEntries.reserve(bonusCharaCards.size());
    for (const auto &[key, cards] : bonusCharaCards) {
        auto [bonus, chara] = getBonusChara(key);
        bonusEntries.push_back({key, bonus, chara, cards.front()});
    }

    // 剩余的组卡目标
    bonusList.erase(std::unique(bonusList.begin(), bonusList.end()), bonusList.end());
    std::vector<int> targets = std::move(bonusList);

    // 按照不同层级过滤进行分层搜索
    for(auto& filter : bonusFilters) {
        auto filteredEntries = applyFilter(filter, bonusEntries);

        BonusDeck current{};
        std::map<int, std::vector<BonusDeck>> result;
        dfsBonus(config, dfsInfo, targets, 0, 0, 0, current, result, filteredEntries, 0);

        // 取卡
        for (auto& [bonus, bonusResult] : result) {
            for (auto &resultCards : bonusResult) {
                std::vector<const CardDetail *> deckCards(
                    resultCards.begin(), resultCards.begin() + config.member
                );
                // 计算卡组详情
                auto deckRes = getBestPermutation(
                    deckCalculator, deckCards, emptySupportCards, scoreFunc,
                    0, eventType, eventId, liveType, config
                ).bestDeck.value();
                // 需要验证加成正确
                if(std::abs(deckRes.eventBonus.value_or(0) * 2 - bonus) < 1e-6)
                    dfsInfo.update(deckRes, 1e9);
                else
                    std::cerr << "Warning: Event bonus mismatch, expected " 
                            << bonus / 2.0 << ", got " 
                            << deckRes.eventBonus.value_or(0) << std::endl;
            }
        }

        if (targets.empty()) {
            // 如果已经找到所有目标，退出
            break;
        }
    }
}
