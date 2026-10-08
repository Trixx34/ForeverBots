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
#include "Creature.h"
#include "DB2Structure.h"
#include "TemporarySummon.h"

namespace
{
    struct DetachedGuardian : Guardian
    {
        DetachedGuardian(SummonPropertiesEntry const* properties, Unit* owner)
            : Guardian(properties, owner, false)
        {
            SetLevel(1, false);
        }

        void SetScaling(int32 minLevel, int32 maxLevel, int32 delta = 0)
        {
            SetUpdateFieldValue(m_values.ModifyValue(&Unit::m_unitData).ModifyValue(&UF::UnitData::ScalingLevelMin), minLevel);
            SetUpdateFieldValue(m_values.ModifyValue(&Unit::m_unitData).ModifyValue(&UF::UnitData::ScalingLevelMax), maxLevel);
            SetUpdateFieldValue(m_values.ModifyValue(&Unit::m_unitData).ModifyValue(&UF::UnitData::ScalingLevelDelta), delta);
        }
    };

    SummonPropertiesEntry PetProperties()
    {
        SummonPropertiesEntry properties{};
        properties.ID = 67;
        properties.Control = SUMMON_CATEGORY_PET;
        properties.Title = int32(SummonTitle::Pet);
        return properties;
    }
}

TEST_CASE("Summoned guardians inherit the owner level when scaling limits are absent", "[TemporarySummonLevel]")
{
    Creature owner;
    SummonPropertiesEntry properties = PetProperties();
    for (uint8 ownerLevel : { 10, 11, 40, 60 })
    {
        INFO("Owner level: " << uint32(ownerLevel));
        owner.SetLevel(ownerLevel, false);
        DetachedGuardian summon(&properties, &owner);
        summon.SetScaling(0, 0);

        // Run shared summon initialization without attaching either unit to a Map.
        summon.TempSummon::InitStats(&owner, 10s);
        CHECK(uint32(summon.GetLevel()) == uint32(ownerLevel));
    }
}

TEST_CASE("Summoned guardians without scaling limits have a minimum level of one", "[TemporarySummonLevel]")
{
    Creature owner;
    owner.SetLevel(0, false);
    SummonPropertiesEntry properties = PetProperties();
    DetachedGuardian summon(&properties, &owner);
    summon.SetScaling(0, 0);
    summon.TempSummon::InitStats(&owner, 10s);
    CHECK(summon.GetLevel() == 1);
}

TEST_CASE("Summoned guardians preserve explicit scaling limits", "[TemporarySummonLevel]")
{
    struct LevelCase { uint8 Owner; int32 Min; int32 Max; int32 Delta; uint8 Expected; };
    Creature owner;
    SummonPropertiesEntry properties = PetProperties();
    for (LevelCase const& level : {
        LevelCase{ 10, 20, 70, 0, 20 },
        LevelCase{ 40, 20, 70, 0, 40 },
        LevelCase{ 60, 20, 50, 0, 50 },
        LevelCase{ 60, 5, 5, 0, 5 },
        LevelCase{ 10, 63, 63, 0, 63 },
        LevelCase{ 40, 10, 30, 2, 32 } })
    {
        INFO("Owner: " << uint32(level.Owner) << ", range: " << level.Min << "-" << level.Max << ", delta: " << level.Delta);
        owner.SetLevel(level.Owner, false);
        DetachedGuardian summon(&properties, &owner);
        summon.SetScaling(level.Min, level.Max, level.Delta);
        summon.TempSummon::InitStats(&owner, 10s);
        CHECK(uint32(summon.GetLevel()) == uint32(level.Expected));
    }
}

TEST_CASE("Summoned guardians retain explicitly requested creature levels", "[TemporarySummonLevel]")
{
    Creature owner;
    owner.SetLevel(60, false);
    SummonPropertiesEntry properties = PetProperties();
    properties.Flags[0] = int32(SummonPropertiesFlags::UseCreatureLevel);
    DetachedGuardian summon(&properties, &owner);
    summon.SetLevel(30, false);
    summon.SetScaling(0, 0);
    summon.TempSummon::InitStats(&owner, 10s);
    CHECK(summon.GetLevel() == 30);
}

TEST_CASE("Summoned guardians without properties retain their initial level", "[TemporarySummonLevel]")
{
    Creature owner;
    owner.SetLevel(60, false);
    DetachedGuardian summon(nullptr, &owner);
    summon.SetLevel(42, false);
    summon.TempSummon::InitStats(&owner, 10s);
    CHECK(summon.GetLevel() == 42);
}
