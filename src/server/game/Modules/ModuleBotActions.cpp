#include "ModuleBotActions.h"
#include "Creature.h"
#include "DB2Structure.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

using forever_module::ActionType;
using forever_module::BotAction;
using forever_module::BotActionStatus;

namespace
{

// Resolves a unit reference (forever_module::MakeUnitRef) on the bot's map. Players are found anywhere online.
// Creatures are found only on the bot's map, since a creature guid is meaningless elsewhere.
Unit* ResolveUnit(Player& bot, uint64 ref)
{
    switch (static_cast<HighGuid>(forever_module::UnitRefType(ref)))
    {
        case HighGuid::Player:
            return ObjectAccessor::FindPlayerByLowGUID(forever_module::UnitRefCounter(ref));

        case HighGuid::Creature:
        {
            Map* map = bot.GetMap();
            if (!map)
                return nullptr;
            ObjectGuid const guid = ObjectGuid::Create<HighGuid::Creature>(uint16(map->GetId()),
                                                                          forever_module::UnitRefEntry(ref),
                                                                          forever_module::UnitRefCounter(ref));
            return map->GetCreature(guid);
        }

        default:
            return nullptr;
    }
}

// A target the bot may attack: alive, in the world, and hostile to the bot.
Unit* ResolveHostileTarget(Player& bot, uint64 ref, std::string& reason)
{
    Unit* target = ResolveUnit(bot, ref);
    if (!target || target == &bot || !target->IsInWorld() || !target->IsAlive())
    {
        reason = "target is not a live unit on this bot's map";
        return nullptr;
    }
    if (!bot.IsHostileTo(target))
    {
        reason = "target is not hostile to the bot";
        return nullptr;
    }
    return target;
}

// Maps a cast result to the status a module sees in the next snapshot.
BotActionStatus StatusForCast(SpellCastResult result)
{
    switch (result)
    {
        case SPELL_CAST_OK:
            return BotActionStatus::kApplied;
        case SPELL_FAILED_OUT_OF_RANGE:
            return BotActionStatus::kOutOfRange;
        case SPELL_FAILED_NOT_READY:
            return BotActionStatus::kOnCooldown;
        case SPELL_FAILED_NO_POWER:
            return BotActionStatus::kNotEnoughPower;
        case SPELL_FAILED_LINE_OF_SIGHT:
            return BotActionStatus::kNoLineOfSight;
        case SPELL_FAILED_BAD_TARGETS:
        case SPELL_FAILED_BAD_IMPLICIT_TARGETS:
            return BotActionStatus::kInvalidTarget;
        case SPELL_FAILED_NOT_KNOWN:
            return BotActionStatus::kNotKnown;
        default:
            return BotActionStatus::kRefused;
    }
}

} // namespace

BotActionStatus ApplyBotAction(Player& bot, BotAction const& action, std::string& reason)
{
    switch (action.type)
    {
        case ActionType::kStop:
            bot.StopMoving();
            bot.GetMotionMaster()->Clear();
            bot.GetMotionMaster()->MoveIdle();
            return BotActionStatus::kApplied;

        case ActionType::kMoveTo:
            bot.GetMotionMaster()->Clear();
            bot.GetMotionMaster()->MovePoint(0, action.dest_x, action.dest_y, action.dest_z);
            return BotActionStatus::kApplied;

        case ActionType::kFollow:
        {
            Unit* target = ResolveUnit(bot, action.target_guid);
            if (!target || target == &bot || !target->IsInWorld())
            {
                reason = "follow target is not a unit on this bot's map";
                return BotActionStatus::kInvalidTarget;
            }
            bot.GetMotionMaster()->Clear();
            bot.GetMotionMaster()->MoveFollow(target, 1.0f);
            return BotActionStatus::kApplied;
        }

        case ActionType::kAttackTarget:
        {
            Unit* target = ResolveHostileTarget(bot, action.target_guid, reason);
            if (!target)
                return BotActionStatus::kInvalidTarget;
            if (!bot.Attack(target, true))
            {
                reason = "attack was refused";
                return BotActionStatus::kRefused;
            }
            return BotActionStatus::kApplied;
        }

        case ActionType::kCastSpell:
        {
            if (!sSpellMgr->GetSpellInfo(action.spell_id, DIFFICULTY_NONE))
            {
                reason = "unknown spell " + std::to_string(action.spell_id);
                return BotActionStatus::kUnknownSpell;
            }
            if (!bot.HasSpell(action.spell_id))
            {
                reason = "bot does not know spell " + std::to_string(action.spell_id);
                return BotActionStatus::kNotKnown;
            }
            // A zero target means the bot casts on itself.
            Unit* target = action.target_guid == 0 ? &bot : ResolveUnit(bot, action.target_guid);
            if (!target || !target->IsInWorld())
            {
                reason = "cast target is not a unit on this bot's map";
                return BotActionStatus::kInvalidTarget;
            }
            SpellCastResult const result = bot.CastSpell(target, action.spell_id, false);
            if (result != SPELL_CAST_OK)
                reason = "cast of spell " + std::to_string(action.spell_id) + " returned result " + std::to_string(int32(result));
            return StatusForCast(result);
        }
    }

    reason = "unknown action type";
    return BotActionStatus::kRefused;
}
