#include "deck-recommend/deck-result-update.h"

#include <algorithm>
#include <array>

bool RecommendDeck::operator>(const RecommendDeck &other) const
{
    // 先按目标值
    if (targetValue != other.targetValue) return targetValue > other.targetValue;
    // 目标值一样，按C位CardID
    return cards[0].cardId < other.cards[0].cardId;
}

uint64_t getRecommendDeckHash(const RecommendDeck &deck)
{
    // 结果去重必须按队长和完整卡组集合判断；只看分数/综合/队长会把实效不同的近似同分卡组吞掉。
    std::array<int, 5> cardIds{};
    int cardCount = std::min<int>(deck.cards.size(), cardIds.size());
    for (int i = 0; i < cardCount; ++i) {
        cardIds[i] = deck.cards[i].cardId;
    }
    std::sort(cardIds.begin(), cardIds.begin() + cardCount);

    uint64_t hash = 1469598103934665603ULL;
    constexpr uint64_t prime = 1099511628211ULL;
    hash ^= static_cast<uint64_t>(deck.cards.empty() ? 0 : deck.cards[0].cardId);
    hash *= prime;
    hash ^= static_cast<uint64_t>(cardCount);
    hash *= prime;
    for (int i = 0; i < cardCount; ++i) {
        hash ^= static_cast<uint64_t>(cardIds[i]);
        hash *= prime;
    }
    return hash;
}

void RecommendCalcInfo::update(const RecommendDeck &deck, int limit)
{
    // 如果已经足够，判断是否劣于当前最差的
    if (int(deckQueue.size()) >= limit && deckQueue.top() > deck)
        return;

    // 判断是否已经存在
    uint64_t hash = getRecommendDeckHash(deck);
    if (deckQueueHashSet.count(hash)) 
        return; 
    deckQueueHashSet.insert(hash);

    deckQueue.push(deck);
    while (int(deckQueue.size()) > limit) {
        deckQueue.pop();
    }
}

bool RecommendCalcInfo::isTimeout()
{
    if (++timeout_check_count % 256 != 0) 
        return is_timeout;
    if (std::chrono::high_resolution_clock::now().time_since_epoch().count() - start_ts > timeout) 
        is_timeout = true;
    return is_timeout;
}
