#include "data-provider/user-data.h"

#include <fstream>
#include <iostream>
#include <iterator>


template<typename T>
T loadUserDataJson(const json_view& j, const std::string& key, bool required = true) {
    if (j.contains(key)) {
        return T::fromJson(j[key]);
    } 
    if (required) {
        throw std::runtime_error("user data key not found: " + key);
    }
    std::cerr << "[sekai-deck-recommend-cpp] warning: user data key not found: " + key << std::endl;
    return {};
}

template<typename T>
std::vector<T> loadUserDataJsonList(const json_view& j, const std::string& key, bool required = true) {
    if (j.contains(key)) {
        return T::fromJsonList(j[key]);
    } 
    if (required) {
        throw std::runtime_error("user data key not found: " + key);
    }
    std::cerr << "[sekai-deck-recommend-cpp] warning: user data key not found: " + key << std::endl;
    return {};
}

template<typename T>
std::vector<T> loadOptionalUserDataJsonList(const json_view& j, const std::string& key) {
    if (j.contains(key)) {
        return T::fromJsonList(j[key]);
    }
    return {};
}


void UserData::loadFromJson(const json_view& j) {
    this->userGamedata = loadUserDataJson<UserGameData>(j, "userGamedata");
    this->userAreas = loadUserDataJsonList<UserArea>(j, "userAreas");
    this->userCards = loadUserDataJsonList<UserCard>(j, "userCards");
    this->userChallengeLiveSoloDecks = loadOptionalUserDataJsonList<UserChallengeLiveSoloDeck>(j, "userChallengeLiveSoloDecks");
    this->userCharacters = loadUserDataJsonList<UserCharacter>(j, "userCharacters");
    this->userDecks = loadOptionalUserDataJsonList<UserDeck>(j, "userDecks");
    this->userHonors = loadUserDataJsonList<UserHonor>(j, "userHonors");

    this->userMysekaiCanvases = loadUserDataJsonList<UserMysekaiCanvas>(j, "userMysekaiCanvases", false);
    this->userMysekaiFixtureGameCharacterPerformanceBonuses = loadUserDataJsonList<UserMysekaiFixtureGameCharacterPerformanceBonus>(j, "userMysekaiFixtureGameCharacterPerformanceBonuses", false);
    this->userMysekaiGates = loadUserDataJsonList<UserMysekaiGate>(j, "userMysekaiGates", false);
}

void UserData::loadFromFile(const std::string& path) {
    std::string content;
    try {
        this->path = path;
        std::ifstream file(path);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open user data file: " + path);
        }
        content.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to load user data from file: " + path + ", error: " + e.what());
    }
    auto doc = json_doc::parse(content, "user data file: " + path);
    this->loadFromJson(doc.root());
}

void UserData::loadFromString(const std::string& s) {
    json_doc doc;
    try {
        this->path.clear();
        doc = json_doc::parse(s, "user data string");
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to load user data from bytes, error: " + std::string(e.what()));
    }
    this->loadFromJson(doc.root());
}
