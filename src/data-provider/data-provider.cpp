#include "data-provider.h"

#include <cctype>

void DataProvider::init()
{
    if (inited) return;

    std::map<std::string, std::set<int>> unitCharacters = {
        { "lightsound", {1, 2, 3, 4} },
        { "idol", {5, 6, 7, 8} },
        { "street", {9, 10, 11, 12} },
        { "themepark", {13, 14, 15, 16} },
        { "schoolrefusal", {17, 18, 19, 20} },
        { "piapro", {21, 22, 23, 24, 25, 26} },
    };

    // 预处理用户哪些角色有终章称号活动加成
    userData->userCharacterFinalChapterHonorEventBonusMap.clear();
    for (const auto& userHonor : userData->userHonors) {
        try {
            auto& honor = findOrThrow(masterData->honors,  [&](const Honor& it) { 
                return it.id == userHonor.honorId; 
            });
            if (honor.honorRarity == Enums::HonorRarity::high
            || honor.honorRarity == Enums::HonorRarity::highest) {
                auto start_idx = honor.assetbundleName.find("wl_2nd");
                if (start_idx != std::string::npos) {
                    start_idx += 7;
                    auto end_idx = honor.assetbundleName.find("_cp", start_idx);
                    if (end_idx == std::string::npos)
                        continue;
                    auto unit_name = honor.assetbundleName.substr(start_idx, end_idx - start_idx);
                    auto unit_it = unitCharacters.find(unit_name);
                    if (unit_it == unitCharacters.end())
                        continue;
                    auto chapter_start_idx = end_idx + 3;
                    auto chapter_end_idx = chapter_start_idx;
                    while (
                        chapter_end_idx < honor.assetbundleName.size()
                        && std::isdigit(static_cast<unsigned char>(honor.assetbundleName[chapter_end_idx]))
                    ) {
                        chapter_end_idx++;
                    }
                    if (chapter_end_idx == chapter_start_idx)
                        continue;
                    int chapter = std::stoi(honor.assetbundleName.substr(chapter_start_idx, chapter_end_idx - chapter_start_idx));
                    auto& characters = unit_it->second;
                    for (auto& item : masterData->worldBlooms) {
                        // Generated fake WL events use synthetic chapter ordering, so only real WL2
                        // chapters can be used to map a badge to its exact character.
                        if (item.eventId >= 1000
                         || item.eventId == finalChapterEventId
                         || masterData->getWorldBloomEventTurn(item.eventId) != 2)
                            continue;
                        if (characters.count(item.gameCharacterId) && item.chapterNo == chapter) {
                            userData->userCharacterFinalChapterHonorEventBonusMap[item.gameCharacterId] = 50.0;
                            // std::cerr << item.gameCharacterId << " has final chapter honor event bonus" << std::endl;
                        }
                    }
                }
            }
        } catch (const ElementNoFoundError& e) {
            std::cerr << "[warning] honor id " << userHonor.honorId << " appears in user data but not in master data." << std::endl;
        }
    }
}
