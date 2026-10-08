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

// Classic 1.60: data for the website armory (contrib/support_site). ".armory export" writes every item the server knows, with the
// values the game shows (DB2 + hotfixes; stats from StatPercentEditor and the random property points, like Item::GetItemStatValue),
// into world.armory_item, and the race, class, skill and achievement names into world.armory_name. Run it after data changes; the
// site runs it through RA when the tables are empty.

#include "Chat.h"
#include "ChatCommand.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "GameTables.h"
#include "ItemEnchantmentMgr.h"
#include "ItemTemplate.h"
#include "ObjectMgr.h"
#include "RBAC.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include "Timer.h"
#include <unordered_map>

using namespace Trinity::ChatCommands;

namespace
{
std::string Quote(std::string text)
{
    WorldDatabase.EscapeString(text);
    return "'" + text + "'";
}

// icon of the item's default appearance (lowest OrderIndex of modifier 0), else the item's own icon
std::unordered_map<uint32, int32> BuildIconMap()
{
    std::unordered_map<uint32, std::pair<int32, int32>> best;   // item -> (order, appearance)
    for (ItemModifiedAppearanceEntry const* ima : sItemModifiedAppearanceStore)
    {
        if (ima->ItemAppearanceModifierID != 0)
            continue;
        auto itr = best.find(ima->ItemID);
        if (itr == best.end() || ima->OrderIndex < itr->second.first)
            best[ima->ItemID] = { ima->OrderIndex, ima->ItemAppearanceID };
    }

    std::unordered_map<uint32, int32> icons;
    for (auto const& [itemId, appearance] : best)
        if (ItemAppearanceEntry const* entry = sItemAppearanceStore.LookupEntry(appearance.second))
            icons[itemId] = entry->DefaultIconFileDataID;
    return icons;
}
}

class classic_armory_commandscript : public CommandScript
{
public:
    classic_armory_commandscript() : CommandScript("classic_armory_commandscript") { }

    std::span<ChatCommandBuilder const> GetCommands() const override
    {
        static ChatCommandTable armoryCommandTable =
        {
            { "export", HandleExportCommand, rbac::RBAC_PERM_COMMAND_RELOAD, Console::Yes },
        };
        static ChatCommandTable commandTable =
        {
            { "armory", armoryCommandTable },
        };
        return commandTable;
    }

    static bool HandleExportCommand(ChatHandler* handler)
    {
        uint32 oldMSTime = getMSTime();
        uint32 items = ExportItems();
        uint32 names = ExportNames();
        handler->PSendSysMessage("Armory: exported %u items and %u names in %u ms.", items, names, GetMSTimeDiffToNow(oldMSTime));
        return true;
    }

    static uint32 ExportNames()
    {
        WorldDatabaseTransaction trans = WorldDatabase.BeginTransaction();
        trans->Append("DELETE FROM armory_name");
        std::string values;
        uint32 rows = 0;
        auto add = [&](char const* kind, uint32 id, char const* name, int32 icon, int32 extra)
        {
            if (!name || !*name)
                return;
            values += Trinity::StringFormat("{}('{}',{},{},{},{})", values.empty() ? "" : ",", kind, id, Quote(name), icon, extra);
            ++rows;
            if (values.size() > 60000)
            {
                trans->Append(("INSERT INTO armory_name (kind, id, name, iconFileDataId, extra) VALUES " + values).c_str());
                values.clear();
            }
        };

        for (ChrRacesEntry const* race : sChrRacesStore)
            add("race", race->ID, race->Name[LOCALE_enUS], 0, race->Alliance);   // 0 Alliance, 1 Horde, 2 neutral
        for (ChrClassesEntry const* cls : sChrClassesStore)
            add("class", cls->ID, cls->Name[LOCALE_enUS], 0, 0);
        for (SkillLineEntry const* skill : sSkillLineStore)
            add("skill", skill->ID, skill->DisplayName[LOCALE_enUS], skill->SpellIconFileID, skill->CategoryID);
        for (AchievementEntry const* achievement : sAchievementStore)
            add("achievement", achievement->ID, achievement->Title[LOCALE_enUS], achievement->IconFileID, achievement->Points);

        if (!values.empty())
            trans->Append(("INSERT INTO armory_name (kind, id, name, iconFileDataId, extra) VALUES " + values).c_str());
        WorldDatabase.DirectCommitTransaction(trans);
        return rows;
    }

