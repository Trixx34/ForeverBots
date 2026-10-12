// Bot ownership rules: one owner per bot, max_bots per module, release and revoke, and the cleanup on session end.
// Built by hand: g++ -std=c++20 -I src/common -I src/server/shared -I src/server/game/Modules
//   src/server/game/Modules/tests/ModuleBotOwnershipTest.cpp src/server/game/Modules/ModuleBotOwnership.cpp

#include "ModuleBotOwnership.h"

#include <cstdio>

namespace
{

int g_failures = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            ++g_failures;                                                           \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                           \
    } while (0)

} // namespace

int main()
{
    ModuleBotOwnership owners;
    uint32 owner = 0;
    using Claim = ModuleBotOwnership::Claim;

    // First claim is granted and recorded.
    CHECK(owners.TryClaim(100, 1, 2, owner) == Claim::Granted);
    CHECK(owner == 1);
    CHECK(owners.OwnedCount(1) == 1);

    // Another module cannot take it, and the owner is reported.
    CHECK(owners.TryClaim(100, 2, 2, owner) == Claim::OwnedByOther);
    CHECK(owner == 1);
    CHECK(owners.OwnedCount(2) == 0);

    // Re-claiming a bot you already control is granted and does not count twice.
    CHECK(owners.TryClaim(100, 1, 2, owner) == Claim::Granted);
    CHECK(owners.OwnedCount(1) == 1);

    // max_bots is enforced per module.
    CHECK(owners.TryClaim(101, 1, 2, owner) == Claim::Granted);
    CHECK(owners.TryClaim(102, 1, 2, owner) == Claim::LimitReached);
    CHECK(!owners.CanTakeMore(1, 2));
    CHECK(owners.Owner(102, owner) == false);

    // Another module has its own budget.
    CHECK(owners.TryClaim(102, 2, 2, owner) == Claim::Granted);
    CHECK(owners.OwnedCount(2) == 1);

    // Release only works for the controlling module.
    CHECK(!owners.Release(100, 2));
    CHECK(owners.Owner(100, owner) && owner == 1);
    CHECK(owners.Release(100, 1));
    CHECK(owners.OwnedCount(1) == 1);
    CHECK(owners.TryClaim(103, 1, 2, owner) == Claim::Granted); // freed budget is usable

    // Revoke takes the bot from whoever controls it and reports them.
    CHECK(owners.Revoke(103, owner));
    CHECK(owner == 1);
    CHECK(!owners.Owner(103, owner));
    CHECK(!owners.Revoke(103, owner)); // already gone

    // DropModule releases everything the session owned and leaves the others alone.
    CHECK(owners.TryClaim(200, 1, 5, owner) == Claim::Granted);
    CHECK(owners.TryClaim(201, 1, 5, owner) == Claim::Granted);
    CHECK(owners.TryClaim(202, 2, 5, owner) == Claim::Granted);
    auto dropped = owners.DropModule(1);
    CHECK(dropped.size() == 3); // 101 and 200 and 201 (101 was still owned by module 1)
    CHECK(owners.OwnedCount(1) == 0);
    CHECK(owners.OwnedCount(2) == 2); // 102 (module 2 claimed it earlier) and 202
    CHECK(owners.Owner(202, owner) && owner == 2);

    // Zero budget refuses everything.
    CHECK(owners.TryClaim(300, 3, 0, owner) == Claim::LimitReached);

    if (g_failures == 0)
    {
        std::printf("PASS  bot ownership rules\n");
        return 0;
    }
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
}
