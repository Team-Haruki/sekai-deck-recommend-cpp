#include "deck-recommend/base-deck-recommend.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <set>


void BaseDeckRecommend::findBestCardsSA(
    int liveType,
    const DeckRecommendConfig& cfg,
    Rng& rng,
    const std::vector<CardDetail> &cardDetails,     // 所有参与组队的卡牌
    std::map<int, std::vector<SupportDeckCard>>& supportCards,        // 全部卡牌（用于计算支援卡组加成）
    const std::function<Score(const DeckDetail &)> &scoreFunc,    
    RecommendCalcInfo& saInfo,
    int limit, 
    bool isChallengeLive, 
    int member, 
    int honorBonus, 
    std::optional<int> eventType, 
    std::optional<int> eventId,
    const std::vector<CardDetail>& fixedCards
)
{
    if (isChallengeLive) {
        member = std::min(member, int(cardDetails.size()));
    }

    member -= int(fixedCards.size());
    auto remainingFixedCharacters = resolveRemainingFixedCharacters(cfg, fixedCards, eventId);
    std::set<int> remainingFixedCharacterSet(
        remainingFixedCharacters.begin(),
        remainingFixedCharacters.end()
    );
    if (member < int(remainingFixedCharacters.size())) {
        return;
    }

    constexpr int MAX_CID = 27;
    std::vector<CardDetail> charaCardDetails[MAX_CID] = {};
    for (const auto& card : cardDetails) {
        charaCardDetails[card.characterId].push_back(card);
    }

    auto evaluateDeck = [&](const std::vector<const CardDetail*>& deck) {
        auto hash = calcDeckHash(deck);
        auto it = saInfo.deckTargetValueMap.find(hash);
        if (it != saInfo.deckTargetValueMap.end()) {
            return it->second;
        }

        auto ret = getBestPermutation(
            this->deckCalculator, deck, supportCards, scoreFunc,
            honorBonus, eventType, eventId, liveType, cfg
        );
        double targetValue = -1e18;
        if (ret.bestDeck.has_value()) {
            targetValue = ret.bestDeck.value().targetValue;
            saInfo.update(ret.bestDeck.value(), limit);
        } else {
            targetValue = -1e9 + ret.maxMultiLiveScoreUp;
        }
        saInfo.deckTargetValueMap[hash] = targetValue;
        return targetValue;
    };

    double temperature = cfg.saStartTemperature;
    auto start_time = std::chrono::high_resolution_clock::now();
    int iter_num = 0;
    int no_improve_iter_num = 0;
    double current_score = -1e18;
    double last_score = -1e18;
    std::vector<int> replacableCardIndices{};
    std::set<int> deckCharacters{};
    std::set<int> deckCardIds{};

    std::vector<const CardDetail*> deck{};
    if (member > 0) {
        if (!isChallengeLive) {
            for (const auto& characterId : remainingFixedCharacters) {
                auto& cards = charaCardDetails[characterId];
                if (cards.empty()) {
                    return;
                }
                auto& max_card = *std::max_element(cards.begin(), cards.end(), [](const CardDetail& a, const CardDetail& b) {
                    return a.power.min != b.power.min ? a.power.min < b.power.min : a.cardId > b.cardId;
                });
                deck.push_back(&max_card);
            }
            for (int i = 0; i < MAX_CID; ++i) {
                auto& cards = charaCardDetails[i];
                if (cards.empty()) {
                    continue;
                }
                if (std::find_if(fixedCards.begin(), fixedCards.end(), [&](const CardDetail& card) {
                        return card.characterId == i;
                    }) != fixedCards.end()) {
                    continue;
                }
                if (remainingFixedCharacterSet.count(i)) {
                    continue;
                }
                auto& max_card = *std::max_element(cards.begin(), cards.end(), [](const CardDetail& a, const CardDetail& b) {
                    return a.power.min != b.power.min ? a.power.min < b.power.min : a.cardId > b.cardId;
                });
                deck.push_back(&max_card);
            }
        } else {
            for (const auto& card : cardDetails) {
                if (std::find_if(fixedCards.begin(), fixedCards.end(), [&](const CardDetail& fixedCard) {
                        return card.cardId == fixedCard.cardId;
                    }) != fixedCards.end()) {
                    continue;
                }
                deck.push_back(&card);
            }
        }

        auto strongerCard = [](const CardDetail* a, const CardDetail* b) {
            return a->power.min != b->power.min ? a->power.min > b->power.min : a->cardId < b->cardId;
        };
        if (!isChallengeLive && !remainingFixedCharacterSet.empty()) {
            std::vector<const CardDetail*> requiredDeck{};
            std::vector<const CardDetail*> flexibleDeck{};
            for (const auto* card : deck) {
                if (remainingFixedCharacterSet.count(card->characterId)) {
                    requiredDeck.push_back(card);
                } else {
                    flexibleDeck.push_back(card);
                }
            }
            std::sort(flexibleDeck.begin(), flexibleDeck.end(), strongerCard);
            deck = std::move(requiredDeck);
            for (const auto* card : flexibleDeck) {
                if (int(deck.size()) >= member) {
                    break;
                }
                deck.push_back(card);
            }
        } else {
            std::sort(deck.begin(), deck.end(), strongerCard);
            if (int(deck.size()) > member) {
                deck.resize(member);
            }
        }
    }

    for (const auto& card : fixedCards) {
        deck.push_back(&card);
    }
    for (const auto* card : deck) {
        deckCharacters.insert(card->characterId);
        deckCardIds.insert(card->cardId);
    }

    if (!deck.empty()) {
        current_score = evaluateDeck(deck);
        last_score = current_score;
    }

    if (member <= 0) {
        return;
    }

    while (true) {
        if (saInfo.isTimeout()) {
            break;
        }
        if (int(deck.size()) <= int(fixedCards.size())) {
            break;
        }

        int pos = std::uniform_int_distribution<int>(0, int(deck.size()) - int(fixedCards.size()) - 1)(rng);

        replacableCardIndices.clear();
        for (int i = 0; i < MAX_CID; ++i) {
            if (!isChallengeLive && remainingFixedCharacterSet.count(deck[pos]->characterId) && i != deck[pos]->characterId) {
                continue;
            }
            if (!isChallengeLive && i != deck[pos]->characterId && deckCharacters.count(i)) {
                continue;
            }
            for (int j = 0; j < int(charaCardDetails[i].size()); ++j) {
                if (isChallengeLive
                    && charaCardDetails[i][j].cardId != deck[pos]->cardId
                    && deckCardIds.count(charaCardDetails[i][j].cardId)) {
                    continue;
                }
                replacableCardIndices.push_back(i * 10000 + j);
            }
        }
        if (replacableCardIndices.empty()) {
            break;
        }

        int index = std::uniform_int_distribution<int>(0, int(replacableCardIndices.size()) - 1)(rng);
        int chara_index = replacableCardIndices[index] / 10000;
        int card_index = replacableCardIndices[index] % 10000;
        auto* old_card = deck[pos];
        auto* new_card = &charaCardDetails[chara_index][card_index];

        deck[pos] = new_card;
        double new_score = evaluateDeck(deck);
        double delta = new_score - current_score;
        double accept_prob = delta > 0 ? 1.0 : std::exp(delta / std::max(temperature, 1e-12));

        if (std::uniform_real_distribution<double>(0.0, 1.0)(rng) < accept_prob) {
            deckCharacters.erase(old_card->characterId);
            deckCardIds.erase(old_card->cardId);
            deckCharacters.insert(new_card->characterId);
            deckCardIds.insert(new_card->cardId);
            last_score = current_score;
            current_score = new_score;
        } else {
            deck[pos] = old_card;
            last_score = current_score;
        }

        if (cfg.saDebug) {
            std::cerr << "sa iter: " << iter_num
                      << ", score: " << new_score
                      << ", last_score: " << last_score
                      << ", temp: " << temperature
                      << ", prob: " << accept_prob
                      << " no_impro_iter: " << no_improve_iter_num
                      << std::endl;
        }

        if (++iter_num >= cfg.saMaxIter) {
            break;
        }
        if (current_score <= last_score) {
            if (++no_improve_iter_num >= cfg.saMaxIterNoImprove) {
                break;
            }
        } else {
            no_improve_iter_num = 0;
        }

        auto current_time = std::chrono::high_resolution_clock::now();
        auto elapsed_time = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time).count();
        if (elapsed_time > cfg.saMaxTimeMs) {
            break;
        }
        temperature *= cfg.saCoolingRate;
    }
}
