#include "data-provider/music-metas.h"
#include <fstream>
#include <iterator>

void MusicMetas::loadFromJson(const json_view& j)
{
    this->metas = MusicMeta::fromJsonList(j);
}

void MusicMetas::loadFromFile(const std::string &path)
{
    std::string content;
    try {
        this->path = path;
        std::ifstream file(path);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + path);
        }
        content.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    catch (const std::exception &e) {
        throw std::runtime_error("Failed to load music metas from file: " + path + ", error: " + e.what());
    }
    auto doc = json_doc::parse(content, "music metas file: " + path);
    this->loadFromJson(doc.root());
}

void MusicMetas::loadFromString(const std::string& s)
{
    json_doc doc;
    try {
        this->path.clear();
        doc = json_doc::parse(s, "music metas string");
    } 
    catch (const std::exception &e) {
        throw std::runtime_error("Failed to load music metas from string, error: " + std::string(e.what()));
    }
    this->loadFromJson(doc.root());
}