    static uint32 ExportItems()
    {
        std::unordered_map<uint32, int32> icons = BuildIconMap();

        WorldDatabaseTransaction trans = WorldDatabase.BeginTransaction();
        trans->Append("DELETE FROM armory_item");

        std::string values;
        uint32 rows = 0, inBatch = 0;
        auto flush = [&]()
        {
            if (values.empty())
                return;
            trans->Append(("INSERT INTO armory_item (id, name, quality, itemLevel, requiredLevel, class, subclass, inventoryType, bonding, "
                "armor, dmgMin, dmgMax, delay, stats, effects, description, iconFileDataId, sellPrice, allowableClass, itemSet, requiredSkill, "
                "requiredSkillRank, flags) VALUES " + values).c_str());
            values.clear();
            inBatch = 0;
        };

        for (auto const& [itemId, proto] : sObjectMgr->GetItemTemplateStore())
        {
            uint32 itemLevel = proto.GetBaseItemLevel();

            std::string stats;
            float points = GetRandomPropertyPoints(itemLevel, proto.GetQuality(), proto.GetInventoryType(), proto.GetSubClass());
            for (uint32 i = 0; i < MAX_ITEM_PROTO_STATS; ++i)
            {
                int32 type = proto.GetStatModifierBonusStat(i);
                if (type < 0 || !proto.GetStatPercentEditor(i))
                    continue;
                float value = float(proto.GetStatPercentEditor(i) * points) * 0.0001f;
                if (GtItemSocketCostPerLevelEntry const* gtCost = sItemSocketCostPerLevelGameTable.GetRow(itemLevel))
                    value -= proto.GetStatPercentageOfSocket(i) * gtCost->SocketCost;
                if (int32 rounded = int32(value + (value >= 0 ? 0.5f : -0.5f)))
                    stats += Trinity::StringFormat("{}{}:{}", stats.empty() ? "" : ",", type, rounded);
            }

            std::string effects;
            for (ItemEffectEntry const* effect : proto.Effects)
            {
                SpellNameEntry const* spellName = sSpellNameStore.LookupEntry(effect->SpellID);
                std::string name = spellName ? spellName->Name[LOCALE_enUS] : "";
                std::erase_if(name, [](char c) { return c == '|' || c == ':'; });
                effects += Trinity::StringFormat("{}{}:{}:{}", effects.empty() ? "" : "|", effect->TriggerType, effect->SpellID, name);
            }

            // ItemTemplate::GetDPS asserts when the damage tables have no row for the item level (some weapons in the data do)
            float dmgMin = 0.0f, dmgMax = 0.0f;
            if (proto.GetClass() == ITEM_CLASS_WEAPON && sItemDamageOneHandStore.LookupEntry(itemLevel) && sItemDamageOneHandCasterStore.LookupEntry(itemLevel)
                && sItemDamageTwoHandStore.LookupEntry(itemLevel) && sItemDamageTwoHandCasterStore.LookupEntry(itemLevel)
                && sItemDamageAmmoStore.LookupEntry(itemLevel))
                proto.GetDamage(itemLevel, dmgMin, dmgMax);

            auto icon = icons.find(itemId);
            int32 iconFileDataId = icon != icons.end() && icon->second ? icon->second : proto.BasicData->IconFileDataID;

            values += Trinity::StringFormat("{}({},{},{},{},{},{},{},{},{},{},{:.1f},{:.1f},{},{},{},{},{},{},{},{},{},{},{})",
                values.empty() ? "" : ",", itemId, Quote(proto.GetName(LOCALE_enUS)), proto.GetQuality(), itemLevel, proto.GetBaseRequiredLevel(),
                proto.GetClass(), proto.GetSubClass(), uint32(proto.GetInventoryType()), uint32(proto.GetBonding()), proto.GetArmor(itemLevel),
                dmgMin, dmgMax, proto.GetDelay(), Quote(stats), Quote(effects), Quote(proto.ExtendedData->Description[LOCALE_enUS]), iconFileDataId,
                proto.GetSellPrice(), proto.GetAllowableClass(), proto.GetItemSet(), proto.GetRequiredSkill(), proto.GetRequiredSkillRank(),
                proto.ExtendedData->Flags[0]);
            ++rows;
            if (++inBatch == 500)
                flush();
        }
        flush();
        WorldDatabase.DirectCommitTransaction(trans);
        return rows;
    }
};

void AddSC_classic_armory_commands()
{
    new classic_armory_commandscript();
}
