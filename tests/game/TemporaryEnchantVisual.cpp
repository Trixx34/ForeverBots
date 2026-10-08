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
#include "DB2Stores.h"
#include "DummyData.h"
#include "Item.h"
#include <utility>

uint16 UnitTestDataLoader::ResolveItemVisual(Item const& item, uint32 visibleEnchantmentId)
{
    return item.ResolveVisibleItemVisual(visibleEnchantmentId);
}

namespace
{
    void LoadVisuals()
    {
        static UnitTestDataLoader::DB2<SpellItemEnchantmentEntry, &SpellItemEnchantmentEntry::ID> store(sSpellItemEnchantmentStore);
        static bool loaded = false;
        if (loaded)
            return;
        auto loader = store.Loader();
        // Selected native 70124 and 70205 records agree on these visuals.
        for (auto [id, visual] : { std::pair{283u, 81u}, {284u, 81u}, {525u, 81u}, {1669u, 81u},
            {3u, 32u}, {2u, 33u}, {1u, 61u}, {999u, 0u} })
        {
            auto& row = loader.Add();
            row.ID = id;
            row.ItemVisual = visual;
        }
        loaded = true;
    }

    struct Weapon final : Item
    {
        void Temporary(uint32 id)
        {
            SetUpdateFieldValue(m_values.ModifyValue(&Item::m_itemData).ModifyValue(&UF::ItemData::Enchantment, TEMP_ENCHANTMENT_SLOT)
                .ModifyValue(&UF::ItemEnchantment::ID), id);
        }
    };
}

TEST_CASE("Temporary enchants supply their native visual when no selected visual exists", "[TemporaryEnchantVisual]")
{
    LoadVisuals();
    Weapon weapon;
    for (auto [id, visual] : { std::pair{283u, 81u}, {284u, 81u}, {525u, 81u}, {1669u, 81u},
        {3u, 32u}, {2u, 33u}, {1u, 61u} })
    {
        INFO("Temporary enchant: " << id);
        weapon.Temporary(id);
        CHECK(UnitTestDataLoader::ResolveItemVisual(weapon, 0) == visual);
        CHECK(UnitTestDataLoader::ResolveItemVisual(weapon, 999) == visual);
    }
    weapon.ClearEnchantment(TEMP_ENCHANTMENT_SLOT);
    CHECK(UnitTestDataLoader::ResolveItemVisual(weapon, 0) == 0);
    weapon.Temporary(123456); // Missing data must not invent a visual.
    CHECK(UnitTestDataLoader::ResolveItemVisual(weapon, 0) == 0);
}

TEST_CASE("Selected permanent enchant or illusion visuals retain precedence", "[TemporaryEnchantVisual]")
{
    LoadVisuals();
    Weapon weapon;
    weapon.Temporary(283);
    CHECK(UnitTestDataLoader::ResolveItemVisual(weapon, 3) == 32);
    CHECK(UnitTestDataLoader::ResolveItemVisual(weapon, 2) == 33);
    weapon.Temporary(0);
    CHECK(UnitTestDataLoader::ResolveItemVisual(weapon, 3) == 32);
}
