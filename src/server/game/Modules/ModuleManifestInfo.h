/*
 * Parsed module.json (doc/module-system/05-phase3-build-discovery-design.md, "module.json schema").
 * No TrinityCore dependencies, so it is unit-testable with the stubs in tests/.
 */

#ifndef TRINITYCORE_MODULE_MANIFEST_INFO_H
#define TRINITYCORE_MODULE_MANIFEST_INFO_H

#include "Define.h"

#include <string>
#include <vector>

struct ModuleManifestInfo
{
    std::string Name;
    std::string Version;
    std::string ProtocolVersion;     // exact-match gate, checked by the caller (ModuleMgr)
    std::string SdkPackageVersion;   // informational
    std::vector<std::string> Tier1Hooks;

    bool Tier2Participates = false;
    uint32 RequestedMaxBots = 0;
    uint32 RequestedMaxNearbyUnits = 0;
    uint32 RequestedMaxActions = 0;
    uint32 RequestedTickInterval = 1;
};

// Returns false and fills error when the JSON is malformed or a required field is missing.
// Required: name, version, protocol_version. Everything else is optional.
bool ParseModuleManifest(std::string const& json, ModuleManifestInfo& out, std::string& error);

#endif // TRINITYCORE_MODULE_MANIFEST_INFO_H
