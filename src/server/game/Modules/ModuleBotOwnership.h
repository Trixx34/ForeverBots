/*
 * Which module controls which bot (doc/module-system/04-phase2-module-host-design.md §6).
 *
 * A bot has at most one controlling module. A module may control at most the max_bots granted at handshake.
 * Only the world thread changes ownership, and ModuleMgr guards it with its own mutex, so this class has no locking.
 * It holds no game objects, so it is unit-tested directly (tests/ModuleBotOwnershipTest.cpp).
 */

#ifndef TRINITYCORE_MODULE_BOT_OWNERSHIP_H
#define TRINITYCORE_MODULE_BOT_OWNERSHIP_H

#include "Define.h"

#include <unordered_map>
#include <vector>

class ModuleBotOwnership
{
    public:
        enum class Claim : uint8
        {
            Granted,      // now owned by the requesting module (or already was)
            OwnedByOther, // another module controls it; `owner` says which
            LimitReached  // the module already controls max_bots bots
        };

        // Records that `module` controls `botGuid`. `owner` is set to the controlling module in every case.
        Claim TryClaim(uint64 botGuid, uint32 module, uint32 maxBots, uint32& owner);

        // True if `module` may take one more bot under `maxBots`. Used before spawning, so a refused request
        // does not leave a bot behind.
        bool CanTakeMore(uint32 module, uint32 maxBots) const;

        // Releases a bot the module controls. False if the module did not control it.
        bool Release(uint64 botGuid, uint32 module);

        // Core takes a bot back from whichever module controls it. Returns false if none did.
        bool Revoke(uint64 botGuid, uint32& previousOwner);

        // Forgets every bot the module controls (its session ended). Returns the guids that were released.
        std::vector<uint64> DropModule(uint32 module);

        uint32 OwnedCount(uint32 module) const;
        bool Owner(uint64 botGuid, uint32& owner) const;

    private:
        std::unordered_map<uint64, uint32> _owners; // bot guid -> module
};

#endif // TRINITYCORE_MODULE_BOT_OWNERSHIP_H
