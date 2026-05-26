#ifndef INGAME_COMBO_H
#define INGAME_COMBO_H

#include "common/collection-utils.h"

struct IngameCombo {
    int id = 0;
    int fromCount = 0;
    int toCount = 0;
    double scoreCoefficient = 0.0;

    static inline std::vector<IngameCombo> fromJsonList(const json_view& jsonData) {
        std::vector<IngameCombo> combos;
        for (const auto& item : jsonData) {
            IngameCombo combo;
            combo.id = item.value("id", 0);
            combo.fromCount = item.value("fromCount", 0);
            combo.toCount = item.value("toCount", 0);
            combo.scoreCoefficient = item.value("scoreCoefficient", 0.0);
            combos.push_back(combo);
        }
        return combos;
    }
};

#endif // INGAME_COMBO_H
