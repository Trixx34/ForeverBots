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

#include "ScriptMgr.h"
#include "BotAI.h"
#include "BotAlts.h"
#include "BotQuest.h"
#include "BotChat.h"
#include "BotCombat.h"
#include "BotMgr.h"
#include "BotQuestLog.h"
#include "Chat.h"
#include "CellImpl.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "DB2Stores.h"
#include "GridNotifiersImpl.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "PathGenerator.h"
#include "Player.h"
#include "QuestDef.h"
#include "WorldSession.h"
#include <chrono>
#include "ChatCommand.h"
#include "RBAC.h"
#include "StringFormat.h"

using namespace Trinity::ChatCommands;

namespace
{
// snapshot of the AI counters at the last `bot ai status`, for the per-window rate (zeroed by `bot ai reset`)
struct AiStatusSnapshot
{
    std::chrono::steady_clock::time_point Time = std::chrono::steady_clock::now();
    uint64 Ticks = 0, TickNs = 0;
};
AiStatusSnapshot _aiSnapshot;
}

// Console/GM commands of the player-bot subsystem (spawn, despawn, list; Phase 1). See docs/playerbots/implementation-plan.md.
// Uses the dedicated RBAC_PERM_COMMAND_BOT (granted to Gamemaster/Administrator by default,
// see sql/custom/auth/2026_10_05_00_auth_rbac_bot.sql).
class bot_commandscript : public CommandScript
{
public:
    bot_commandscript() : CommandScript("bot_commandscript") { }

