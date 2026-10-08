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

// Classic 1.60 (WoW Forever): GM command to test the Legacy trees. Legacy Points (trait currency 4225, TraitSourced) are not a
// currency the server can add: the client and TraitMgr count them from TraitCurrencySource rows, one point per completed Legacy
// challenge achievement ("Reach Level 25/45/60 for the first time on a <class>"). ".legacy points [count]" completes those
// achievements (only the rows of our ruleset, SuperDistrictSetID 0, in OrderIndex order) for the selected player, which also
// raises the Legacy reward track renown and unlocks the Legacy menu (Player::UpdateClassicLegacyUnlock).

#include "Chat.h"
#include "ChatCommand.h"
#include "DB2Stores.h"
#include "Player.h"
#include "RBAC.h"
#include "ScriptMgr.h"
#include <algorithm>

using namespace Trinity::ChatCommands;

namespace
{
constexpr uint32 LegacyPointsTraitCurrencyID = 4225;

std::vector<TraitCurrencySourceEntry const*> GetLegacyPointSources()
{
    std::vector<TraitCurrencySourceEntry const*> sources;
    for (TraitCurrencySourceEntry const* source : sTraitCurrencySourceStore)
        if (source->TraitCurrencyID == LegacyPointsTraitCurrencyID && !source->SuperDistrictSetID && source->AchievementID)
            sources.push_back(source);

    std::ranges::sort(sources, [](TraitCurrencySourceEntry const* a, TraitCurrencySourceEntry const* b)
    {
        return std::tie(a->OrderIndex, a->ID) < std::tie(b->OrderIndex, b->ID);
    });
    return sources;
}
}

class classic_legacy_commandscript : public CommandScript
{
public:
    classic_legacy_commandscript() : CommandScript("classic_legacy_commandscript") { }

    std::span<ChatCommandBuilder const> GetCommands() const override
    {
        static ChatCommandTable legacyCommandTable =
        {
            { "points", HandlePointsCommand, rbac::RBAC_PERM_COMMAND_ACHIEVEMENT_ADD, Console::No },
        };
        static ChatCommandTable commandTable =
        {
            { "legacy", legacyCommandTable },
        };
        return commandTable;
    }

    // .legacy points [count]: grant up to <count> more Legacy Points (default: all of them)
    static bool HandlePointsCommand(ChatHandler* handler, Optional<uint32> count)
    {
        Player* target = handler->getSelectedPlayerOrSelf();
        if (!target)
        {
            handler->SendSysMessage(LANG_NO_CHAR_SELECTED);
            handler->SetSentErrorMessage(true);
            return false;
        }

        uint32 wanted = count.value_or(std::numeric_limits<uint32>::max());
        int32 granted = 0;
        int32 total = 0;
        for (TraitCurrencySourceEntry const* source : GetLegacyPointSources())
        {
            AchievementEntry const* achievement = sAchievementStore.LookupEntry(source->AchievementID);
            if (!achievement)
                continue;

            if (!target->HasAchieved(achievement->ID))
            {
                if (uint32(granted) >= wanted)
                    continue;

                target->CompletedAchievement(achievement);
                if (!target->HasAchieved(achievement->ID))
                    continue;

                granted += source->Amount;
            }
            total += source->Amount;
        }

        target->UpdateClassicLegacyUnlock();
        handler->PSendSysMessage("Granted %d Legacy Points to %s (%d earned in total). Reopen the Legacy window to see them.",
            granted, target->GetName().c_str(), total);
        return true;
    }
};

void AddSC_classic_legacy_commands()
{
    new classic_legacy_commandscript();
}
