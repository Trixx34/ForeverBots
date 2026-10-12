#include "ModuleBotActions.h"
#include "DB2Structure.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

using forever_module::ActionType;
using forever_module::BotAction;

namespace
{

// Target guids on the wire are player counters for now (see ModuleBotActions.h).
Player* ResolvePlayerTarget(uint64 counter)
{
    return ObjectAccessor::FindPlayerByLowGUID(counter);
}

} // namespace

bool ApplyBotAction(Player& bot, BotAction const& action, std::string& reason)
{
    switch (action.type)
    {
        case ActionType::kStop:
            bot.StopMoving();
            bot.GetMotionMaster()->Clear();
            bot.GetMotionMaster()->MoveIdle();
            return true;

        case ActionType::kMoveTo:
            bot.GetMotionMaster()->Clear();
            bot.GetMotionMaster()->MovePoint(0, action.dest_x, action.dest_y, action.dest_z);
            return true;

        case ActionType::kFollow:
        {
            Player* target = ResolvePlayerTarget(action.target_guid);
            if (!target || target == &bot)
            {
                reason = "follow target is not an online player";
                return false;
            }
            bot.GetMotionMaster()->Clear();
            bot.GetMotionMaster()->MoveFollow(target, 1.0f);
            return true;
        }

        case ActionType::kAttackTarget:
        {
            Player* target = ResolvePlayerTarget(action.target_guid);
            if (!target || target == &bot)
            {
                reason = "attack target is not an online player";
                return false;
            }
            return bot.Attack(target, true);
        }

        case ActionType::kCastSpell:
        {
            if (!sSpellMgr->GetSpellInfo(action.spell_id, DIFFICULTY_NONE))
            {
                reason = "unknown spell " + std::to_string(action.spell_id);
                return false;
            }
            if (!bot.HasSpell(action.spell_id))
            {
                reason = "bot does not know spell " + std::to_string(action.spell_id);
                return false;
            }
            // target_guid 0 means the bot casts on itself.
            Player* target = action.target_guid == 0 ? &bot : ResolvePlayerTarget(action.target_guid);
            if (!target)
            {
                reason = "cast target is not an online player";
                return false;
            }
            bot.CastSpell(target, action.spell_id, false);
            return true;
        }
    }

    reason = "unknown action type";
    return false;
}