    std::span<ChatCommandBuilder const> GetCommands() const override
    {
        static ChatCommandTable botAiCommandTable =
        {
            { "status", HandleBotAiStatusCommand, rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "reset",  HandleBotAiResetCommand,  rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "on",     HandleBotAiOnCommand,     rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "off",    HandleBotAiOffCommand,    rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "force",  HandleBotAiForceCommand,  rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
        };

        static ChatCommandTable botCommandTable =
        {
            { "hello",   HandleBotHelloCommand,   rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "spawn",   HandleBotSpawnCommand,   rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "despawn", HandleBotDespawnCommand, rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "list",    HandleBotListCommand,    rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "stats",   HandleBotStatsCommand,   rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "logtest", HandleBotLogTestCommand, rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "strategy", HandleBotStrategyCommand, rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "trace",   HandleBotTraceCommand,   rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "nudge",   HandleBotNudgeCommand,   rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "kill",    HandleBotKillCommand,    rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "quest",   HandleBotQuestCommand,   rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "goto",    HandleBotGotoCommand,    rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "follow",  HandleBotFollowCommand,  rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "stay",    HandleBotStayCommand,    rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "say",     HandleBotSayCommand,     rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "group",   HandleBotGroupCommand,   rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "hurt",    HandleBotHurtCommand,    rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "root",    HandleBotRootCommand,    rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "aggro",   HandleBotAggroCommand,   rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "envdmg",  HandleBotEnvDmgCommand,  rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "level",   HandleBotLevelCommand,   rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "tele",    HandleBotTeleCommand,    rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "state",   HandleBotStateCommand,   rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "path",    HandleBotPathCommand,    rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "spells",  HandleBotSpellsCommand,  rbac::RBAC_PERM_COMMAND_BOT, Console::Yes },
            { "alt",     HandleBotAltCommand,     rbac::RBAC_PERM_COMMAND_BOT_ALT, Console::Yes },
            { "ai",      botAiCommandTable },
        };

        static ChatCommandTable commandTable =
        {
            { "bot", botCommandTable },
        };

        return commandTable;
    }

    // bot alt add|remove|list [name] [test]: logs a character of the issuer's own account in/out as a bot (BotAlts.cpp).
    static bool HandleBotAltCommand(ChatHandler* handler, std::string op, Optional<std::string> name, Optional<std::string> test)
    {
        return BotAlts::HandleCommand(handler, op, name ? *name : std::string(), test && *test == "test");
    }

    static bool HandleBotHelloCommand(ChatHandler* handler)
    {
        handler->PSendSysMessage("%s", sBotMgr->GetStatus().c_str());
        return true;
    }

    // bot spawn <count> [class] [faction] [level]   class: warrior|paladin|hunter|rogue|priest|shaman|mage|warlock|druid|any,
    // faction: alliance|horde|any. Reuses existing bot characters first, creates the rest. Logins are spread over a few ticks.
    static bool HandleBotSpawnCommand(ChatHandler* handler, uint32 count, Optional<std::string> classArg, Optional<std::string> factionArg, Optional<uint8> level)
    {
        uint8 classId = 0;
        if (classArg && !StringEqualI(*classArg, "any"))
        {
            classId = BotMgr::ParseClass(*classArg);
            if (!classId)
            {
                handler->PSendSysMessage("Unknown class '%s' (warrior, paladin, hunter, rogue, priest, shaman, mage, warlock, druid or any).", classArg->c_str());
                { handler->SetSentErrorMessage(true); return false; }
            }
        }

        int8 faction = -1;
        if (factionArg && !StringEqualI(*factionArg, "any"))
        {
            faction = BotMgr::ParseFaction(*factionArg);
            if (faction < 0)
            {
                handler->PSendSysMessage("Unknown faction '%s' (alliance, horde or any).", factionArg->c_str());
                { handler->SetSentErrorMessage(true); return false; }
            }
        }

        if (count < 1 || count > 1000)
        {
            handler->PSendSysMessage("%s", "Count must be between 1 and 1000.");
            { handler->SetSentErrorMessage(true); return false; }
        }

        BotSpawnResult result = sBotMgr->SpawnBots(count, classId, faction, level);
        handler->PSendSysMessage("Bot spawn: %u requested, %u reused, %u created, %u failed%s%s", count, result.Reused, result.Created, result.Failed,
            result.Error.empty() ? "" : " - ", result.Error.c_str());
        for (std::string const& name : result.Names)
            handler->PSendSysMessage("  queued %s", name.c_str());

        return result.Error.empty() || !result.Names.empty();
    }

    // bot despawn [all|<name>]: saves and logs the bot(s) out.
    static bool HandleBotDespawnCommand(ChatHandler* handler, Optional<std::string> name)
    {
        if (name && BotAlts::DespawnAlt(handler, *name)) // an alt bot: owner only
            return true;
        uint32 count = sBotMgr->DespawnBots(name ? *name : std::string());
        handler->PSendSysMessage("Bot despawn: %u bots logged out or queued for logout.", count);
        return count > 0 || !name || StringEqualI(*name, "all");
    }

    // bot stats: counts per faction, class and race over all known bots (to check a batch is balanced).
    static bool HandleBotStatsCommand(ChatHandler* handler)
    {
        for (std::string const& line : sBotMgr->GetStats())
            handler->PSendSysMessage("%s", line.c_str());
        return true;
    }

    // bot list: one line per known bot character (online bots show their live position, offline ones the saved one).
    static bool HandleBotListCommand(ChatHandler* handler)
    {
        std::vector<BotInfo> bots = sBotMgr->ListBots();
        uint32 online = 0;
        for (BotInfo const& bot : bots)
        {
            if (bot.State == BOT_ONLINE)
                ++online;

            handler->PSendSysMessage("%llu %s %s race %u %s L%u %s map %u zone %u (%.1f, %.1f, %.1f)", (unsigned long long)bot.Guid, bot.Name.c_str(),
                BotMgr::ClassName(bot.Class), uint32(bot.Race), bot.Horde ? "horde" : "alliance", uint32(bot.Level), BotMgr::StateName(bot.State),
                uint32(bot.MapId), uint32(bot.ZoneId), bot.X, bot.Y, bot.Z);
        }

        handler->PSendSysMessage("Bots: %u known, %u online.", uint32(bots.size()), online);
        return true;
    }

    // ---- Phase 2: engine control (world thread, runs after the map updates finished, see docs/playerbots/engine-design.md) ----

    static Player* FindOneBot(ChatHandler* handler, std::string const& name)
    {
        std::vector<Player*> players = sBotMgr->GetOnlineBotPlayers(name);
        if (name.empty() || StringEqualI(name, "all") || players.size() != 1)
        {
            handler->PSendSysMessage("Bot '%s' is not online (give the exact bot name).", name.c_str());
            return nullptr;
        }
        return players.front();
    }

    // bot spells <name>: known spells and what the combat strategy resolved (BotCombat.cpp)
    static bool HandleBotSpellsCommand(ChatHandler* handler, std::string name)
    {
        Player* bot = FindOneBot(handler, name);
        if (!bot)
            { handler->SetSentErrorMessage(true); return false; }
        for (std::string const& line : BotCombatDescribeSpells(bot))
            handler->PSendSysMessage("%s", line.c_str());
        return true;
    }

    // bot strategy <name|all> [+name|-name[,+name...]]: no change argument lists the strategies per engine.
    static bool HandleBotStrategyCommand(ChatHandler* handler, std::string name, Optional<std::string> change)
    {
        std::vector<Player*> players = sBotMgr->GetOnlineBotPlayers(name);
        if (players.empty())
        {
            handler->PSendSysMessage("No online bot matches '%s'.", name.c_str());
            { handler->SetSentErrorMessage(true); return false; }
        }

        if (!change)
        {
            if (players.size() == 1)
                handler->PSendSysMessage("Strategies of %s:", players.front()->GetName().c_str());
            else
                handler->PSendSysMessage("%s", "Strategies of the first bot (use a name for another):");
            if (BotAI* first = players.front()->GetSession()->GetBotAI())
                for (std::string const& line : first->DescribeStrategies())
                    handler->PSendSysMessage("  %s", line.c_str());

            std::string known;
            for (std::string const& n : BotRegistry::instance().StrategyNames())
                known += " " + n;
            handler->PSendSysMessage("Known strategies:%s", known.c_str());
            return true;
        }

        uint32 changed = 0;
        std::string const rest = *change;
        size_t pos = 0;
        while (pos < rest.size())
        {
            size_t const end = rest.find(',', pos);
            std::string token = rest.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
            pos = end == std::string::npos ? rest.size() : end + 1;
            if (token.size() < 2 || (token[0] != '+' && token[0] != '-'))
            {
                handler->PSendSysMessage("Bad change '%s' (use +name or -name).", token.c_str());
                { handler->SetSentErrorMessage(true); return false; }
            }

            std::string strategy = token.substr(1);
            if (!BotRegistry::instance().FindStrategy(strategy))
            {
                handler->PSendSysMessage("Unknown strategy '%s'.", strategy.c_str());
                { handler->SetSentErrorMessage(true); return false; }
            }

            for (Player* player : players)
            {
                BotAI* ai = player->GetSession()->GetBotAI();
                if (ai && (token[0] == '+' ? ai->AddStrategy(player, strategy) : ai->RemoveStrategy(player, strategy)))
                    ++changed;
            }
        }
        handler->PSendSysMessage("Strategy change applied to %u engine set(s) over %u bot(s).", changed, uint32(players.size()));
        return true;
    }

    // bot trace <name|all> on|off: verbose decision logging (event_type 'trace', plus quiet actions and every quest progress step).
    static bool HandleBotTraceCommand(ChatHandler* handler, std::string name, std::string onOff)
    {
        bool const on = StringEqualI(onOff, "on");
        if (!on && !StringEqualI(onOff, "off"))
        {
            handler->PSendSysMessage("%s", "Use: bot trace <name|all> on|off");
            { handler->SetSentErrorMessage(true); return false; }
        }

        bool const all = StringEqualI(name, "all");
        if (all)
            BotAI::SetTraceAll(on);

        uint32 count = 0;
        for (Player* player : sBotMgr->GetOnlineBotPlayers(all ? std::string() : name))
            if (BotAI* ai = player->GetSession()->GetBotAI())
            {
                ai->SetTrace(on);
                ++count;
            }

        handler->PSendSysMessage("Trace %s for %u bot(s)%s.", on ? "on" : "off", count, all ? " (and bots spawned later)" : "");
        return all || count > 0;
    }

    // bot kill <name>: kills an online bot (test aid, no killer). Use `revive <name>` to bring it back.
    static bool HandleBotKillCommand(ChatHandler* handler, std::string name)
    {
        Player* player = FindOneBot(handler, name);
        if (!player)
            { handler->SetSentErrorMessage(true); return false; }

        if (BotAI* ai = player->GetSession()->GetBotAI())
            ai->NoteTestCommand("bot kill");
        if (player->IsAlive())
            player->KillSelf();
        handler->PSendSysMessage("Bot %s killed.", player->GetName().c_str());
        return true;
    }

    static bool HandleBotAiOnCommand(ChatHandler* handler)
    {
        BotAI::SetEnabled(true);
        handler->PSendSysMessage("%s", "Bot AI ticking is on.");
        return true;
    }

    static bool HandleBotAiOffCommand(ChatHandler* handler)
    {
        BotAI::SetEnabled(false);
        handler->PSendSysMessage("%s", "Bot AI ticking is off (bots keep their AI objects, nothing runs).");
        return true;
    }

    static bool HandleBotAiResetCommand(ChatHandler* handler)
    {
        BotAIStats& st = BotAI::Stats();
        st.Ticks = 0; st.TickNs = 0; st.MaxTickNs = 0; st.LogNs = 0; st.ActionsRun = 0; st.StateChanges = 0; st.Events = 0;
        st.HookNs = 0; st.HookCalls = 0; st.Fights = 0; st.Deaths = 0; st.DeathNs = 0;
        _aiSnapshot = AiStatusSnapshot();
        handler->PSendSysMessage("%s", "Bot AI counters reset.");
        return true;
    }

    // bot ai force <name|all> <noncombat|combat|dead|auto>: pins the ENGINE choice (the game state is untouched), to exercise an engine in tests.
    static bool HandleBotAiForceCommand(ChatHandler* handler, std::string name, std::string state)
    {
        std::optional<BotState> forced;
        if (StringEqualI(state, "noncombat")) forced = BotState::NonCombat;
        else if (StringEqualI(state, "combat")) forced = BotState::Combat;
        else if (StringEqualI(state, "dead")) forced = BotState::Dead;
        else if (!StringEqualI(state, "auto"))
        {
            handler->PSendSysMessage("%s", "Use: bot ai force <name|all> noncombat|combat|dead|auto");
            { handler->SetSentErrorMessage(true); return false; }
        }

        uint32 count = 0;
        for (Player* player : sBotMgr->GetOnlineBotPlayers(name))
            if (BotAI* ai = player->GetSession()->GetBotAI())
            {
                ai->SetForcedState(forced);
                ++count;
            }
        handler->PSendSysMessage("Engine %s for %u bot(s).", forced ? state.c_str() : "auto", count);
        return count > 0;
    }

    // bot nudge <name|all> <yards> [degrees]: test aid for the position telemetry. Moves the bot(s) straight in the given
    // direction (default: along their facing; 0 = north) without a teleport handshake, so the AI keeps ticking.
    static bool HandleBotNudgeCommand(ChatHandler* handler, std::string name, float yards, Optional<float> degrees)
    {
        std::vector<Player*> players = sBotMgr->GetOnlineBotPlayers(name);
        if (players.empty())
        {
            handler->PSendSysMessage("No online bot matches '%s'.", name.c_str());
            { handler->SetSentErrorMessage(true); return false; }
        }
        uint32 moved = 0, skipped = 0;
        for (Player* player : players)
        {
            float const angle = degrees ? float(*degrees) * float(M_PI) / 180.0f : player->GetOrientation();
            float const x = player->GetPositionX() + yards * std::cos(angle);
            float const y = player->GetPositionY() + yards * std::sin(angle);
            float z = player->GetPositionZ();
            player->UpdateGroundPositionZ(x, y, z);
            // a straight-line move must not leave the bot off the navmesh (every later goto would fail with NO_PATH): only move
            // when the destination is a complete walkable path away
            PathGenerator probe(player);
            probe.CalculatePath(x, y, z, false);
            if (probe.GetPathType() != PATHFIND_NORMAL)
            {
                ++skipped;
                continue;
            }
            player->UpdatePosition(x, y, z, player->GetOrientation(), true);
            ++moved;
        }
        handler->PSendSysMessage("Moved %u bot(s) by %.1f yards, %u skipped (destination not on the navmesh).", moved, yards, skipped);
        return moved > 0;
    }

    // ---- Phase 3 test aids and control (world thread) ----

    // bot goto <name> <x> <y> <z> [arriveYards]: sets a walking goal on the bot's current map (adds the goto strategy if needed).
    static bool HandleBotGotoCommand(ChatHandler* handler, std::string name, float x, float y, float z, Optional<float> arrive)
    {
        Player* player = FindOneBot(handler, name);
        if (!player)
            { handler->SetSentErrorMessage(true); return false; }
        BotAI* ai = player->GetSession()->GetBotAI();
        if (!ai)
            { handler->SetSentErrorMessage(true); return false; }
        ai->AddStrategy(player, "goto", "command");
        BotMotion::EnsureGrids(player, x, y);
        player->UpdateGroundPositionZ(x, y, z); // the z argument is only a hint, the goal is put on the ground
        ai->Motion().SetGoal(player->GetMapId(), x, y, z, arrive.value_or(3.0f), "goto");
        handler->PSendSysMessage("%s will walk to (%.1f, %.1f, %.1f) on map %u (%.1f yards away).", player->GetName().c_str(), x, y, z, player->GetMapId(),
            player->GetExactDist2d(x, y));
        return true;
    }

    // bot follow <name> <leaderName|off>: follows a player or bot on the same map.
    static bool HandleBotFollowCommand(ChatHandler* handler, std::string name, std::string leaderName)
    {
        Player* player = FindOneBot(handler, name);
        if (!player)
            { handler->SetSentErrorMessage(true); return false; }
        BotAI* ai = player->GetSession()->GetBotAI();
        if (!ai)
            { handler->SetSentErrorMessage(true); return false; }
        if (StringEqualI(leaderName, "off"))
        {
            ai->Motion().SetFollow(ObjectGuid::Empty);
            if (ai->Motion().HasGoal() && !strcmp(ai->Motion().GetTag(), "follow"))
                ai->Motion().ClearGoal();
            handler->PSendSysMessage("%s stops following.", player->GetName().c_str());
            return true;
        }
        Player* leader = ObjectAccessor::FindPlayerByName(leaderName);
        if (!leader)
        {
            handler->PSendSysMessage("Player '%s' is not online.", leaderName.c_str());
            { handler->SetSentErrorMessage(true); return false; }
        }
        ai->AddStrategy(player, "follow", "command");
        ai->Motion().SetFollow(leader->GetGUID());
        handler->PSendSysMessage("%s follows %s.", player->GetName().c_str(), leader->GetName().c_str());
        return true;
    }

    // bot say <issuer> <party|raid|whisper> <text>: feeds the text to the bot chat commands as if the issuer had typed it
    // (whisper: the text starts with the bot name). Test path for scripted raids without a client.
    static bool HandleBotSayCommand(ChatHandler* handler, std::string issuer, std::string channel, Trinity::ChatCommands::Tail text)
    {
        return BotChat::TestSay(handler, issuer, channel, std::string_view(text));
    }

    // bot group form|move|list|disband|disbandall ...: builds and tears down bot-only test groups and raids.
    static bool HandleBotGroupCommand(ChatHandler* handler, std::string op, Trinity::ChatCommands::Tail args)
    {
        return BotChat::TestGroup(handler, op, std::string_view(args));
    }

    // bot stay <name|all> on|off: toggles the stay strategy (hold position).
    static bool HandleBotStayCommand(ChatHandler* handler, std::string name, std::string onOff)
    {
        bool const on = StringEqualI(onOff, "on");
        if (!on && !StringEqualI(onOff, "off"))
        {
            handler->PSendSysMessage("%s", "Use: bot stay <name|all> on|off");
            { handler->SetSentErrorMessage(true); return false; }
        }
        uint32 count = 0;
        for (Player* player : sBotMgr->GetOnlineBotPlayers(name))
            if (BotAI* ai = player->GetSession()->GetBotAI())
                if (on ? ai->AddStrategy(player, "stay", "command") : ai->RemoveStrategy(player, "stay", "command"))
                    ++count;
        handler->PSendSysMessage("Stay %s for %u bot(s).", on ? "on" : "off", count);
        return true;
    }

    // bot hurt <name> <hp%> [mana%]: test aid, sets health (and mana) to a percentage.
    static bool HandleBotHurtCommand(ChatHandler* handler, std::string name, float hpPct, Optional<float> manaPct)
    {
        Player* player = FindOneBot(handler, name);
        if (!player || !player->IsAlive())
            { handler->SetSentErrorMessage(true); return false; }
        if (BotAI* ai = player->GetSession()->GetBotAI())
            ai->NoteTestCommand("bot hurt");
        player->SetHealth(std::max<uint32>(1, uint32(player->GetMaxHealth() * std::clamp(hpPct, 0.0f, 100.0f) / 100.0f)));
        if (manaPct && player->GetMaxPower(POWER_MANA) > 0)
            player->SetPower(POWER_MANA, uint32(player->GetMaxPower(POWER_MANA) * std::clamp(*manaPct, 0.0f, 100.0f) / 100.0f));
        handler->PSendSysMessage("%s: health %u/%u, mana %u/%u.", player->GetName().c_str(), uint32(player->GetHealth()), uint32(player->GetMaxHealth()),
            player->GetPower(POWER_MANA), player->GetMaxPower(POWER_MANA));
        return true;
    }

    // bot aggro <name> [radius]: test aid, makes hostile creatures within radius (default 30) attack the bot (combat/death telemetry tests).
    static bool HandleBotAggroCommand(ChatHandler* handler, std::string name, Optional<float> radius)
    {
        Player* player = FindOneBot(handler, name);
        if (!player || !player->IsAlive())
            { handler->SetSentErrorMessage(true); return false; }
        if (BotAI* ai = player->GetSession()->GetBotAI())
            ai->NoteTestCommand("bot aggro");
        std::list<Creature*> creatures;
        Trinity::AnyUnfriendlyUnitInObjectRangeCheck check(player, player, radius ? *radius : 30.0f);
        std::list<Unit*> units;
        Trinity::UnitListSearcher<Trinity::AnyUnfriendlyUnitInObjectRangeCheck> searcher(player, units, check);
        Cell::VisitAllObjects(player, searcher, radius ? *radius : 30.0f);
        uint32 count = 0;
        for (Unit* unit : units)
            if (Creature* creature = unit->ToCreature())
                if (creature->IsAlive() && creature->AI())
                {
                    creature->EngageWithTarget(player);
                    creature->AI()->AttackStart(player);
                    ++count;
                }
        handler->PSendSysMessage("%u creature(s) now attack %s.", count, player->GetName().c_str());
        return true;   // false would print the command usage text even though the command ran
    }

    // bot envdmg <name> <type 0-6> <amount>: test aid, environmental damage (0 fatigue, 1 drowning, 2 fall, 3 lava, 4 slime, 5 fire).
    static bool HandleBotEnvDmgCommand(ChatHandler* handler, std::string name, uint8 type, uint32 amount)
    {
        Player* player = FindOneBot(handler, name);
        if (!player || !player->IsAlive() || type > DAMAGE_FALL_TO_VOID)
            { handler->SetSentErrorMessage(true); return false; }
        if (BotAI* ai = player->GetSession()->GetBotAI())
            ai->NoteTestCommand("bot envdmg");
        player->EnvironmentalDamage(EnviromentalDamage(type), amount);
        handler->PSendSysMessage("%s took %u environmental damage (type %u), health %u/%u.", player->GetName().c_str(), amount, uint32(type), uint32(player->GetHealth()), uint32(player->GetMaxHealth()));
        return true;
    }

    // bot root <name> on|off: test aid for the stuck detection.
    static bool HandleBotRootCommand(ChatHandler* handler, std::string name, std::string onOff)
    {
        Player* player = FindOneBot(handler, name);
        if (!player)
            { handler->SetSentErrorMessage(true); return false; }
        bool const on = StringEqualI(onOff, "on");
        player->SetControlled(on, UNIT_STATE_ROOT);
        handler->PSendSysMessage("%s is %s.", player->GetName().c_str(), on ? "rooted" : "free to move");
        return true;
    }

    // bot level <name> <level>: test aid (resurrection sickness starts at level 11).
    static bool HandleBotLevelCommand(ChatHandler* handler, std::string name, uint8 level)
    {
        Player* player = FindOneBot(handler, name);
        if (!player || level < 1 || level > 60)
            { handler->SetSentErrorMessage(true); return false; }
        player->GiveLevel(level);
        player->InitTalentForLevel();
        player->SetXP(0);
        handler->PSendSysMessage("%s is now level %u.", player->GetName().c_str(), uint32(player->GetLevel()));
        return true;
    }

    // bot tele <name> <map> <x> <y> <z>: test aid, teleports a bot (the teleport handshake is completed by the BotMgr).
    // A destination within 500 yd of the start point of a race of the other faction is refused (hostile guards kill the bot,
    // see the Northshire deaths of 2026-10-06) unless the optional last argument is "force".
    static bool HandleBotTeleCommand(ChatHandler* handler, std::string name, uint32 mapId, float x, float y, float z, Optional<std::string> force)
    {
        Player* player = FindOneBot(handler, name);
        if (!player)
            { handler->SetSentErrorMessage(true); return false; }
        if (!(force && StringEqualI(*force, "force")))
        {
            for (ChrRacesEntry const* race : sChrRacesStore)
            {
                if (Player::TeamIdForRace(race->ID) == player->GetTeamId())
                    continue;
                PlayerInfo const* info = sObjectMgr->GetPlayerInfo(race->ID, player->GetClass());
                if (!info || info->createPosition.Loc.GetMapId() != mapId || info->createPosition.Loc.GetExactDist2d(x, y) > 500.0f)
                    continue;
                BotEvent event;
                event.BotGuid = player->GetGUID().GetCounter();
                event.Type = "decision";
                event.Severity = BOTLOG_WARN;
                event.Reason = "TELE_REFUSED_FACTION";
                event.Summary = Trinity::StringFormat("bot tele refused: destination is in the start zone of race {} (other faction)", uint32(race->ID));
                event.MapId = uint16(mapId);
                event.Details = Trinity::StringFormat(R"({{"source":"test_command","dest":{{"map":{},"x":{:.1f},"y":{:.1f},"z":{:.1f}}},"enemy_race":{}}})", mapId, x, y, z, uint32(race->ID));
                sBotMgr->LogEvent(std::move(event));
                handler->PSendSysMessage("%s: teleport refused, the destination is in the start zone of an enemy race (%u); add 'force' as the last argument to override.", player->GetName().c_str(), uint32(race->ID));
                { handler->SetSentErrorMessage(true); return false; }
            }
        }
        bool const ok = player->TeleportTo(mapId, x, y, z, player->GetOrientation());
        handler->PSendSysMessage("%s teleport to map %u (%.1f, %.1f, %.1f): %s", player->GetName().c_str(), mapId, x, y, z, ok ? "started" : "refused");
        return ok;
    }

    // bot path <name> <x> <y> <z>: test aid, runs the pathfinding query a goto would run (nothing moves) and prints the classification.
    static bool HandleBotPathCommand(ChatHandler* handler, std::string name, float x, float y, float z)
    {
        Player* player = FindOneBot(handler, name);
        if (!player)
            { handler->SetSentErrorMessage(true); return false; }
        BotMotion::EnsureGrids(player, x, y);
        player->UpdateGroundPositionZ(x, y, z);
        BotPathInfo const pi = BotMotion::QueryPath(player, x, y, z);
        handler->PSendSysMessage("%s -> (%.1f, %.1f, %.1f): type 0x%02X valid %d nopath %d mmap_missing %d off_navmesh %d partial %d length %.0f end_gap %.0f (straight %.0f)", player->GetName().c_str(),
            x, y, z, pi.Type, pi.Valid ? 1 : 0, pi.NoPath ? 1 : 0, pi.MmapMissing ? 1 : 0, pi.OffMesh ? 1 : 0, pi.Partial ? 1 : 0, pi.Length, pi.EndGap, player->GetExactDist2d(x, y));
        return true;
    }

    // bot state <name>: one line with the live state (life, ghost, corpse, goal, rest, engine).
    static bool HandleBotStateCommand(ChatHandler* handler, std::string name)
    {
        Player* player = FindOneBot(handler, name);
        if (!player)
            { handler->SetSentErrorMessage(true); return false; }
        BotAI* ai = player->GetSession()->GetBotAI();
        std::string extra;
        if (ai)
        {
            extra = Trinity::StringFormat(" engine {} goal {} rest {} plan {}({})", BotStateName(ai->GetState()), ai->Motion().HasGoal() ? ai->Motion().GetTag() : "none",
                uint32(ai->Rest().Bits), uint32(ai->Recover().Plan), ai->Recover().PlanReason) + " | " + BotQuest::DescribeTask(ai);
        }
        WorldLocation const& corpse = player->GetCorpseLocation();
        handler->PSendSysMessage("%s L%u %s%s map %u (%.1f, %.1f, %.1f) hp %u/%u mana %u/%u moving %d corpse %s (map %u, %.1f, %.1f)%s", player->GetName().c_str(), uint32(player->GetLevel()),
            player->IsAlive() ? "alive" : "dead", player->HasPlayerFlag(PLAYER_FLAGS_GHOST) ? " ghost" : "", player->GetMapId(), player->GetPositionX(), player->GetPositionY(),
            player->GetPositionZ(), uint32(player->GetHealth()), uint32(player->GetMaxHealth()), player->GetPower(POWER_MANA), player->GetMaxPower(POWER_MANA),
            player->isMoving() ? 1 : 0, player->HasCorpse() ? "yes" : "no", corpse.GetMapId(), corpse.GetPositionX(), corpse.GetPositionY(), extra.c_str());
        return true;
    }

    // bot ai status: configuration, bots per engine, tick counters and cost.
    static bool HandleBotAiStatusCommand(ChatHandler* handler)
    {
        using Clock = std::chrono::steady_clock;
        Clock::time_point& lastTime = _aiSnapshot.Time;
        uint64& lastTicks = _aiSnapshot.Ticks;
        uint64& lastTickNs = _aiSnapshot.TickNs;

        BotAIConfig const& cfg = BotAI::Config();
        uint32 counts[BOT_STATE_COUNT] = { };
        uint32 withAi = 0, forced = 0, traced = 0;
        std::vector<Player*> players = sBotMgr->GetOnlineBotPlayers();
        for (Player* player : players)
            if (BotAI* ai = player->GetSession()->GetBotAI())
            {
                ++withAi;
                ++counts[uint32(ai->GetState())];
                if (ai->GetForcedState()) ++forced;
                if (ai->IsTrace()) ++traced;
            }

        BotAIStats& st = BotAI::Stats();
        uint64 const ticks = st.Ticks, tickNs = st.TickNs;
        auto const now = Clock::now();
        double const window = std::chrono::duration<double>(now - lastTime).count();
        double const rate = ticks >= lastTicks && window > 0.0 ? double(ticks - lastTicks) / window : 0.0;
        double const winAvgUs = ticks > lastTicks ? double(tickNs - lastTickNs) / double(ticks - lastTicks) / 1000.0 : 0.0;
        lastTime = now; lastTicks = ticks; lastTickNs = tickNs;

        handler->PSendSysMessage("Bot AI: %s, tick %u ms, test strategy %s, trace-all %s", BotAI::IsEnabled() ? "ON" : "OFF", cfg.TickMs,
            cfg.TestStrategy ? "on" : "off", BotAI::GetTraceAll() ? "on" : "off");
        handler->PSendSysMessage("Bots online %u (with AI %u): noncombat %u, combat %u, dead %u; forced %u, traced %u", uint32(players.size()), withAi,
            counts[0], counts[1], counts[2], forced, traced);
        handler->PSendSysMessage("Ticks %llu total, %.1f/s since the last status call (window %.1fs)", (unsigned long long)ticks, rate, window);
        handler->PSendSysMessage("Tick cost: avg %.2f us (window %.2f us), max %.1f us, total %.1f ms, of which logging %.1f ms",
            ticks ? double(tickNs) / double(ticks) / 1000.0 : 0.0, winAvgUs, double(st.MaxTickNs) / 1000.0, double(tickNs) / 1.0e6, double(st.LogNs) / 1.0e6);
        handler->PSendSysMessage("Position telemetry: every %u s, %llu samples queued, %.1f ms total (included in the tick cost)",
            sBotMgr->GetPosIntervalMs() / 1000, (unsigned long long)st.PosSamples, double(st.PosNs) / 1.0e6);
        handler->PSendSysMessage("Actions run %llu, state changes %llu, engine log events %llu", (unsigned long long)st.ActionsRun, (unsigned long long)st.StateChanges,
            (unsigned long long)st.Events);
        uint64 const hookCalls = st.HookCalls, deaths = st.Deaths;
        handler->PSendSysMessage("Combat hooks (outside the tick): %llu calls, avg %.2f us, total %.1f ms; fights %llu; death snapshots %llu, avg %.1f us",
            (unsigned long long)hookCalls, hookCalls ? double(st.HookNs) / double(hookCalls) / 1000.0 : 0.0, double(st.HookNs) / 1.0e6,
            (unsigned long long)st.Fights, (unsigned long long)deaths, deaths ? double(st.DeathNs) / double(deaths) / 1000.0 : 0.0);
        return true;
    }

    // bot quest add|complete|reward|abandon|fail <bot> <questId> [choiceItemId]: test aids that use the normal Player quest APIs, so the
    // quest hooks log exactly what a real quest flow would.
    static bool HandleBotQuestCommand(ChatHandler* handler, std::string action, std::string name, uint32 questId, Optional<uint32> choice)
    {
        Player* player = FindOneBot(handler, name);
        if (!player)
            { handler->SetSentErrorMessage(true); return false; }

        BotQuestLog::Scope testScope("test_command", true); // every quest event below is tagged source=test_command
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
        {
            handler->PSendSysMessage("Quest %u does not exist.", questId);
            { handler->SetSentErrorMessage(true); return false; }
        }

        if (StringEqualI(action, "add"))
        {
            if (!player->CanTakeQuest(quest, false))
            {
                std::string failed;
                auto check = [&](bool ok, char const* what) { if (!ok) failed += std::string(" ") + what; };
                check(player->SatisfyQuestStatus(quest, false), "status");
                check(player->SatisfyQuestExclusiveGroup(quest, false), "exclusive_group");
                check(player->SatisfyQuestClass(quest, false), "class");
                check(player->SatisfyQuestRace(quest, false), "race");
                check(player->SatisfyQuestMinLevel(quest, false), "min_level");
                check(player->SatisfyQuestMaxLevel(quest, false), "max_level");
                check(player->SatisfyQuestSkill(quest, false), "skill");
                check(player->SatisfyQuestReputation(quest, false), "reputation");
                check(player->SatisfyQuestDependentQuests(quest, false), "dependent_quests");
                check(player->SatisfyQuestTimed(quest, false), "timed");
                check(player->SatisfyQuestConditions(quest, false), "conditions");
                handler->PSendSysMessage("%s cannot take quest %u, failed checks:%s (unlisted: disabled, day/week/month/seasonal, expansion)", player->GetName().c_str(), questId,
                    failed.empty() ? " none of the listed" : failed.c_str());
                { handler->SetSentErrorMessage(true); return false; }
            }
            if (!player->CanAddQuest(quest, false))
            {
                handler->PSendSysMessage("%s cannot add quest %u (log full or no room for the source item).", player->GetName().c_str(), questId);
                { handler->SetSentErrorMessage(true); return false; }
            }
            player->AddQuestAndCheckCompletion(quest, nullptr);
        }
        else if (StringEqualI(action, "complete"))
        {
            if (player->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE)
            {
                handler->PSendSysMessage("Quest %u is not in progress for %s.", questId, player->GetName().c_str());
                { handler->SetSentErrorMessage(true); return false; }
            }
            for (QuestObjective const& obj : quest->GetObjectives())
            {
                if (obj.Type == QUEST_OBJECTIVE_MONSTER)
                    for (int32 i = 0; i < obj.Amount; ++i)
                        player->KilledMonsterCredit(obj.ObjectID);
                else
                    player->SetQuestObjectiveData(obj, obj.Amount);
            }
            if (player->CanCompleteQuest(questId))
                player->CompleteQuest(questId);
        }
        else if (StringEqualI(action, "reward"))
        {
            uint32 const rewardId = choice ? *choice : 0;
            if (player->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE || !player->CanRewardQuest(quest, false) || !player->CanRewardQuest(quest, LootItemType::Item, rewardId, false))
            {
                handler->PSendSysMessage("Quest %u cannot be rewarded to %s now (not complete, reward choice missing, or no bag room).", questId, player->GetName().c_str());
                { handler->SetSentErrorMessage(true); return false; }
            }
            player->RewardQuest(quest, LootItemType::Item, rewardId, nullptr, true);
        }
        else if (StringEqualI(action, "abandon"))
        {
            if (player->GetQuestStatus(questId) == QUEST_STATUS_NONE || player->GetQuestStatus(questId) == QUEST_STATUS_REWARDED)
            {
                handler->PSendSysMessage("Quest %u is not in the log of %s.", questId, player->GetName().c_str());
                { handler->SetSentErrorMessage(true); return false; }
            }
            player->TakeQuestSourceItem(questId, true);
            player->RemoveActiveQuest(questId);
            player->AbandonQuest(questId);
        }
        else if (StringEqualI(action, "fail"))
            player->FailQuest(questId);
        else
        {
            handler->PSendSysMessage("%s", "Use: bot quest add|complete|reward|abandon|fail <bot> <questId> [choiceItemId]");
            { handler->SetSentErrorMessage(true); return false; }
        }

        handler->PSendSysMessage("Quest %u %s: status is now %u for %s.", questId, action.c_str(), uint32(player->GetQuestStatus(questId)), player->GetName().c_str());
        return true;
    }

    // Writes one test row (bot_guid 0, event_type 'test') to the bot log and flushes, to verify the pipeline end to end.
    static bool HandleBotLogTestCommand(ChatHandler* handler)
    {
        if (!sBotMgr->IsLogDatabaseAvailable())
        {
            handler->PSendSysMessage("%s", "Bot log is off: set BotLogDatabaseInfo in worldserver.conf and restart.");
            return true;
        }

        BotEvent event;
        event.Type = "test";
        event.Severity = BOTLOG_INFO;
        event.Reason = "LOGTEST";
        event.Summary = "bot log test row from .bot logtest";
        event.Details = R"({"source":"cs_bot","note":"safe to delete"})";
        sBotMgr->LogEvent(std::move(event));
        sBotMgr->FlushLog();

        handler->PSendSysMessage("%s", "Bot log test event queued and flushed (check bot_event where event_type = 'test').");
        return true;
    }
};

void AddSC_bot_commandscript()
{
    new bot_commandscript();
}
