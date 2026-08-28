#include "deck-recommend/base-deck-recommend.h"

#include <array>
#include <cmath>
#include <cstdint>

static int getCharaAttrBonusKey(int chara, int attr, int bonus) {
    return bonus * 1000 + chara * 10 + attr;
}
static int getBonus(int key) {
    return key / 1000;
}
static int getChara(int key) {
    return (key / 10) % 100;
}
static int getAttr(int key) {
    return key % 10;
}
static std::tuple<int, int, int> getCharaAttrBonus(int key) {
    return { getChara(key), getAttr(key), getBonus(key) };
}

struct WorldBloomBonusSearchEntry {
    int key;
    int bonus;
    int chara;
    int attr;
    const CardDetail *card;
};

using WorldBloomBonusDeck = std::array<const CardDetail *, 5>;

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
static std::vector<WorldBloomBonusSearchEntry> applyFilter(
    const BonusFilter &filter,
    const std::vector<WorldBloomBonusSearchEntry> &entries
) {
    std::vector<WorldBloomBonusSearchEntry> ret;
    ret.reserve(entries.size());
    for (const auto &entry : entries) {
        if (filter(entry.key))
            ret.push_back(entry);
    }
    return ret;
}


static bool dfsWorldBloomBonus(
    const DeckRecommendConfig &config, 
    RecommendCalcInfo &dfsInfo, 
    std::vector<int> &targets,
    int currentBonus,
    int depth,
    std::size_t startIndex,
    WorldBloomBonusDeck &current,
    std::map<int, std::vector<WorldBloomBonusDeck>> &result,
    const std::vector<WorldBloomBonusSearchEntry> &entries,
    std::uint64_t charaMask,
    std::uint16_t attrMask,
    int diffAttrCount,
    const std::array<int, 6> &diffAttrBonus,
    const int maxAttrBonus
)
{
    int currentDiffAttrBonus = diffAttrBonus[diffAttrCount];

    if (depth == config.member) {
        int realCurrentBonus = currentBonus + currentDiffAttrBonus;
        auto target = std::lower_bound(targets.begin(), targets.end(), realCurrentBonus);
        if (target != targets.end() && *target == realCurrentBonus) {
            result[realCurrentBonus].push_back(current);
            if (result[realCurrentBonus].size() == static_cast<std::size_t>(config.limit))
                targets.erase(target);
        }
        return !targets.empty();
    }

    // 超过时间，退出
    if (dfsInfo.isTimeout()) 
        return false;

    // 加成超过目标，剪枝
    if (currentBonus + currentDiffAttrBonus > *targets.rbegin())
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
    // 最低加成假设为当前异色数（因为加入新卡异色数只会变多），最高加成假设为全异色
    if(currentBonus + currentDiffAttrBonus + lowestBonus  > *targets.rbegin() 
    || currentBonus + maxAttrBonus         + highestBonus < *targets.begin()) 
        return true;

    // 搜索剩下卡牌
    for (std::size_t i = startIndex; i < entries.size(); ++i) {
        const auto &entry = entries[i];
        const auto charaBit = std::uint64_t{1} << entry.chara;
        if (charaMask & charaBit)
            continue;

        const auto attrBit = std::uint16_t{1} << entry.attr;
        current[depth] = entry.card;
        bool cont = dfsWorldBloomBonus(
            config, dfsInfo, targets, currentBonus + entry.bonus, depth + 1, i + 1,
            current, result, entries, charaMask | charaBit, attrMask | attrBit,
            diffAttrCount + ((attrMask & attrBit) == 0), diffAttrBonus, maxAttrBonus
        );
        if (!cont)
            return false;
    }   
    return true;
}


