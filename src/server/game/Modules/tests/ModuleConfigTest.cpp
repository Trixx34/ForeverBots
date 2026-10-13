// Module config: only a module's own keys are sent, with the prefix removed. Built by hand like the other module tests
// (ModuleConfig.cpp and this file, with the core include paths).

#include "ModuleConfig.h"

#include <cstdio>
#include <map>

namespace
{

int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            ++g_failures;                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                 \
    } while (0)

} // namespace

int main()
{
    std::map<std::string, std::string> values = {
        { "Module.forever-bots.MaxBots", "2" },
        { "Module.forever-bots.StateFile", "bots.state" },
        { "Module.forever-bots-extra.MaxBots", "9" },   // another module whose name starts the same way
        { "Module.forever-bots.", "" },                 // no key name: not a setting
        { "Modules.Enabled", "1" },                     // the core's own key
        { "Module.auto-vendor-trash.MinValueCopper", "500" },
    };
    std::vector<std::string> keys;
    for (auto const& entry : values)
        keys.push_back(entry.first);

    auto valueOf = [&values](std::string const& key) { return values.at(key); };
    auto const entries = ModuleConfigFor("forever-bots", keys, valueOf);

    CHECK(entries.size() == 2);
    std::map<std::string, std::string> got;
    for (auto const& entry : entries)
        got[entry.Key] = entry.Value;
    CHECK(got.size() == 2);
    CHECK(got["MaxBots"] == "2");
    CHECK(got["StateFile"] == "bots.state");

    // A module with no keys gets an empty config.
    CHECK(ModuleConfigFor("nobody", keys, valueOf).empty());

    if (g_failures == 0)
    {
        std::printf("PASS  module config keys\n");
        return 0;
    }
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
}
