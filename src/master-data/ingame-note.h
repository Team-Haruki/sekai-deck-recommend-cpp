#ifndef INGAME_NOTE_H
#define INGAME_NOTE_H

#include "common/collection-utils.h"

struct IngameNote {
    int id = 0;
    std::string ingameNoteType;
    double scoreCoefficient = 0.0;
    int damageBad = 0;
    int damageMiss = 0;

    static inline std::vector<IngameNote> fromJsonList(const json_view& jsonData) {
        std::vector<IngameNote> notes;
        for (const auto& item : jsonData) {
            IngameNote note;
            note.id = item.value("id", 0);
            note.ingameNoteType = item.value("ingameNoteType", "");
            note.scoreCoefficient = item.value("scoreCoefficient", 0.0);
            note.damageBad = item.value("damageBad", 0);
            note.damageMiss = item.value("damageMiss", 0);
            notes.push_back(note);
        }
        return notes;
    }
};

#endif // INGAME_NOTE_H