void BaseDeckRecommend::findWorldBloomTargetBonusCardsDFS(
    int liveType, 
    const DeckRecommendConfig &config, 
    const std::vector<CardDetail> &cardDetails, 
    const std::function<Score(const DeckDetail &)> &scoreFunc, 
    RecommendCalcInfo &dfsInfo, 
    int,
    int,
    std::optional<int> eventType, 
    std::optional<int> eventId
)
{
    if (eventId.has_value() && this->dataProvider.masterData->isWorldBloomFinale(eventId.value()))
        throw std::invalid_argument("world bloom finale event is not supported for bonus target");

    std::map<int, std::vector<SupportDeckCard>> emptySupportCards{};

    std::vector<int> bonusList = config.bonusList;
    for (auto& x : bonusList) x *= 2;
    std::sort(bonusList.begin(), bonusList.end());
    if (bonusList.empty()) 
        throw std::runtime_error("Bonus list is empty");

    if (eventType.value_or(0) != Enums::EventType::world_bloom) {
        // 该函数只用于WL活动
        throw std::runtime_error("this func is only used for world bloom event");
    }

    // 按照加成*2和角色类型和卡牌颜色归类
    std::map<int, std::vector<const CardDetail *>> bonusCharaCards;
    for (const auto &card : cardDetails) {
        if (card.maxEventBonus.has_value() && card.maxEventBonus.value() > 0) {
            if (std::abs(std::round(card.maxEventBonus.value() * 2) - card.maxEventBonus.value() * 2) > 1e-6)
                continue;
            if (card.characterId < 0 || card.characterId >= 64) {
                throw std::runtime_error(
                    "Unsupported character ID in world bloom bonus search: "
                    + std::to_string(card.characterId)
                );
            }
            int bonus = std::round(card.maxEventBonus.value() * 2);
            int chara = card.characterId;
            int attr = card.attr;
            int key = getCharaAttrBonusKey(chara, attr, bonus);
            bonusCharaCards[key].push_back(&card);
        }
    }
    for(auto& [key, cards] : bonusCharaCards) {
        std::sort(cards.begin(), cards.end(), [](const CardDetail *a, const CardDetail *b) {
            return std::tuple(a->power.max, a->cardId) < std::tuple(b->power.max, b->cardId);
        });
    }
    std::vector<WorldBloomBonusSearchEntry> bonusEntries;
    bonusEntries.reserve(bonusCharaCards.size());
    for (const auto &[key, cards] : bonusCharaCards) {
        auto [chara, attr, bonus] = getCharaAttrBonus(key);
        bonusEntries.push_back({key, bonus, chara, attr, cards.front()});
    }

    // wl异色加成
    std::array<int, 6> diffAttrBonus{};
    int maxAttrBonus = 0;
    auto& worldBloomDifferentAttributeBonuses = this->dataProvider.masterData->worldBloomDifferentAttributeBonuses;
    for (const auto &bonus : worldBloomDifferentAttributeBonuses) {
        const int doubledBonus = std::round(bonus.bonusRate * 2);
        if (bonus.attributeCount >= 0 &&
            bonus.attributeCount < static_cast<int>(diffAttrBonus.size())) {
            diffAttrBonus[bonus.attributeCount] = doubledBonus;
        }
        maxAttrBonus = std::max(maxAttrBonus, doubledBonus);
    }

    // 剩余的组卡目标
    bonusList.erase(std::unique(bonusList.begin(), bonusList.end()), bonusList.end());
    std::vector<int> targets = std::move(bonusList);

    // 按照不同层级过滤进行分层搜索
    for(auto& filter : bonusFilters) {
        auto filteredEntries = applyFilter(filter, bonusEntries);

        WorldBloomBonusDeck current{};
        std::map<int, std::vector<WorldBloomBonusDeck>> result;
        dfsWorldBloomBonus(
            config, dfsInfo, targets, 0, 0, 0, current, result, filteredEntries,
            0, 0, 0, diffAttrBonus, maxAttrBonus
        );

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
                    std::cerr << "Warning: World Bloom bonus mismatch, expected " 
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
