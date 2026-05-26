#ifndef MUSIC_SCORE_H
#define MUSIC_SCORE_H

#include "common/collection-utils.h"

struct MusicNoteBase {
    double time = 0.0;
};

struct MusicNote : MusicNoteBase {
    int type = 0;
    int longId = 0;
};

struct MusicScore {
    std::vector<MusicNote> notes;
    std::vector<MusicNoteBase> skills;
    std::vector<MusicNoteBase> fevers;

    static inline MusicScore fromJson(const json_view& item) {
        MusicScore score;
        for (const auto& noteJson : item.value("notes", json_view::array())) {
            MusicNote note;
            note.time = noteJson.value("time", 0.0);
            note.type = noteJson.value("type", 0);
            note.longId = noteJson.value("longId", 0);
            score.notes.push_back(note);
        }
        for (const auto& skillJson : item.value("skills", json_view::array())) {
            MusicNoteBase skill;
            skill.time = skillJson.value("time", 0.0);
            score.skills.push_back(skill);
        }
        for (const auto& feverJson : item.value("fevers", json_view::array())) {
            MusicNoteBase fever;
            fever.time = feverJson.value("time", 0.0);
            score.fevers.push_back(fever);
        }
        return score;
    }

    static inline MusicScore fromJsonString(const std::string& s) {
        auto doc = json_doc::parse(s, "music score");
        return fromJson(doc.root());
    }
};

#endif // MUSIC_SCORE_H
