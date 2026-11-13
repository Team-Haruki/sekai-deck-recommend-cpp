#ifndef CONFIG_H
#define CONFIG_H

#include "common/collection-utils.h"

// One row of the `configs` masterdata table: a key and its value as a string.
struct Config {
    std::string configKey;
    std::string value;

    static inline std::vector<Config> fromJsonList(const json_view& jsonData) {
        std::vector<Config> configs;
        for (const auto& item : jsonData) {
            Config config;
            config.configKey = item.value("configKey", "");
            config.value = item.value("value", "");
            configs.push_back(config);
        }
        return configs;
    }
};

#endif // CONFIG_H
