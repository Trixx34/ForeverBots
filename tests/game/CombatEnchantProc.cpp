#include "tc_catch2.h"
#include "CombatEnchantProc.h"
#include <vector>

TEST_CASE("A successful enchant effect rolls and casts once", "[CombatEnchantProc]")
{
    std::vector<float> chances;
    unsigned casts = 0;
    CombatEnchantProc::Dispatch(37.5f, [&](float chance) { chances.push_back(chance); return true; }, [&] { ++casts; });
    REQUIRE(chances.size() == 1);
    CHECK(chances.front() == 37.5f);
    CHECK(casts == 1);
}

TEST_CASE("A missed enchant effect has no second attempt", "[CombatEnchantProc]")
{
    unsigned rolls = 0, casts = 0;
    CombatEnchantProc::Dispatch(12.5f, [&](float chance) { CHECK(chance == 12.5f); return rolls++ != 0; }, [&] { ++casts; });
    CHECK(rolls == 1);
    CHECK(casts == 0);
}

TEST_CASE("Separate enchant effects retain separate chances and item cast callbacks", "[CombatEnchantProc]")
{
    unsigned rolls = 0;
    std::vector<unsigned> spells;
    auto roll = [&](float chance) { CHECK(chance == 17.25f); return ++rolls != 2; };
    for (unsigned spell : { 8034u, 8037u, 10458u })
        CombatEnchantProc::Dispatch(17.25f, roll, [&] { spells.push_back(spell); });
    CHECK(rolls == 3);
    REQUIRE(spells.size() == 2);
    CHECK(spells[0] == 8034);
    CHECK(spells[1] == 10458);
}
