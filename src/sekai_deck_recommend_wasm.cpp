// Embind binding for the deck recommend engine (WebAssembly target).
//
// Mirrors the public surface of the pybind11 binding in sekai_deck_recommend.cpp
// but exposes a JSON-in / JSON-out interface so the JS side can call directly
// without per-option Embind value_object glue. Option/result schemas are kept
// identical to the Python binding so existing client code (e.g. deck-service)
// can be reused with minimal changes.
//
// NOTE: Parts of the option-validation logic intentionally duplicate
// sekai_deck_recommend.cpp's construct_options_from_py. This is the
// lower-risk path until we extract a shared core; keep the two in sync when
// adding fields. See AGENTS.md / CLAUDE.md.

#ifdef __EMSCRIPTEN__

#include "deck-recommend/event-deck-recommend.h"
#include "deck-recommend/challenge-live-deck-recommend.h"
#include "deck-recommend/mysekai-deck-recommend.h"
#include "area-item-recommend/area-item-recommend.h"
#include "data-provider/static-data.h"
#include "live-score/live-exact-calculator.h"
#include "music-recommend/music-recommend.h"
#include "common/collection-utils.h"
#include "common/common-enums.h"
#include "common/enum-maps.h"

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace {

const std::map<std::string, Region> REGION_ENUM_MAP = {
    {"jp", Region::JP},
    {"tw", Region::TW},
    {"en", Region::EN},
    {"kr", Region::KR},
    {"cn", Region::CN},
};

const std::string DEFAULT_TARGET = "score";
const std::set<std::string> VALID_TARGETS = {
    "score", "skill", "power", "bonus",
};

const std::string DEFAULT_ALGORITHM = "ga";
const std::set<std::string> VALID_ALGORITHMS = {
    "sa", "dfs", "ga", "dfs_ga", "rl",
};

const std::set<std::string> VALID_MUSIC_DIFFS = {
    "easy", "normal", "hard", "expert", "master", "append",
};

const std::set<std::string> VALID_LIVE_TYPES = {
    "multi", "solo", "challenge", "cheerful", "auto", "mysekai", "challenge_auto",
};

const std::set<std::string> VALID_UNIT_TYPES = {
    "light_sound", "idol", "street", "theme_park", "school_refusal", "piapro",
};

const std::set<std::string> VALID_EVENT_ATTRS = {
    "mysterious", "cool", "pure", "cute", "happy",
};

const std::set<std::string> VALID_EVENT_TYPES = {
    "marathon", "cheerful_carnival", "world_bloom",
};

const std::string DEFAULT_SKILL_REFERENCE_CHOOSE_STRATEGY = "average";
const std::set<std::string> VALID_SKILL_REFERENCE_CHOOSE_STRATEGIES = {
    "average", "max", "min",
};

const std::string DEFAULT_SKILL_ORDER_CHOOSE_STRATEGY = "average";
const std::set<std::string> VALID_SKILL_ORDER_CHOOSE_STRATEGIES = {
    "average", "max", "min", "specific",
};

// yyjson helpers -----------------------------------------------------------

std::string jsonWriteErrorMessage(const yyjson_write_err& err) {
    return err.msg ? std::string(err.msg) : std::string("unknown error");
}

std::string dumpJson(const json_view& v) {
    size_t len = 0;
    yyjson_write_err err{};
    char* out = yyjson_val_write_opts(v.raw(), YYJSON_WRITE_NOFLAG, nullptr, &len, &err);
    if (!out) {
        throw std::runtime_error("Failed to serialize JSON value: " + jsonWriteErrorMessage(err));
    }
    std::string result(out, len);
    std::free(out);
    return result;
}

template <typename T>
std::optional<T> jsonOpt(const json_view& j, const char* key) {
    if (!j.contains(key) || j[key].is_null()) return std::nullopt;
    return j[key].get<T>();
}

std::string jsonTypeName(const json_view& v) {
    const char* type = yyjson_get_type_desc(const_cast<yyjson_val*>(v.raw()));
    return type ? std::string(type) : std::string("null");
}

// `user_data_str` may arrive as a JSON string OR already-parsed JSON object.
// Normalize to a serialized string for UserData::loadFromString.
std::string extractUserDataStr(const json_view& v) {
    if (v.is_string()) return v.get<std::string>();
    return dumpJson(v);
}

class MutableJsonDoc {
public:
    MutableJsonDoc() : doc(yyjson_mut_doc_new(nullptr)) {
        if (!doc) throw std::runtime_error("Failed to allocate mutable JSON document.");
    }
    MutableJsonDoc(const MutableJsonDoc&) = delete;
    MutableJsonDoc& operator=(const MutableJsonDoc&) = delete;
    ~MutableJsonDoc() { yyjson_mut_doc_free(doc); }

    yyjson_mut_doc* get() const { return doc; }

private:
    yyjson_mut_doc* doc = nullptr;
};

template <typename T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
void jsonAdd(yyjson_mut_doc* doc, yyjson_mut_val* obj, const char* key, T value) {
    if (!yyjson_mut_obj_add_int(doc, obj, key, value))
        throw std::runtime_error(std::string("Failed to add JSON integer field: ") + key);
}

void jsonAdd(yyjson_mut_doc* doc, yyjson_mut_val* obj, const char* key, double value) {
    if (!yyjson_mut_obj_add_real(doc, obj, key, value))
        throw std::runtime_error(std::string("Failed to add JSON real field: ") + key);
}

void jsonAdd(yyjson_mut_doc* doc, yyjson_mut_val* obj, const char* key, bool value) {
    if (!yyjson_mut_obj_add_bool(doc, obj, key, value))
        throw std::runtime_error(std::string("Failed to add JSON bool field: ") + key);
}

void jsonAdd(yyjson_mut_doc* doc, yyjson_mut_val* obj, const char* key, const std::string& value) {
    if (!yyjson_mut_obj_add_strncpy(doc, obj, key, value.data(), value.size()))
        throw std::runtime_error(std::string("Failed to add JSON string field: ") + key);
}

void jsonAddValue(yyjson_mut_doc* doc, yyjson_mut_val* obj, const char* key, yyjson_mut_val* value) {
    if (!yyjson_mut_obj_add_val(doc, obj, key, value))
        throw std::runtime_error(std::string("Failed to add JSON value field: ") + key);
}

void jsonArrayAppend(yyjson_mut_val* arr, yyjson_mut_val* value) {
    if (!yyjson_mut_arr_append(arr, value))
        throw std::runtime_error("Failed to append JSON array value.");
}

std::string dumpMutableJson(yyjson_mut_val* value) {
    size_t len = 0;
    yyjson_write_err err{};
    char* out = yyjson_mut_val_write_opts(value, YYJSON_WRITE_NOFLAG, nullptr, &len, &err);
    if (!out) {
        throw std::runtime_error("Failed to serialize JSON output: " + jsonWriteErrorMessage(err));
    }
    std::string result(out, len);
    std::free(out);
    return result;
}

template <typename Vec, typename Pred>
auto findOrThrowVec(const Vec& vec, Pred pred, const std::string& msg) {
    auto it = std::find_if(vec.begin(), vec.end(), pred);
    if (it == vec.end()) throw std::invalid_argument(msg);
    return it;
}

// JSON option → DeckRecommendConfig builder -------------------------------
// Mirrors sekai_deck_recommend.cpp::construct_options_from_py.

struct PreparedOptions {
    int liveType = 0;
    int eventId = 0;
    int worldBloomCharacterId = 0;
    int challengeLiveCharacterId = 0;
    DeckRecommendConfig config = {};
    DataProvider dataProvider = {};
};

PreparedOptions buildOptions(
    const json_view& opts,
    std::map<Region, std::shared_ptr<MasterData>>& region_masterdata,
    std::map<Region, std::shared_ptr<MusicMetas>>& region_musicmetas
) {
    PreparedOptions out;

    // region
    auto regionStr = jsonOpt<std::string>(opts, "region");
    if (!regionStr) throw std::invalid_argument("region is required.");
    auto regionIt = REGION_ENUM_MAP.find(*regionStr);
    if (regionIt == REGION_ENUM_MAP.end())
        throw std::invalid_argument("Invalid region: " + *regionStr);
    Region region = regionIt->second;

    // user data
    auto userdata = std::make_shared<UserData>();
    if (opts.contains("user_data_file_path") && !opts["user_data_file_path"].is_null()) {
        userdata->loadFromFile(opts["user_data_file_path"].get<std::string>());
    } else if (opts.contains("user_data_str") && !opts["user_data_str"].is_null()) {
        userdata->loadFromString(extractUserDataStr(opts["user_data_str"]));
    } else if (opts.contains("user_data") && !opts["user_data"].is_null()) {
        userdata->loadFromString(extractUserDataStr(opts["user_data"]));
    } else {
        throw std::invalid_argument("Either user_data / user_data_file_path / user_data_str is required.");
    }

    // master/music data
    if (!region_masterdata.count(region))
        throw std::invalid_argument("Master data not found for region: " + *regionStr);
    auto masterdata = region_masterdata[region];

    if (!region_musicmetas.count(region))
        throw std::invalid_argument("Music metas not found for region: " + *regionStr);
    auto musicmetas = region_musicmetas[region];

    out.dataProvider = DataProvider{region, masterdata, userdata, musicmetas};

    // liveType
    bool is_mysekai = false;
    auto liveTypeStr = jsonOpt<std::string>(opts, "live_type");
    if (!liveTypeStr) throw std::invalid_argument("live_type is required.");
    if (!VALID_LIVE_TYPES.count(*liveTypeStr))
        throw std::invalid_argument("Invalid live type: " + *liveTypeStr);
    if (*liveTypeStr == "mysekai") {
        is_mysekai = true;
        out.liveType = mapEnum(EnumMap::liveType, "multi");
    } else {
        out.liveType = mapEnum(EnumMap::liveType, *liveTypeStr);
    }
    bool is_challenge_live = Enums::LiveType::isChallenge(out.liveType);

    // eventId
    auto eventIdOpt = jsonOpt<int>(opts, "event_id");
    auto worldBloomTurnOpt = jsonOpt<int>(opts, "world_bloom_event_turn");
    auto worldBloomCharOpt = jsonOpt<int>(opts, "world_bloom_character_id");
    auto eventAttrOpt = jsonOpt<std::string>(opts, "event_attr");
    auto eventUnitOpt = jsonOpt<std::string>(opts, "event_unit");
    auto eventTypeOpt = jsonOpt<std::string>(opts, "event_type");

    if (eventIdOpt) {
        if (is_challenge_live)
            throw std::invalid_argument("event_id is not valid for challenge live.");
        out.eventId = *eventIdOpt;
        findOrThrowVec(out.dataProvider.masterData->events, [&](const Event& it) {
            return it.id == out.eventId;
        }, "Event not found for eventId: " + std::to_string(out.eventId));
    } else if (!is_challenge_live) {
        std::string event_type = eventTypeOpt.value_or("marathon");
        if (!VALID_EVENT_TYPES.count(event_type))
            throw std::invalid_argument("Invalid event type: " + event_type);
        int event_type_enum = mapEnum(EnumMap::eventType, event_type);

        if (worldBloomTurnOpt) {
            int turn = *worldBloomTurnOpt;
            if (turn < 1 || turn > 3)
                throw std::invalid_argument("Invalid world bloom event turn: " + std::to_string(turn));
            if (turn == 3) {
                if (!worldBloomCharOpt)
                    throw std::invalid_argument("world_bloom_character_id is required for world bloom 3 fake event.");
                int part = out.dataProvider.masterData->getWorldBloom3PartByCharacterId(*worldBloomCharOpt);
                out.eventId = out.dataProvider.masterData->getWorldBloomFakeEventId(turn, part);
            } else {
                if (!eventUnitOpt)
                    throw std::invalid_argument("event_unit is required for world bloom fake event.");
                if (!VALID_UNIT_TYPES.count(*eventUnitOpt))
                    throw std::invalid_argument("Invalid event unit: " + *eventUnitOpt);
                out.eventId = out.dataProvider.masterData->getWorldBloomFakeEventId(
                    turn, mapEnum(EnumMap::unit, *eventUnitOpt));
            }
        } else if (eventAttrOpt || eventUnitOpt) {
            if (!eventAttrOpt || !eventUnitOpt)
                throw std::invalid_argument("event_attr and event_unit must be specified together.");
            if (!VALID_EVENT_ATTRS.count(*eventAttrOpt))
                throw std::invalid_argument("Invalid event attr: " + *eventAttrOpt);
            if (!VALID_UNIT_TYPES.count(*eventUnitOpt))
                throw std::invalid_argument("Invalid event unit: " + *eventUnitOpt);
            int unit = mapEnum(EnumMap::unit, *eventUnitOpt);
            int attr = mapEnum(EnumMap::attr, *eventAttrOpt);
            out.eventId = out.dataProvider.masterData->getUnitAttrFakeEventId(event_type_enum, unit, attr);
        } else {
            out.eventId = out.dataProvider.masterData->getNoEventFakeEventId(event_type_enum);
        }
    } else {
        out.eventId = 0;
    }

    // challengeLiveCharacterId
    if (auto v = jsonOpt<int>(opts, "challenge_live_character_id")) {
        out.challengeLiveCharacterId = *v;
        if (out.challengeLiveCharacterId < 1 || out.challengeLiveCharacterId > 26)
            throw std::invalid_argument("Invalid challenge character ID: " + std::to_string(out.challengeLiveCharacterId));
    } else if (is_challenge_live) {
        throw std::invalid_argument("challenge_live_character_id is required for challenge live.");
    }

    // worldBloomCharacterId
    if (worldBloomCharOpt) {
        out.worldBloomCharacterId = *worldBloomCharOpt;
        if (out.worldBloomCharacterId < 1 || out.worldBloomCharacterId > 26)
            throw std::invalid_argument("Invalid world bloom character ID: " + std::to_string(out.worldBloomCharacterId));
        findOrThrowVec(out.dataProvider.masterData->worldBlooms, [&](const WorldBloom& it) {
            return it.eventId == out.eventId && it.gameCharacterId == out.worldBloomCharacterId;
        }, "World bloom chapter not found for eventId: " + std::to_string(out.eventId) +
           ", characterId: " + std::to_string(out.worldBloomCharacterId));
    }

    // build DeckRecommendConfig
    auto& config = out.config;

    // target
    if (is_mysekai) {
        config.target = RecommendTarget::Mysekai;
    } else {
        std::string target = jsonOpt<std::string>(opts, "target").value_or(DEFAULT_TARGET);
        if (!VALID_TARGETS.count(target))
            throw std::invalid_argument("Invalid target: " + target);
        if (target == "score") config.target = RecommendTarget::Score;
        else if (target == "skill") config.target = RecommendTarget::Skill;
        else if (target == "power") config.target = RecommendTarget::Power;
        else if (target == "bonus") config.target = RecommendTarget::Bonus;
    }

    // bonus list
    auto targetBonusList = jsonOpt<std::vector<int>>(opts, "target_bonus_list").value_or(std::vector<int>{});
    if (!targetBonusList.empty()) {
        if (config.target != RecommendTarget::Bonus)
            throw std::invalid_argument("target_bonus_list is only valid for bonus target.");
        config.bonusList = targetBonusList;
    }

    // custom mixed bonus
    if (auto v = jsonOpt<std::string>(opts, "custom_bonus_attr")) {
        if (!VALID_EVENT_ATTRS.count(*v))
            throw std::invalid_argument("Invalid custom bonus attr: " + *v);
        config.customBonusAttr = mapEnum(EnumMap::attr, *v);
    }
    if (auto v = jsonOpt<std::vector<int>>(opts, "custom_bonus_character_ids")) {
        std::set<int> uniqueCids;
        std::vector<int> cids;
        for (int cid : *v) {
            if (cid < 1 || cid > 26)
                throw std::invalid_argument("Invalid custom bonus character ID: " + std::to_string(cid));
            if (uniqueCids.count(cid))
                throw std::invalid_argument("Duplicate custom bonus character ID: " + std::to_string(cid));
            uniqueCids.insert(cid);
            cids.push_back(cid);
        }
        config.customBonusCharacterIds = cids;
    }
    if (opts.contains("custom_bonus_character_support_units") &&
        !opts["custom_bonus_character_support_units"].is_null()) {
        std::unordered_map<int, int> supportUnits;
        json_view supportUnitsJson = opts["custom_bonus_character_support_units"];
        yyjson_obj_iter iter = yyjson_obj_iter_with(supportUnitsJson.raw());
        yyjson_val* key = nullptr;
        while ((key = yyjson_obj_iter_next(&iter))) {
            const char* rawCidKey = yyjson_get_str(key);
            const std::string cidKey = rawCidKey ? std::string(rawCidKey) : std::string();
            json_view value(yyjson_obj_iter_get_val(key));
            int cid = 0;
            try {
                size_t parsed = 0;
                cid = std::stoi(cidKey, &parsed);
                if (parsed != cidKey.size()) {
                    throw std::invalid_argument("invalid key");
                }
            } catch (const std::exception &) {
                throw std::invalid_argument(
                    "Invalid custom bonus support unit character ID key: " + cidKey +
                    " (value type: " + jsonTypeName(value) + ")");
            }
            std::string unitName = value.get<std::string>();
            if (cid < 1 || cid > 26)
                throw std::invalid_argument("Invalid custom bonus support unit character ID: " + std::to_string(cid));
            if (cid < 21 || cid > 26)
                throw std::invalid_argument("custom bonus support unit is only valid for virtual singer characters.");
            if (!VALID_UNIT_TYPES.count(unitName) || unitName == "piapro")
                throw std::invalid_argument("Invalid custom bonus support unit: " + unitName);
            if (!config.customBonusCharacterIds.has_value() ||
                std::find(config.customBonusCharacterIds->begin(),
                          config.customBonusCharacterIds->end(), cid) ==
                    config.customBonusCharacterIds->end()) {
                throw std::invalid_argument(
                    "custom bonus support unit character ID must be included in custom_bonus_character_ids: " +
                    std::to_string(cid));
            }
            supportUnits[cid] = mapEnum(EnumMap::unit, unitName);
        }
        config.customBonusSupportUnits = supportUnits;
    }

    // algorithm
    std::string algorithm = jsonOpt<std::string>(opts, "algorithm").value_or(DEFAULT_ALGORITHM);
    if (!VALID_ALGORITHMS.count(algorithm))
        throw std::invalid_argument("Invalid algorithm: " + algorithm);
    if (algorithm == "sa") config.algorithm = RecommendAlgorithm::SA;
    else if (algorithm == "dfs") config.algorithm = RecommendAlgorithm::DFS;
    else if (algorithm == "ga") config.algorithm = RecommendAlgorithm::GA;
    else if (algorithm == "dfs_ga") config.algorithm = RecommendAlgorithm::DFS_GA;
    else if (algorithm == "rl") config.algorithm = RecommendAlgorithm::RL;

    config.filterOtherUnit = jsonOpt<bool>(opts, "filter_other_unit").value_or(false);

    // music
    auto musicId = jsonOpt<int>(opts, "music_id");
    auto musicDiff = jsonOpt<std::string>(opts, "music_diff");
    if (!musicId) throw std::invalid_argument("music_id is required.");
    if (!musicDiff) throw std::invalid_argument("music_diff is required.");
    config.musicId = *musicId;
    if (!VALID_MUSIC_DIFFS.count(*musicDiff))
        throw std::invalid_argument("Invalid music difficulty: " + *musicDiff);
    config.musicDiff = mapEnum(EnumMap::musicDifficulty, *musicDiff);
    findOrThrowVec(out.dataProvider.musicMetas->metas, [&](const MusicMeta& it) {
        return it.music_id == config.musicId && it.difficulty == config.musicDiff;
    }, "Music meta not found for musicId: " + std::to_string(config.musicId) +
       ", difficulty: " + *musicDiff);

    // limit / member
    config.limit = jsonOpt<int>(opts, "limit").value_or(10);
    if (config.limit < 1)
        throw std::invalid_argument("Invalid limit: " + std::to_string(config.limit));

    config.member = jsonOpt<int>(opts, "member").value_or(5);
    if (config.member < 2 || config.member > 5)
        throw std::invalid_argument("Invalid member count: " + std::to_string(config.member));

    // fixed cards
    if (auto v = jsonOpt<std::vector<int>>(opts, "fixed_cards")) {
        if (int(v->size()) > config.member)
            throw std::invalid_argument("Fixed cards size exceeds member count.");
        for (int card_id : *v) {
            findOrThrowVec(out.dataProvider.masterData->cards, [&](const Card& it) {
                return it.id == card_id;
            }, "Invalid fixed card ID: " + std::to_string(card_id));
        }
        config.fixedCards = *v;
    }

    // fixed characters
    if (auto v = jsonOpt<std::vector<int>>(opts, "fixed_characters")) {
        if (int(v->size()) > config.member)
            throw std::invalid_argument("Fixed characters size exceeds member count.");
        if (is_challenge_live)
            throw std::invalid_argument("fixed_characters is not valid for challenge live.");
        for (int cid : *v) {
            if (cid < 1 || cid > 26)
                throw std::invalid_argument("Invalid fixed character ID: " + std::to_string(cid));
        }
        config.fixedCharacters = *v;
    }

    // forced leader
    if (auto v = jsonOpt<int>(opts, "forcedLeaderCharacterId")) {
        if (*v < 1 || *v > 26)
            throw std::invalid_argument("Invalid forced leader character ID: " + std::to_string(*v));
        config.forcedLeaderCharacterId = *v;
    }

    // skill reference choose strategy
    {
        std::string s = jsonOpt<std::string>(opts, "skill_reference_choose_strategy")
                            .value_or(DEFAULT_SKILL_REFERENCE_CHOOSE_STRATEGY);
        if (!VALID_SKILL_REFERENCE_CHOOSE_STRATEGIES.count(s))
            throw std::invalid_argument("Invalid skill reference choose strategy: " + s);
        if (s == "average") config.skillReferenceChooseStrategy = SkillReferenceChooseStrategy::Average;
        else if (s == "max") config.skillReferenceChooseStrategy = SkillReferenceChooseStrategy::Max;
        else if (s == "min") config.skillReferenceChooseStrategy = SkillReferenceChooseStrategy::Min;
    }

    if (auto v = jsonOpt<bool>(opts, "keep_after_training_state"))
        config.keepAfterTrainingState = *v;

    if (auto v = jsonOpt<int>(opts, "multi_live_teammate_score_up")) {
        config.multiTeammateScoreUp = *v;
        if (!Enums::LiveType::isMulti(out.liveType))
            throw std::invalid_argument("multi_live_teammate_score_up is only valid for multi live.");
        if (*v < 0 || *v > 1000)
            throw std::invalid_argument("Invalid multi live teammate score up: " + std::to_string(*v));
    }
    if (auto v = jsonOpt<int>(opts, "multi_live_teammate_power")) {
        config.multiTeammatePower = *v;
        if (!Enums::LiveType::isMulti(out.liveType))
            throw std::invalid_argument("multi_live_teammate_power is only valid for multi live.");
        if (*v < 0 || *v > 10000000)
            throw std::invalid_argument("Invalid multi live teammate power: " + std::to_string(*v));
    }
    if (auto v = jsonOpt<bool>(opts, "best_skill_as_leader"))
        config.bestSkillAsLeader = *v;
    if (auto v = jsonOpt<double>(opts, "multi_live_score_up_lower_bound")) {
        if (!Enums::LiveType::isMulti(out.liveType))
            throw std::invalid_argument("multi_live_score_up_lower_bound is only valid for multi live.");
        config.multiScoreUpLowerBound = *v;
    }

    // skill order choose strategy
    std::string skill_order_choose_strategy = jsonOpt<std::string>(opts, "skill_order_choose_strategy")
                                                  .value_or(DEFAULT_SKILL_ORDER_CHOOSE_STRATEGY);
    if (!VALID_SKILL_ORDER_CHOOSE_STRATEGIES.count(skill_order_choose_strategy))
        throw std::invalid_argument("Invalid skill order choose strategy: " + skill_order_choose_strategy);
    if (skill_order_choose_strategy == "average") config.liveSkillOrder = LiveSkillOrder::average;
    else if (skill_order_choose_strategy == "max") config.liveSkillOrder = LiveSkillOrder::best;
    else if (skill_order_choose_strategy == "min") config.liveSkillOrder = LiveSkillOrder::worst;
    else if (skill_order_choose_strategy == "specific") config.liveSkillOrder = LiveSkillOrder::specific;

    if (auto v = jsonOpt<std::vector<int>>(opts, "specific_skill_order")) {
        if (skill_order_choose_strategy != "specific")
            throw std::invalid_argument("specific_skill_order is only valid when skill_order_choose_strategy is specific.");
        if (int(v->size()) != config.member)
            throw std::invalid_argument("specific_skill_order size must equal to member count.");
        for (int idx : *v) {
            if (idx < 0 || idx >= config.member)
                throw std::invalid_argument("Invalid specific skill order index: " + std::to_string(idx));
        }
        config.specificSkillOrder = *v;
    } else if (skill_order_choose_strategy == "specific") {
        throw std::invalid_argument("specific_skill_order is required when skill_order_choose_strategy is specific.");
    }

    if (auto v = jsonOpt<int>(opts, "timeout_ms")) {
        config.timeout_ms = *v;
        if (config.timeout_ms < 0)
            throw std::invalid_argument("Invalid timeout: " + std::to_string(config.timeout_ms));
    }

    // rarity card configs
    auto applyCardConfig = [](CardConfig& dst, const json_view& src) {
        if (src.contains("disable") && !src["disable"].is_null()) dst.disable = src["disable"].get<bool>();
        if (src.contains("level_max") && !src["level_max"].is_null()) dst.rankMax = src["level_max"].get<bool>();
        if (src.contains("episode_read") && !src["episode_read"].is_null()) dst.episodeRead = src["episode_read"].get<bool>();
        if (src.contains("master_max") && !src["master_max"].is_null()) dst.masterMax = src["master_max"].get<bool>();
        if (src.contains("skill_max") && !src["skill_max"].is_null()) dst.skillMax = src["skill_max"].get<bool>();
        if (src.contains("canvas") && !src["canvas"].is_null()) dst.canvas = src["canvas"].get<bool>();
        if (src.contains("level") && !src["level"].is_null()) dst.level = src["level"].get<int>();
        if (src.contains("skill_level") && !src["skill_level"].is_null()) dst.skillLevel = src["skill_level"].get<int>();
        if (src.contains("master_rank") && !src["master_rank"].is_null()) dst.masterRank = src["master_rank"].get<int>();
        if (src.contains("episode_read_count") && !src["episode_read_count"].is_null()) dst.episodeReadCount = src["episode_read_count"].get<int>();
    };

    const std::vector<std::pair<std::string, std::string>> rarityKeys = {
        {"rarity_1", "rarity_1_config"},
        {"rarity_2", "rarity_2_config"},
        {"rarity_3", "rarity_3_config"},
        {"rarity_birthday", "rarity_birthday_config"},
        {"rarity_4", "rarity_4_config"},
    };
    for (const auto& [rarity, key] : rarityKeys) {
        CardConfig cc;
        if (opts.contains(key) && !opts[key].is_null())
            applyCardConfig(cc, opts[key]);
        config.cardConfig[mapEnum(EnumMap::cardRarityType, rarity)] = cc;
    }

    if (opts.contains("single_card_configs") && !opts["single_card_configs"].is_null()) {
        for (const auto& sc : opts["single_card_configs"]) {
            CardConfig cc;
            applyCardConfig(cc, sc);
            int card_id = sc.at("card_id").get<int>();
            config.singleCardConfig[card_id] = cc;
        }
    }

    if (auto v = jsonOpt<bool>(opts, "support_master_max")) config.supportMasterMax = *v;
    if (auto v = jsonOpt<bool>(opts, "support_skill_max")) config.supportSkillMax = *v;

    // SA options
    if (algorithm == "sa" && opts.contains("sa_options") && !opts["sa_options"].is_null()) {
        json_view sa = opts["sa_options"];
        if (auto v = jsonOpt<int>(sa, "run_num")) config.saRunCount = *v;
        if (config.saRunCount < 1)
            throw std::invalid_argument("Invalid sa run count: " + std::to_string(config.saRunCount));
        if (auto v = jsonOpt<int>(sa, "seed")) config.saSeed = *v;
        if (auto v = jsonOpt<int>(sa, "max_iter")) config.saMaxIter = *v;
        if (config.saMaxIter < 1)
            throw std::invalid_argument("Invalid sa max iter: " + std::to_string(config.saMaxIter));
        if (auto v = jsonOpt<int>(sa, "max_no_improve_iter")) config.saMaxIterNoImprove = *v;
        if (config.saMaxIterNoImprove < 1)
            throw std::invalid_argument("Invalid sa max no improve iter: " + std::to_string(config.saMaxIterNoImprove));
        if (auto v = jsonOpt<int>(sa, "time_limit_ms")) config.saMaxTimeMs = *v;
        if (config.saMaxTimeMs < 0)
            throw std::invalid_argument("Invalid sa max time ms: " + std::to_string(config.saMaxTimeMs));
        if (auto v = jsonOpt<double>(sa, "start_temprature")) config.saStartTemperature = *v;
        if (config.saStartTemperature < 0)
            throw std::invalid_argument("Invalid sa start temperature: " + std::to_string(config.saStartTemperature));
        if (auto v = jsonOpt<double>(sa, "cooling_rate")) config.saCoolingRate = *v;
        if (config.saCoolingRate < 0 || config.saCoolingRate > 1)
            throw std::invalid_argument("Invalid sa cooling rate: " + std::to_string(config.saCoolingRate));
        if (auto v = jsonOpt<bool>(sa, "debug")) config.saDebug = *v;
    }

    // GA options
    if (opts.contains("ga_options") && !opts["ga_options"].is_null()) {
        json_view ga = opts["ga_options"];
        if (auto v = jsonOpt<int>(ga, "seed")) config.gaSeed = *v;
        if (auto v = jsonOpt<bool>(ga, "debug")) config.gaDebug = *v;
        if (auto v = jsonOpt<int>(ga, "max_iter")) config.gaMaxIter = *v;
        if (config.gaMaxIter < 1)
            throw std::invalid_argument("Invalid ga max iter: " + std::to_string(config.gaMaxIter));
        if (auto v = jsonOpt<int>(ga, "max_no_improve_iter")) config.gaMaxIterNoImprove = *v;
        if (config.gaMaxIterNoImprove < 1)
            throw std::invalid_argument("Invalid ga max no improve iter: " + std::to_string(config.gaMaxIterNoImprove));
        if (auto v = jsonOpt<int>(ga, "pop_size")) config.gaPopSize = *v;
        if (config.gaPopSize < 1)
            throw std::invalid_argument("Invalid ga pop size: " + std::to_string(config.gaPopSize));
        if (auto v = jsonOpt<int>(ga, "parent_size")) config.gaParentSize = *v;
        if (config.gaParentSize < 1 || config.gaParentSize > config.gaPopSize)
            throw std::invalid_argument("Invalid ga parent size: " + std::to_string(config.gaParentSize));
        if (auto v = jsonOpt<int>(ga, "elite_size")) config.gaEliteSize = *v;
        if (config.gaEliteSize < 0 || config.gaEliteSize > config.gaPopSize)
            throw std::invalid_argument("Invalid ga elite size: " + std::to_string(config.gaEliteSize));
        if (auto v = jsonOpt<double>(ga, "crossover_rate")) config.gaCrossoverRate = *v;
        if (config.gaCrossoverRate < 0 || config.gaCrossoverRate > 1)
            throw std::invalid_argument("Invalid ga crossover rate: " + std::to_string(config.gaCrossoverRate));
        if (auto v = jsonOpt<double>(ga, "base_mutation_rate")) config.gaBaseMutationRate = *v;
        if (config.gaBaseMutationRate < 0 || config.gaBaseMutationRate > 1)
            throw std::invalid_argument("Invalid ga base mutation rate: " + std::to_string(config.gaBaseMutationRate));
        if (auto v = jsonOpt<double>(ga, "no_improve_iter_to_mutation_rate"))
            config.gaNoImproveIterToMutationRate = *v;
    }

    return out;
}

// Engine result → JSON ----------------------------------------------------

yyjson_mut_val* deckToJson(yyjson_mut_doc* doc, const RecommendDeck& deck) {
    yyjson_mut_val* j = yyjson_mut_obj(doc);
    if (!j) throw std::runtime_error("Failed to allocate deck JSON object.");
    jsonAdd(doc, j, "score", deck.score);
    jsonAdd(doc, j, "live_score", deck.liveScore);
    jsonAdd(doc, j, "mysekai_event_point", deck.mysekaiEventPoint);
    jsonAdd(doc, j, "total_power", deck.power.total);
    jsonAdd(doc, j, "base_power", deck.power.base);
    jsonAdd(doc, j, "area_item_bonus_power", deck.power.areaItemBonus);
    jsonAdd(doc, j, "character_bonus_power", deck.power.characterBonus);
    jsonAdd(doc, j, "honor_bonus_power", deck.power.honorBonus);
    jsonAdd(doc, j, "fixture_bonus_power", deck.power.fixtureBonus);
    jsonAdd(doc, j, "gate_bonus_power", deck.power.gateBonus);
    jsonAdd(doc, j, "event_bonus_rate", deck.eventBonus.value_or(0.0));
    jsonAdd(doc, j, "support_deck_bonus_rate", deck.supportDeckBonus.value_or(0.0));
    jsonAdd(doc, j, "multi_live_score_up", deck.multiLiveScoreUp);

    yyjson_mut_val* supportDeckCards = yyjson_mut_arr(doc);
    if (!supportDeckCards) throw std::runtime_error("Failed to allocate support deck cards JSON array.");
    if (deck.supportDeckCards.has_value()) {
        for (const auto& c : deck.supportDeckCards.value()) {
            yyjson_mut_val* cj = yyjson_mut_obj(doc);
            if (!cj) throw std::runtime_error("Failed to allocate support deck card JSON object.");
            jsonAdd(doc, cj, "card_id", c.cardId);
            jsonAdd(doc, cj, "bonus", c.supportDeckBonus.value_or(0.0));
            jsonAdd(doc, cj, "skill_level", c.skillLevel);
            jsonAdd(doc, cj, "master_rank", c.masterRank);
            jsonAdd(doc, cj, "level", c.level);
            jsonAdd(doc, cj, "after_training", c.afterTraining);
            jsonAdd(doc, cj, "default_image", mappedEnumToString(EnumMap::defaultImage, c.defaultImage));
            jsonArrayAppend(supportDeckCards, cj);
        }
    }
    jsonAddValue(doc, j, "support_deck_cards", supportDeckCards);

    yyjson_mut_val* cards = yyjson_mut_arr(doc);
    if (!cards) throw std::runtime_error("Failed to allocate cards JSON array.");
    for (const auto& c : deck.cards) {
        yyjson_mut_val* cj = yyjson_mut_obj(doc);
        if (!cj) throw std::runtime_error("Failed to allocate card JSON object.");
        jsonAdd(doc, cj, "card_id", c.cardId);
        jsonAdd(doc, cj, "total_power", c.power.total);
        jsonAdd(doc, cj, "base_power", c.power.base);
        jsonAdd(doc, cj, "event_bonus_rate", c.eventBonus.value_or(0.0));
        jsonAdd(doc, cj, "master_rank", c.masterRank);
        jsonAdd(doc, cj, "level", c.level);
        jsonAdd(doc, cj, "skill_level", c.skillLevel);
        jsonAdd(doc, cj, "skill_score_up", c.skill.scoreUp);
        jsonAdd(doc, cj, "skill_life_recovery", c.skill.lifeRecovery);
        jsonAdd(doc, cj, "episode1_read", c.episode1Read);
        jsonAdd(doc, cj, "episode2_read", c.episode2Read);
        jsonAdd(doc, cj, "after_training", c.afterTraining);
        jsonAdd(doc, cj, "default_image", mappedEnumToString(EnumMap::defaultImage, c.defaultImage));
        jsonAdd(doc, cj, "has_canvas_bonus", c.hasCanvasBonus);
        jsonArrayAppend(cards, cj);
    }
    jsonAddValue(doc, j, "cards", cards);
    return j;
}

yyjson_mut_val* areaItemToJson(yyjson_mut_doc* doc, const RecommendAreaItem& item) {
    yyjson_mut_val* j = yyjson_mut_obj(doc);
    if (!j) throw std::runtime_error("Failed to allocate area item JSON object.");
    jsonAdd(doc, j, "area_id", item.areaId);
    jsonAdd(doc, j, "area_type", mappedEnumToString(EnumMap::areaType, item.areaType));
    jsonAdd(doc, j, "area_view_type", mappedEnumToString(EnumMap::viewType, item.areaViewType));
    jsonAdd(doc, j, "area_item_id", item.areaItemId);
    jsonAdd(doc, j, "next_level", item.nextLevel);
    jsonAdd(doc, j, "shop_item_id", item.shopItemId);
    yyjson_mut_val* cost = yyjson_mut_obj(doc);
    if (!cost) throw std::runtime_error("Failed to allocate area item cost JSON object.");
    jsonAdd(doc, cost, "coin", item.cost.coin);
    jsonAdd(doc, cost, "seed", item.cost.seed);
    jsonAdd(doc, cost, "szk", item.cost.szk);
    jsonAddValue(doc, j, "cost", cost);
    jsonAdd(doc, j, "power", item.power);
    jsonAdd(doc, j, "power_per_coin", item.powerPerCoin);
    return j;
}

yyjson_mut_val* recommendMusicToJson(yyjson_mut_doc* doc, const RecommendMusic& music) {
    yyjson_mut_val* j = yyjson_mut_obj(doc);
    if (!j) throw std::runtime_error("Failed to allocate music JSON object.");
    jsonAdd(doc, j, "music_id", music.musicId);
    jsonAdd(doc, j, "difficulty", mappedEnumToString(EnumMap::musicDifficulty, music.difficulty));
    jsonAdd(doc, j, "live_score", music.liveScore);
    if (music.eventPoint.has_value()) {
        jsonAdd(doc, j, "event_point", music.eventPoint.value());
    } else if (!yyjson_mut_obj_add_null(doc, j, "event_point")) {
        throw std::runtime_error("Failed to add JSON null field: event_point");
    }
    return j;
}

yyjson_mut_val* liveExactDetailToJson(yyjson_mut_doc* doc, const LiveExactDetail& detail) {
    yyjson_mut_val* j = yyjson_mut_obj(doc);
    if (!j) throw std::runtime_error("Failed to allocate exact live JSON object.");
    jsonAdd(doc, j, "total", detail.total);
    jsonAdd(doc, j, "active_bonus", detail.activeBonus);
    yyjson_mut_val* notes = yyjson_mut_arr(doc);
    if (!notes) throw std::runtime_error("Failed to allocate exact live notes JSON array.");
    for (const auto& note : detail.notes) {
        yyjson_mut_val* nj = yyjson_mut_obj(doc);
        if (!nj) throw std::runtime_error("Failed to allocate exact live note JSON object.");
        jsonAdd(doc, nj, "note_coefficient", note.noteCoefficient);
        jsonAdd(doc, nj, "combo_coefficient", note.comboCoefficient);
        jsonAdd(doc, nj, "judge_coefficient", note.judgeCoefficient);
        yyjson_mut_val* effects = yyjson_mut_arr(doc);
        if (!effects) throw std::runtime_error("Failed to allocate effect bonuses JSON array.");
        for (double effect : note.effectBonuses) {
            jsonArrayAppend(effects, yyjson_mut_real(doc, effect));
        }
        jsonAddValue(doc, nj, "effect_bonuses", effects);
        jsonAdd(doc, nj, "score", note.score);
        jsonArrayAppend(notes, nj);
    }
    jsonAddValue(doc, j, "notes", notes);
    return j;
}

DeckDetail deckFromJson(const json_view& j) {
    DeckDetail deck{};
    deck.power = DeckPowerDetail{
        .base = j.value("base_power", 0),
        .areaItemBonus = j.value("area_item_bonus_power", 0),
        .characterBonus = j.value("character_bonus_power", 0),
        .honorBonus = j.value("honor_bonus_power", 0),
        .fixtureBonus = j.value("fixture_bonus_power", 0),
        .gateBonus = j.value("gate_bonus_power", 0),
        .total = j.value("total_power", 0),
    };
    deck.eventBonus = j.value("event_bonus_rate", 0.0);
    deck.supportDeckBonus = j.value("support_deck_bonus_rate", 0.0);
    deck.multiLiveScoreUp = j.value("multi_live_score_up", 0.0);
    for (const auto& c : j.value("cards", json_view::array())) {
        std::string defaultImage = c.value("default_image", "");
        deck.cards.push_back(DeckCardDetail{
            .cardId = c.value("card_id", 0),
            .level = c.value("level", 0),
            .skillLevel = c.value("skill_level", 0),
            .masterRank = c.value("master_rank", 0),
            .power = DeckCardPowerDetail{
                .base = c.value("base_power", 0),
                .areaItemBonus = 0,
                .characterBonus = 0,
                .fixtureBonus = 0,
                .gateBonus = 0,
                .total = c.value("total_power", 0),
            },
            .eventBonus = c.value("event_bonus_rate", 0.0),
            .skill = DeckCardSkillDetail{
                .scoreUp = c.value("skill_score_up", 0.0),
                .lifeRecovery = c.value("skill_life_recovery", 0.0),
            },
            .episode1Read = c.value("episode1_read", false),
            .episode2Read = c.value("episode2_read", false),
            .afterTraining = c.value("after_training", false),
            .defaultImage = defaultImage.empty() ? 0 : mapEnum(EnumMap::defaultImage, defaultImage),
            .hasCanvasBonus = c.value("has_canvas_bonus", false),
        });
    }
    return deck;
}

int getLiveTypeFromOptions(const json_view& opts) {
    auto liveTypeStr = jsonOpt<std::string>(opts, "live_type");
    if (!liveTypeStr) throw std::invalid_argument("live_type is required.");
    if (!VALID_LIVE_TYPES.count(*liveTypeStr) || *liveTypeStr == "mysekai")
        throw std::invalid_argument("Invalid live type: " + *liveTypeStr);
    return mapEnum(EnumMap::liveType, *liveTypeStr);
}

LiveSkillOrder getLiveSkillOrderFromOptions(const json_view& opts) {
    std::string strategy = jsonOpt<std::string>(opts, "skill_order_choose_strategy")
        .value_or(DEFAULT_SKILL_ORDER_CHOOSE_STRATEGY);
    if (!VALID_SKILL_ORDER_CHOOSE_STRATEGIES.count(strategy))
        throw std::invalid_argument("Invalid skill order choose strategy: " + strategy);
    if (strategy == "average") return LiveSkillOrder::average;
    if (strategy == "max") return LiveSkillOrder::best;
    if (strategy == "min") return LiveSkillOrder::worst;
    return LiveSkillOrder::specific;
}

int getEventTypeFromOptions(const json_view& opts, const DataProvider& dataProvider) {
    if (auto eventId = jsonOpt<int>(opts, "event_id")) {
        EventService eventService(dataProvider);
        return eventService.getEventType(*eventId);
    }
    std::string eventType = jsonOpt<std::string>(opts, "event_type").value_or("marathon");
    if (!VALID_EVENT_TYPES.count(eventType))
        throw std::invalid_argument("Invalid event type: " + eventType);
    return mapEnum(EnumMap::eventType, eventType);
}

// Public engine wrapper ---------------------------------------------------

class WasmSekaiDeckRecommend {
    mutable std::map<Region, std::shared_ptr<MasterData>> region_masterdata;
    mutable std::map<Region, std::shared_ptr<MusicMetas>> region_musicmetas;

public:
    WasmSekaiDeckRecommend() = default;

    // emscripten::val accepts plain JS objects; we serialize them through JSON
    // to keep type handling identical to the recommend() entry.
    void updateMasterdataFromObject(emscripten::val data, const std::string& region) {
        auto it = REGION_ENUM_MAP.find(region);
        if (it == REGION_ENUM_MAP.end())
            throw std::invalid_argument("Invalid region: " + region);
        emscripten::val keys = emscripten::val::global("Object").call<emscripten::val>("keys", data);
        unsigned len = keys["length"].as<unsigned>();
        std::map<std::string, std::string> m;
        for (unsigned i = 0; i < len; ++i) {
            std::string key = keys[i].as<std::string>();
            emscripten::val v = data[key];
            // accept either pre-serialized string or live JS object/array
            if (v.isString()) {
                m[key] = v.as<std::string>();
            } else {
                emscripten::val js = emscripten::val::global("JSON").call<emscripten::val>("stringify", v);
                m[key] = js.as<std::string>();
            }
        }
        region_masterdata[it->second] = std::make_shared<MasterData>();
        region_masterdata[it->second]->loadFromStrings(m);
    }

    void updateMusicmetasFromString(const std::string& s, const std::string& region) {
        auto it = REGION_ENUM_MAP.find(region);
        if (it == REGION_ENUM_MAP.end())
            throw std::invalid_argument("Invalid region: " + region);
        region_musicmetas[it->second] = std::make_shared<MusicMetas>();
        region_musicmetas[it->second]->loadFromString(s);
    }

    std::string recommend(const std::string& optionsJson) {
        auto optsDoc = json_doc::parse(optionsJson, "wasm recommend options");
        json_view opts = optsDoc.root();
        auto prepared = buildOptions(opts, region_masterdata, region_musicmetas);

        std::vector<RecommendDeck> result;
        if (prepared.config.target == RecommendTarget::Mysekai) {
            MysekaiDeckRecommend r(prepared.dataProvider);
            result = r.recommendMysekaiDeck(prepared.eventId, prepared.config, prepared.worldBloomCharacterId);
        } else if (Enums::LiveType::isChallenge(prepared.liveType)) {
            ChallengeLiveDeckRecommend r(prepared.dataProvider);
            result = r.recommendChallengeLiveDeck(prepared.liveType, prepared.challengeLiveCharacterId, prepared.config);
        } else {
            EventDeckRecommend r(prepared.dataProvider);
            result = r.recommendEventDeck(prepared.eventId, prepared.liveType, prepared.config, prepared.worldBloomCharacterId);
        }

        MutableJsonDoc outDoc;
        yyjson_mut_val* out = yyjson_mut_obj(outDoc.get());
        if (!out) throw std::runtime_error("Failed to allocate recommend output JSON object.");
        yyjson_mut_val* decks = yyjson_mut_arr(outDoc.get());
        if (!decks) throw std::runtime_error("Failed to allocate recommend decks JSON array.");
        for (const auto& d : result) jsonArrayAppend(decks, deckToJson(outDoc.get(), d));
        jsonAddValue(outDoc.get(), out, "decks", decks);
        return dumpMutableJson(out);
    }

    std::string recommendAreaItems(const std::string& optionsJson) {
        auto optsDoc = json_doc::parse(optionsJson, "wasm area item recommend options");
        json_view opts = optsDoc.root();

        auto regionStr = jsonOpt<std::string>(opts, "region");
        if (!regionStr) throw std::invalid_argument("region is required.");
        auto regionIt = REGION_ENUM_MAP.find(*regionStr);
        if (regionIt == REGION_ENUM_MAP.end())
            throw std::invalid_argument("Invalid region: " + *regionStr);
        Region region = regionIt->second;
        if (!region_masterdata.count(region))
            throw std::invalid_argument("Master data not found for region: " + *regionStr);

        auto userdata = std::make_shared<UserData>();
        if (opts.contains("user_data_file_path") && !opts["user_data_file_path"].is_null()) {
            userdata->loadFromFile(opts["user_data_file_path"].get<std::string>());
        } else if (opts.contains("user_data_str") && !opts["user_data_str"].is_null()) {
            userdata->loadFromString(extractUserDataStr(opts["user_data_str"]));
        } else if (opts.contains("user_data") && !opts["user_data"].is_null()) {
            userdata->loadFromString(extractUserDataStr(opts["user_data"]));
        } else {
            throw std::invalid_argument("Either user_data / user_data_file_path / user_data_str is required.");
        }
        auto cardIds = jsonOpt<std::vector<int>>(opts, "card_ids");
        if (!cardIds || cardIds->empty())
            throw std::invalid_argument("card_ids is required.");

        DataProvider dataProvider{
            region,
            region_masterdata[region],
            userdata,
            region_musicmetas.count(region) ? region_musicmetas.at(region) : std::shared_ptr<MusicMetas>{},
        };
        dataProvider.init();

        AreaItemRecommend r(dataProvider);
        auto result = r.recommendAreaItem(*cardIds);
        MutableJsonDoc outDoc;
        yyjson_mut_val* out = yyjson_mut_arr(outDoc.get());
        if (!out) throw std::runtime_error("Failed to allocate area items JSON array.");
        for (const auto& item : result) {
            jsonArrayAppend(out, areaItemToJson(outDoc.get(), item));
        }
        return dumpMutableJson(out);
    }

    std::string recommendMusic(const std::string& optionsJson) {
        auto optsDoc = json_doc::parse(optionsJson, "wasm music recommend options");
        json_view opts = optsDoc.root();

        auto regionStr = jsonOpt<std::string>(opts, "region");
        if (!regionStr) throw std::invalid_argument("region is required.");
        auto regionIt = REGION_ENUM_MAP.find(*regionStr);
        if (regionIt == REGION_ENUM_MAP.end())
            throw std::invalid_argument("Invalid region: " + *regionStr);
        Region region = regionIt->second;
        if (!region_masterdata.count(region))
            throw std::invalid_argument("Master data not found for region: " + *regionStr);
        if (!region_musicmetas.count(region))
            throw std::invalid_argument("Music metas not found for region: " + *regionStr);
        if (!opts.contains("deck") || opts["deck"].is_null())
            throw std::invalid_argument("deck is required.");

        auto userdata = std::make_shared<UserData>();
        DataProvider dataProvider{
            region,
            region_masterdata[region],
            userdata,
            region_musicmetas[region],
        };
        int liveType = getLiveTypeFromOptions(opts);
        int eventType = getEventTypeFromOptions(opts, dataProvider);
        if (eventType == Enums::EventType::cheerful && liveType == Enums::LiveType::multi_live) {
            liveType = Enums::LiveType::cheerful_live;
        }
        auto liveSkillOrder = getLiveSkillOrderFromOptions(opts);
        auto specificSkillOrder = jsonOpt<std::vector<int>>(opts, "specific_skill_order");
        if (liveSkillOrder == LiveSkillOrder::specific && !specificSkillOrder) {
            throw std::invalid_argument("specific_skill_order is required when skill_order_choose_strategy is specific.");
        }
        auto multiTeammateScoreUp = jsonOpt<int>(opts, "multi_live_teammate_score_up");
        auto multiTeammatePower = jsonOpt<int>(opts, "multi_live_teammate_power");
        if ((multiTeammateScoreUp || multiTeammatePower) && !Enums::LiveType::isMulti(liveType)) {
            throw std::invalid_argument("multi live teammate options are only valid for multi live.");
        }

        MusicRecommend r(dataProvider);
        auto result = r.recommendMusic(
            deckFromJson(opts["deck"]),
            liveType,
            eventType,
            liveSkillOrder,
            specificSkillOrder,
            multiTeammateScoreUp,
            multiTeammatePower
        );

        MutableJsonDoc outDoc;
        yyjson_mut_val* out = yyjson_mut_arr(outDoc.get());
        if (!out) throw std::runtime_error("Failed to allocate music recommend JSON array.");
        for (const auto& music : result) {
            jsonArrayAppend(out, recommendMusicToJson(outDoc.get(), music));
        }
        return dumpMutableJson(out);
    }

    std::string calculateExactLive(const std::string& optionsJson) {
        auto optsDoc = json_doc::parse(optionsJson, "wasm exact live options");
        json_view opts = optsDoc.root();

        auto regionStr = jsonOpt<std::string>(opts, "region");
        if (!regionStr) throw std::invalid_argument("region is required.");
        auto regionIt = REGION_ENUM_MAP.find(*regionStr);
        if (regionIt == REGION_ENUM_MAP.end())
            throw std::invalid_argument("Invalid region: " + *regionStr);
        Region region = regionIt->second;
        if (!region_masterdata.count(region))
            throw std::invalid_argument("Master data not found for region: " + *regionStr);
        if (!opts.contains("music_score") || opts["music_score"].is_null())
            throw std::invalid_argument("music_score is required.");

        int liveType = getLiveTypeFromOptions(opts);
        int power = jsonOpt<int>(opts, "power").value_or(0);
        if (power <= 0) throw std::invalid_argument("power must be positive.");
        auto skills = jsonOpt<std::vector<double>>(opts, "skills").value_or(std::vector<double>{});
        std::string musicScoreJson = opts["music_score"].is_string()
            ? opts["music_score"].get<std::string>()
            : dumpJson(opts["music_score"]);
        auto musicScore = MusicScore::fromJsonString(musicScoreJson);

        std::optional<MusicScore> feverMusicScore = std::nullopt;
        if (opts.contains("fever_music_score") && !opts["fever_music_score"].is_null()) {
            std::string feverJson = opts["fever_music_score"].is_string()
                ? opts["fever_music_score"].get<std::string>()
                : dumpJson(opts["fever_music_score"]);
            feverMusicScore = MusicScore::fromJsonString(feverJson);
        }

        DataProvider dataProvider{
            region,
            region_masterdata[region],
            std::make_shared<UserData>(),
            region_musicmetas.count(region) ? region_musicmetas[region] : std::shared_ptr<MusicMetas>{},
        };
        LiveExactCalculator calculator(dataProvider);
        auto detail = calculator.calculate(
            power,
            skills,
            liveType,
            musicScore,
            jsonOpt<int>(opts, "multi_sum_power").value_or(0),
            feverMusicScore
        );
        MutableJsonDoc outDoc;
        return dumpMutableJson(liveExactDetailToJson(outDoc.get(), detail));
    }

    std::string getWorldBloomSupportCards(const std::string& optionsJson) {
        auto optsDoc = json_doc::parse(optionsJson, "wasm world bloom support options");
        json_view opts = optsDoc.root();

        auto regionStr = jsonOpt<std::string>(opts, "region");
        if (!regionStr) throw std::invalid_argument("region is required.");
        auto regionIt = REGION_ENUM_MAP.find(*regionStr);
        if (regionIt == REGION_ENUM_MAP.end())
            throw std::invalid_argument("Invalid region: " + *regionStr);
        Region region = regionIt->second;
        if (!region_masterdata.count(region))
            throw std::invalid_argument("Master data not found for region: " + *regionStr);

        auto userdata = std::make_shared<UserData>();
        if (opts.contains("user_data_file_path") && !opts["user_data_file_path"].is_null()) {
            userdata->loadFromFile(opts["user_data_file_path"].get<std::string>());
        } else if (opts.contains("user_data_str") && !opts["user_data_str"].is_null()) {
            userdata->loadFromString(extractUserDataStr(opts["user_data_str"]));
        } else if (opts.contains("user_data") && !opts["user_data"].is_null()) {
            userdata->loadFromString(extractUserDataStr(opts["user_data"]));
        } else {
            throw std::invalid_argument("Either user_data / user_data_file_path / user_data_str is required.");
        }

        int eventId = 0;
        auto eventIdOpt = jsonOpt<int>(opts, "event_id");
        auto worldBloomTurnOpt = jsonOpt<int>(opts, "world_bloom_event_turn");
        auto worldBloomCharOpt = jsonOpt<int>(opts, "world_bloom_character_id");
        auto eventUnitOpt = jsonOpt<std::string>(opts, "event_unit");

        if (eventIdOpt) {
            eventId = *eventIdOpt;
        } else if (worldBloomTurnOpt) {
            int turn = *worldBloomTurnOpt;
            if (turn < 1 || turn > 3)
                throw std::invalid_argument("Invalid world bloom event turn: " + std::to_string(turn));
            if (turn == 3) {
                if (!worldBloomCharOpt)
                    throw std::invalid_argument("world_bloom_character_id is required for world bloom 3 fake event.");
                int part = region_masterdata[region]->getWorldBloom3PartByCharacterId(*worldBloomCharOpt);
                eventId = region_masterdata[region]->getWorldBloomFakeEventId(turn, part);
            } else {
                if (!eventUnitOpt)
                    throw std::invalid_argument("event_unit is required for world bloom fake event.");
                if (!VALID_UNIT_TYPES.count(*eventUnitOpt))
                    throw std::invalid_argument("Invalid event unit: " + *eventUnitOpt);
                eventId = region_masterdata[region]->getWorldBloomFakeEventId(
                    turn, mapEnum(EnumMap::unit, *eventUnitOpt));
            }
        } else {
            throw std::invalid_argument("event_id or world_bloom_event_turn is required.");
        }

        int characterId = 0;
        if (worldBloomCharOpt) characterId = *worldBloomCharOpt;
        else if (auto v = jsonOpt<int>(opts, "forcedLeaderCharacterId")) characterId = *v;
        if (characterId == 0)
            throw std::invalid_argument("world_bloom_character_id or forcedLeaderCharacterId is required.");
        if (characterId < 1 || characterId > 26)
            throw std::invalid_argument("Invalid world_bloom_character_id or forcedLeaderCharacterId: " + std::to_string(characterId));

        DataProvider dataProvider{
            region,
            region_masterdata[region],
            userdata,
            region_musicmetas.count(region) ? region_musicmetas.at(region) : std::shared_ptr<MusicMetas>{},
        };
        dataProvider.init();

        bool supportMasterMax = jsonOpt<bool>(opts, "support_master_max").value_or(false);
        bool supportSkillMax = jsonOpt<bool>(opts, "support_skill_max").value_or(false);
        bool filterOtherUnit = jsonOpt<bool>(opts, "filter_other_unit").value_or(false);

        CardCalculator cardCalculator(dataProvider);
        std::vector<std::pair<int, double>> result;
        for (const auto& card : userdata->userCards) {
            auto sc = cardCalculator.getSupportDeckCard(
                card, eventId, characterId, supportMasterMax, supportSkillMax, !filterOtherUnit);
            result.emplace_back(sc.cardId, sc.bonus);
        }
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
            return std::tuple(a.second, -a.first) > std::tuple(b.second, -b.first);
        });

        MutableJsonDoc outDoc;
        yyjson_mut_val* out = yyjson_mut_arr(outDoc.get());
        if (!out) throw std::runtime_error("Failed to allocate support cards JSON array.");
        for (const auto& [card_id, bonus] : result) {
            yyjson_mut_val* card = yyjson_mut_obj(outDoc.get());
            if (!card) throw std::runtime_error("Failed to allocate support card JSON object.");
            jsonAdd(outDoc.get(), card, "card_id", card_id);
            jsonAdd(outDoc.get(), card, "bonus", bonus);
            jsonArrayAppend(out, card);
        }
        return dumpMutableJson(out);
    }
};

void wasmInitDataPath(const std::string& path) {
    setStaticDataDir(path);
}

}  // namespace

EMSCRIPTEN_BINDINGS(sekai_deck_recommend) {
    emscripten::class_<WasmSekaiDeckRecommend>("SekaiDeckRecommend")
        .constructor<>()
        .function("updateMasterdataFromObject", &WasmSekaiDeckRecommend::updateMasterdataFromObject)
        .function("updateMusicmetasFromString", &WasmSekaiDeckRecommend::updateMusicmetasFromString)
        .function("recommend", &WasmSekaiDeckRecommend::recommend)
        .function("recommendAreaItems", &WasmSekaiDeckRecommend::recommendAreaItems)
        .function("recommendMusic", &WasmSekaiDeckRecommend::recommendMusic)
        .function("calculateExactLive", &WasmSekaiDeckRecommend::calculateExactLive)
        .function("getWorldBloomSupportCards", &WasmSekaiDeckRecommend::getWorldBloomSupportCards);

    emscripten::function("initDataPath", &wasmInitDataPath);
}

#endif  // __EMSCRIPTEN__
