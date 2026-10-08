/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "tc_catch2.h"
#include "PetScaling.h"

namespace
{
    CreatureFamilyEntry Family(float minScale, int32 minLevel, float maxScale, int32 maxLevel)
    {
        CreatureFamilyEntry family{};
        family.MinScale = minScale;
        family.MinScaleLevel = minLevel;
        family.MaxScale = maxScale;
        family.MaxScaleLevel = maxLevel;
        return family;
    }
}

TEST_CASE("Pet family scale interpolates over the complete level interval", "[PetScaling]")
{
    auto family = Family(0.5f, 10, 1.5f, 50);
    for (auto [level, expected] : { std::pair{10, 0.5f}, {20, 0.75f}, {30, 1.0f}, {40, 1.25f}, {50, 1.5f} })
    {
        INFO("Level: " << level);
        CHECK(PetScaling::FamilyScale(family, uint8(level)) == Catch::Approx(expected));
    }
}

TEST_CASE("Native boar family scale approaches its maximum without a jump", "[PetScaling]")
{
    // Retained build-70124 CreatureFamily row 5: scale 0.6..1.0 at levels 1..60.
    auto family = Family(0.6000000238418579f, 1, 1.0f, 60);
    CHECK(PetScaling::FamilyScale(family, 20) == Catch::Approx(0.7288135593f));
    CHECK(PetScaling::FamilyScale(family, 40) == Catch::Approx(0.8644067797f));
    CHECK(PetScaling::FamilyScale(family, 59) == Catch::Approx(0.9932203390f));
    CHECK(PetScaling::FamilyScale(family, 60) == 1.0f);
}

TEST_CASE("Pet family scale preserves endpoint behavior for absent level intervals", "[PetScaling]")
{
    auto family = Family(0.5f, 10, 1.5f, 50);
    CHECK(PetScaling::FamilyScale(family, 0) == 0.5f);
    CHECK(PetScaling::FamilyScale(family, 255) == 1.5f);
    family.MinScaleLevel = family.MaxScaleLevel = 20;
    CHECK(PetScaling::FamilyScale(family, 19) == 0.5f);
    CHECK(PetScaling::FamilyScale(family, 20) == 1.5f);
    family.MinScaleLevel = 30;
    CHECK(PetScaling::FamilyScale(family, 19) == 0.5f);
    CHECK(PetScaling::FamilyScale(family, 20) == 1.5f);
}
