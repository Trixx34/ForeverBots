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
#include "DummyData.h"
#include "DB2Structure.h"
#include "SpellInfo.h"

namespace
{
    std::vector<SpellEffectEntry> MakeAuraEffect(AuraType aura, int32 miscValue = 0)
    {
        SpellEffectEntry effect{};
        effect.Effect = SPELL_EFFECT_APPLY_AURA;
        effect.EffectAura = aura;
        effect.EffectMiscValue[0] = miscValue;
        return { effect };
    }
}

TEST_CASE("Dispel immunity recognizes the matching aura dispel type", "[SpellDispelImmunity]")
{
    SpellNameEntry name{};
    for (DispelType dispel : { DISPEL_MAGIC, DISPEL_CURSE, DISPEL_DISEASE, DISPEL_POISON })
    {
        INFO("Dispel type: " << uint32(dispel));
        SpellInfo immunity(&name, DIFFICULTY_NONE, MakeAuraEffect(SPELL_AURA_DISPEL_IMMUNITY, dispel));
        UnitTestDataLoader::LoadSpellImmunities(immunity);
        SpellInfo aura(&name, DIFFICULTY_NONE, MakeAuraEffect(SPELL_AURA_PERIODIC_DAMAGE));
        aura.Dispel = dispel;

        CHECK(immunity.CanSpellProvideImmunityAgainstAura(&aura));
        CHECK(immunity.CanSpellEffectProvideImmunityAgainstAuraEffect(immunity.GetEffect(EFFECT_0), &aura, aura.GetEffect(EFFECT_0)));
    }
}

TEST_CASE("Dispel immunity does not confuse enum values with mask values", "[SpellDispelImmunity]")
{
    SpellNameEntry name{};
    for (DispelType dispel : { DISPEL_MAGIC, DISPEL_CURSE, DISPEL_DISEASE, DISPEL_POISON })
    {
        SpellInfo immunity(&name, DIFFICULTY_NONE, MakeAuraEffect(SPELL_AURA_DISPEL_IMMUNITY, dispel));
        UnitTestDataLoader::LoadSpellImmunities(immunity);
        SpellInfo aura(&name, DIFFICULTY_NONE, MakeAuraEffect(SPELL_AURA_PERIODIC_DAMAGE));
        for (DispelType other : { DISPEL_NONE, DISPEL_MAGIC, DISPEL_CURSE, DISPEL_DISEASE, DISPEL_POISON })
        {
            if (other == dispel)
                continue;

            INFO("Immunity type: " << uint32(dispel) << ", aura type: " << uint32(other));
            aura.Dispel = other;
            CHECK_FALSE(immunity.CanSpellProvideImmunityAgainstAura(&aura));
            CHECK_FALSE(immunity.CanSpellEffectProvideImmunityAgainstAuraEffect(immunity.GetEffect(EFFECT_0), &aura, aura.GetEffect(EFFECT_0)));
        }
    }
}

TEST_CASE("Dispel immunity remains independent of school immunity flags", "[SpellDispelImmunity]")
{
    SpellNameEntry name{};
    SpellInfo immunity(&name, DIFFICULTY_NONE, MakeAuraEffect(SPELL_AURA_DISPEL_IMMUNITY, DISPEL_POISON));
    UnitTestDataLoader::LoadSpellImmunities(immunity);
    SpellInfo aura(&name, DIFFICULTY_NONE, MakeAuraEffect(SPELL_AURA_PERIODIC_DAMAGE));
    aura.Dispel = DISPEL_POISON;
    aura.AttributesEx2 = SPELL_ATTR2_NO_SCHOOL_IMMUNITIES;

    CHECK(immunity.CanSpellProvideImmunityAgainstAura(&aura));
    CHECK(immunity.CanSpellEffectProvideImmunityAgainstAuraEffect(immunity.GetEffect(EFFECT_0), &aura, aura.GetEffect(EFFECT_0)));
    CHECK_FALSE(immunity.CanSpellProvideImmunityAgainstAura(nullptr));
}
