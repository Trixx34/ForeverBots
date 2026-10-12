// Test-only stand-in for the worldserver's ConfigMgr, so ModuleMgr.cpp compiles outside the server
// build. Values are set directly by the test. Not part of the worldserver build.
#ifndef TRINITYCORE_TEST_STUB_CONFIG_H
#define TRINITYCORE_TEST_STUB_CONFIG_H

#include "Define.h"
#include <map>
#include <string>
#include <string_view>

class StubConfigMgr
{
    public:
        static StubConfigMgr& Get()
        {
            static StubConfigMgr instance;
            return instance;
        }

        std::map<std::string, std::string, std::less<>> Values;

        std::string GetStringDefault(std::string_view name, std::string_view def, bool = false) const
        {
            auto it = Values.find(name);
            return it == Values.end() ? std::string(def) : it->second;
        }

        bool GetBoolDefault(std::string_view name, bool def, bool = false) const
        {
            auto it = Values.find(name);
            return it == Values.end() ? def : (it->second == "1" || it->second == "true");
        }

        int32 GetIntDefault(std::string_view name, int32 def, bool = false) const
        {
            auto it = Values.find(name);
            return it == Values.end() ? def : std::stoi(it->second);
        }
};

#define sConfigMgr (&StubConfigMgr::Get())

#endif // TRINITYCORE_TEST_STUB_CONFIG_H
