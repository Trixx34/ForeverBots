#include "tc_catch2.h"
#include "Battleground.h"

TEST_CASE("Battleground invited count never wraps below zero", "[BattlegroundHelpers]")
{
    CHECK(Battleground::AfterLeave(0) == 0);
    CHECK(Battleground::AfterLeave(1) == 0);
    CHECK(Battleground::AfterLeave(5) == 4);
}

TEST_CASE("Indoor wmo group test follows the client's MOGP flags", "[BattlegroundHelpers]")
{
    CHECK(Battleground::IsIndoorWmoGroup(0x2000));          // interior only
    CHECK_FALSE(Battleground::IsIndoorWmoGroup(0x8));       // exterior only
    CHECK_FALSE(Battleground::IsIndoorWmoGroup(0x2008));    // interior and exterior: outdoors
    CHECK_FALSE(Battleground::IsIndoorWmoGroup(0));
}

TEST_CASE("Only Warsong Gulch has an enclosed start area", "[BattlegroundHelpers]")
{
    CHECK(Battleground::IsEnclosedStartArea(489));
    CHECK_FALSE(Battleground::IsEnclosedStartArea(529));
    CHECK_FALSE(Battleground::IsEnclosedStartArea(30));
}
