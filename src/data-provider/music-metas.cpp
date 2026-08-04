#include "data-provider/music-metas.h"

void MusicMetas::loadFromJson(const json_view& j)
{
    this->metas = MusicMeta::fromJsonList(j);
}

void MusicMetas::loadFromFile(const std::string &path)
{
    json_doc doc;
    try {
        this->path = path;
        doc = json_doc::parseFile(path, "music metas file: " + path);
    }
    catch (const JsonFileOpenError&) {
        throw std::runtime_error("Failed to load music metas from file: " + path
            + ", error: Failed to open file: " + path);
    }
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
