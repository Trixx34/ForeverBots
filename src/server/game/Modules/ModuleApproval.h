/*
 * Capability approval store (doc/module-system/06-phase4-approval-audit-design.md).
 *
 * A module is spawned only when the capabilities it declares match what an operator approved. The
 * approved state lives in approved_modules.json next to worldserver.conf, not in Modules.Dir, so it
 * survives rebuilds that regenerate the runtime directory.
 *
 * Only capabilities are hashed: sorted Tier 1 hooks and the Tier 2 block. Name, version and SDK version
 * are excluded, so a bug-fix release does not need re-approval; a change in what the module asks for does.
 */

#ifndef TRINITYCORE_MODULE_APPROVAL_H
#define TRINITYCORE_MODULE_APPROVAL_H

#include "Define.h"

#include <map>
#include <string>
#include <vector>

struct ModuleManifestInfo;

struct CapabilityDeclaration
{
    std::vector<std::string> Hooks; // stored sorted and unique by CapabilitiesOf()

    bool Tier2Participates = false;
    uint32 RequestedMaxBots = 0;
    uint32 RequestedMaxNearbyUnits = 0;
    uint32 RequestedMaxActions = 0;
    uint32 RequestedTickInterval = 0;

    bool operator==(CapabilityDeclaration const& other) const = default;
};

CapabilityDeclaration CapabilitiesOf(ModuleManifestInfo const& manifest);

// Canonical JSON of the hashed fields, with keys in sorted order and hooks sorted and unique. This is the
// exact input to CapabilityHash(); changing its form changes every stored hash, so it is pinned by a test.
std::string CanonicalCapabilityJson(CapabilityDeclaration const& capabilities);

// Lowercase hex SHA-256 of CanonicalCapabilityJson().
std::string CapabilityHash(CapabilityDeclaration const& capabilities);

// Human-readable differences, for the log when a previously approved module changes what it asks for.
std::vector<std::string> DescribeCapabilityChanges(CapabilityDeclaration const& approved, CapabilityDeclaration const& current);

struct ApprovalRecord
{
    std::string ApprovedVersion;
    std::string ApprovedAt;      // UTC, ISO 8601
    std::string CapabilityHash;
    CapabilityDeclaration Capabilities; // kept so a later change can be described, not just detected
};

class ApprovalStore
{
    public:
        // A missing file is an empty store. A file that exists but cannot be read or parsed is an error;
        // the caller must not treat that as "nothing approved and nothing to lose".
        bool Load(std::string const& path, std::string& error);
        bool Save(std::string const& path, std::string& error) const;

        ApprovalRecord const* Find(std::string const& name) const;
        void Set(std::string const& name, ApprovalRecord record);

    private:
        std::map<std::string, ApprovalRecord> _records;
};

// Module directory names are used as file names and as property-tree keys, so only a safe subset is allowed.
bool IsValidModuleName(std::string const& name);

// Current time as UTC ISO 8601, e.g. 2026-10-12T14:00:00Z.
std::string UtcTimestampNow();

#endif // TRINITYCORE_MODULE_APPROVAL_H
