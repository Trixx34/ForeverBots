#include "ModuleConfig.h"

#include <string_view>

std::vector<ModuleWire::ConfigEntry> ModuleConfigFor(std::string const& moduleName, std::vector<std::string> const& keys,
                                                     std::function<std::string(std::string const&)> const& valueOf)
{
    std::string const prefix = "Module." + moduleName + ".";

    std::vector<ModuleWire::ConfigEntry> entries;
    for (std::string const& key : keys)
    {
        if (std::string_view(key).substr(0, prefix.size()) != prefix || key.size() == prefix.size())
            continue;
        entries.push_back({ key.substr(prefix.size()), valueOf(key) });
    }
    return entries;
}
