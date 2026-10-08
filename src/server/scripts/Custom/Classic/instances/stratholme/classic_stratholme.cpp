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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/eastern_plaguelands/stratholme/stratholme.cpp (ScriptDev2 lineage, GPL-2)
// go_gauntlet_gate, go_stratholme_postbox, mob_freed_soul, mob_restless_soul, mobs_spectral_ghostly_citizen,
// mobs_cristal_zuggurat, go_supply_crate, spell_haunting_phantoms, spell_eye_of_naxxramas_summon_rockwing_gargoyles
// (mobs_rat_pestifere is not registered in VMaNGOS either and is not ported)

#include "ScriptMgr.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "PetDefines.h"
#include "Player.h"
#include "Random.h"
#include "ScriptedCreature.h"
#include "SpellAuraEffects.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "TemporarySummon.h"
#include "classic_script_text.h"
#include "classic_stratholme.h"
#include <list>

namespace
{
enum ClassicStratholmeMobs : uint32
{
    SAY_CRYSTAL_DESTROYED           = 6527,
    SAY_ALL_CRYSTALS_DESTROYED      = 6289,

    SPELL_EGAN_BLASTER              = 17368,
    SPELL_SOUL_FREED                = 17370,
    QUEST_RESTLESS_SOUL             = 5282,
    ENTRY_RESTLESS                  = 11122,
    ENTRY_FREED                     = 11136,

    SPELL_HAUNTING_PHANTOM          = 16336,
    SPELL_STRAT_SLAP                = 6754,

    NPC_THUZADIN_ACOLYTE            = 10399,

    NPC_STRAT_PLAGUED_RAT           = 10441,
    NPC_STRAT_PLAGUED_INSECT        = 10461,
    NPC_STRAT_PLAGUED_MAGGOT        = 10536,

    SPELL_SUMMON_SPITEFUL_PHANTOM   = 16334,
    SPELL_SUMMON_WRATH_PHANTOM      = 16335
};

// VMaNGOS Creature::MonsterYellToZone - yell heard by every player in the (instance) zone.
void StratholmeYellToZone(Creature* source, uint32 broadcastTextId)
{
    if (!source)
        return;
    // TODO(classic): TC has no zone-wide broadcast-text yell helper; use a range covering the whole instance.
    source->Talk(broadcastTextId, CHAT_MSG_MONSTER_YELL, 1000.0f, nullptr);
}
}

/*######
## go_gauntlet_gate (this is the _first_ of the gauntlet gates, two exist)
######*/

struct classic_go_gauntlet_gate : public GameObjectAI
{
    classic_go_gauntlet_gate(GameObject* go) : GameObjectAI(go) { }

    bool OnGossipHello(Player* /*player*/) override
    {
        InstanceScript* instance = me->GetInstanceScript();
        if (!instance)
            return false;

        if (instance->GetData(TYPE_BARON_RUN) != NOT_STARTED)
            return false;

        instance->SetData(TYPE_BARON_RUN, IN_PROGRESS);
        return false;
    }
};

/*######
## go_stratholme_postbox
######*/

struct classic_go_stratholme_postbox : public GameObjectAI
{
    classic_go_stratholme_postbox(GameObject* go) : GameObjectAI(go) { }

    // VMaNGOS pGOOpen. The postboxes are locked buttons: TC's lock opening (Spell::EffectOpenLock) calls GameObject::Use,
    // which calls this hook.
    // TODO(classic): GameObject::Use (and so this hook) is also reached by a plain CMSG_GAMEOBJ_USE; verify in game that
    //                the postbox event only fires once per box.
    bool OnGossipHello(Player* player) override
    {
        InstanceScript* instance = me->GetInstanceScript();
        if (!instance)
            return false;

        if (instance->GetData(TYPE_POSTMASTER) == DONE)
            return false;

        // When the data is Special, spawn the postmaster
        if (instance->GetData(TYPE_POSTMASTER) == SPECIAL)
        {
            player->CastSpell(player, SPELL_SUMMON_POSTMASTER, true);
            instance->SetData(TYPE_POSTMASTER, DONE);
        }
        else
            instance->SetData(TYPE_POSTMASTER, IN_PROGRESS);

        // Summon 3 postmen for each postbox
        for (uint8 i = 0; i < 3; ++i)
        {
            Position pos = player->GetRandomPoint(player->GetPosition(), 6.0f);
            player->SummonCreature(NPC_UNDEAD_POSTMAN, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), 0.0f, TEMPSUMMON_DEAD_DESPAWN, 0s);
        }

        return false;
    }
};

/*######
## mob_freed_soul
######*/

struct classic_mob_freed_soul : public ScriptedAI
{
    classic_mob_freed_soul(Creature* creature) : ScriptedAI(creature) { }

    void Reset() override
    {
        // Possibly more of these quotes around.
        static uint32 const texts[] = { 6451, 6452, 6453, 6454, 6455 };
        ClassicScriptText(texts[urand(0, 4)], me);
    }
};

/*######
## mob_restless_soul
######*/

struct classic_mob_restless_soul : public ScriptedAI
{
    classic_mob_restless_soul(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    ObjectGuid Tagger;
    uint32 Die_Timer;
    bool Tagged;

    void Initialize()
    {
        Tagger.Clear();
        Die_Timer = 5000;
        Tagged = false;
    }

    void Reset() override
    {
        Initialize();
    }

    void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override
    {
        Player* player = caster ? caster->ToPlayer() : nullptr;
        if (!player)
            return;

        if (!Tagged && spellInfo->Id == SPELL_EGAN_BLASTER && player->GetQuestStatus(QUEST_RESTLESS_SOUL) == QUEST_STATUS_INCOMPLETE)
        {
            Tagged = true;
            Tagger = player->GetGUID();
        }
    }

    void JustSummoned(Creature* summoned) override
    {
        summoned->CastSpell(summoned, SPELL_SOUL_FREED, false);
        if (Unit* temp = ObjectAccessor::GetUnit(*me, Tagger))
            summoned->GetMotionMaster()->MoveFollow(temp, PET_FOLLOW_DIST, ChaseAngle(PET_FOLLOW_ANGLE));
    }

    void JustDied(Unit* /*killer*/) override
    {
        // VMaNGOS summons at (0, 0, 0) which means "at the summoner"
        if (Tagged)
            me->SummonCreature(ENTRY_FREED, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), 0.0f, TEMPSUMMON_TIMED_DESPAWN, 300000ms);
    }

    void UpdateAI(uint32 diff) override
    {
        if (Tagged)
        {
            if (Die_Timer < diff)
            {
                if (Player* temp = ObjectAccessor::GetPlayer(*me, Tagger))
                    temp->KilledMonsterCredit(me->GetEntry(), me->GetGUID());
                me->KillSelf();
            }
            else
                Die_Timer -= diff;
        }
    }
};

/*######
## mobs_spectral_ghostly_citizen
######*/

struct classic_mobs_spectral_ghostly_citizen : public ScriptedAI
{
    classic_mobs_spectral_ghostly_citizen(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
        hasEvadedOnce = false;
    }

    uint32 Die_Timer;
    uint32 cast_Haunting;
    bool Tagged;
    bool hasEvadedOnce;

    void Initialize()
    {
        Die_Timer = 5000;
        cast_Haunting = 20000;
        Tagged = false;
    }

    void Reset() override
    {
        Initialize();
    }

    void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
    {
        if (!Tagged && spellInfo->Id == SPELL_EGAN_BLASTER)
            Tagged = true;
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (Tagged)
        {
            for (uint32 i = 1; i <= 4; ++i)
            {
                Position pos = me->GetRandomPoint(me->GetPosition(), 20.0f);

                //100%, 50%, 33%, 25% chance to spawn
                uint32 j = urand(1, i);
                if (j == 1)
                    me->SummonCreature(ENTRY_RESTLESS, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), 0.0f, TEMPSUMMON_CORPSE_DESPAWN, 600000ms);
            }
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (Tagged)
        {
            if (Die_Timer < diff)
                me->KillSelf();
            else
                Die_Timer -= diff;
        }

        if (!UpdateVictim())
            return;

        if (cast_Haunting < diff)
        {
            me->CastSpell(me->GetVictim(), SPELL_HAUNTING_PHANTOM, false);
            cast_Haunting = 20000;
        }
        else
            cast_Haunting -= diff;
    }

    void ReceiveEmote(Player* player, uint32 emote) override
    {
        switch (emote)
        {
            case TEXT_EMOTE_DANCE:
                if (me->IsInCombat() && !hasEvadedOnce)
                {
                    EnterEvadeMode();
                    hasEvadedOnce = true;
                }
                else
                    me->HandleEmoteCommand(EMOTE_STATE_DANCE);
                break;
            case TEXT_EMOTE_RUDE:
                if (me->IsWithinMeleeRange(player))   // VMaNGOS CanReachWithMeleeAutoAttack
                    me->CastSpell(player, SPELL_STRAT_SLAP, false);
                else
                    me->HandleEmoteCommand(EMOTE_ONESHOT_RUDE);
                break;
            case TEXT_EMOTE_WAVE:
                me->HandleEmoteCommand(EMOTE_ONESHOT_WAVE);
                break;
            case TEXT_EMOTE_BOW:
                me->HandleEmoteCommand(EMOTE_ONESHOT_BOW);
                break;
            case TEXT_EMOTE_KISS:
                me->HandleEmoteCommand(EMOTE_ONESHOT_FLEX);
                break;
            default:
                break;
        }
    }
};

/*######
## mobs_cristal_zuggurat
######*/

struct classic_mobs_cristal_zuggurat : public ScriptedAI
{
    classic_mobs_cristal_zuggurat(Creature* creature) : ScriptedAI(creature), uiUpdateTimer(2000) { }

    uint32 uiUpdateTimer;
    GuidSet m_acolytes;

    void Reset() override { }

    void JustDied(Unit* /*killer*/) override
    {
        if (TempSummon* pop = me->SummonCreature(NPC_THUZADIN_ACOLYTE, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ() - 100.0f, 0.0f, TEMPSUMMON_TIMED_DESPAWN, 1ms))
        {
            StratholmeYellToZone(pop, SAY_CRYSTAL_DESTROYED);
            if (InstanceScript* instance = me->GetInstanceScript())
            {
                instance->SetData(TYPE_CRISTAL_DIE, IN_PROGRESS);
                if (instance->GetData(TYPE_CRISTAL_ALL_DIE) == DONE)
                    StratholmeYellToZone(pop, SAY_ALL_CRYSTALS_DESTROYED);
            }
        }
    }

    void UpdateAI(uint32 diff) override
    {
        InstanceScript* instance = me->GetInstanceScript();
        if (!me->IsAlive() || !instance)
            return;

        if (uiUpdateTimer > diff)
        {
            uiUpdateTimer -= diff;
            return;
        }

        uiUpdateTimer = 2000;

        if (m_acolytes.empty())
        {
            std::list<Creature*> creatures;
            me->GetCreatureListWithEntryInGrid(creatures, NPC_THUZADIN_ACOLYTE, 50.0f);
            for (Creature* creature : creatures)
                m_acolytes.insert(creature->GetGUID());
            return;
        }

        for (ObjectGuid const& guid : m_acolytes)
            if (Creature* creature = me->GetMap()->GetCreature(guid))
                if (creature->IsAlive())
                    return;

        me->KillSelf();
    }
};

/*######
## SUPPLY CRATE
######*/

struct classic_go_supply_crate : public GameObjectAI
{
    classic_go_supply_crate(GameObject* go) : GameObjectAI(go) { }

    // VMaNGOS GameObjectAI::OnUse(Unit*)
    bool OnGossipHello(Player* user) override
    {
        uint32 maxplagued = urand(1, 4);
        uint32 entry = 0;

        switch (urand(0, 2))
        {
            case 0: entry = NPC_STRAT_PLAGUED_RAT; break;       // Plagued Rat
            case 1: entry = NPC_STRAT_PLAGUED_INSECT; break;    // Plagued Insect
            case 2: entry = NPC_STRAT_PLAGUED_MAGGOT; break;    // Plagued Maggot
            default: break;
        }

        for (uint8 i = 0; i < maxplagued; ++i)
            user->SummonCreature(entry, user->GetPositionX() + float(urand(0, 2)), user->GetPositionY() + float(urand(0, 2)), user->GetPositionZ(), 1.0f, TEMPSUMMON_DEAD_DESPAWN, Milliseconds(HOUR * IN_MILLISECONDS));

        // TODO(classic): verify the TC chest handling still opens the crate's loot window after this (VMaNGOS did the same).
        me->SetLootState(GO_JUST_DEACTIVATED);
        return false;
    }
};

// 16336 - Haunting Phantoms
class classic_spell_haunting_phantoms : public AuraScript
{
    void CalcPeriodic(AuraEffect const* /*aurEff*/, bool& isPeriodic, int32& amplitude)
    {
        isPeriodic = true;
        amplitude = 5 * IN_MILLISECONDS;
    }

    void HandleDummyTick(AuraEffect const* /*aurEff*/)
    {
        if (roll_chance(5)) // 5% chance every tick
        {
            if (urand(0, 1))
                GetTarget()->CastSpell(GetTarget(), SPELL_SUMMON_SPITEFUL_PHANTOM, true); // Summon Spiteful Phantom
            else
                GetTarget()->CastSpell(GetTarget(), SPELL_SUMMON_WRATH_PHANTOM, true);    // Summon Wrath Phantom
        }
    }

    void Register() override
    {
        DoEffectCalcPeriodic += AuraEffectCalcPeriodicFn(classic_spell_haunting_phantoms::CalcPeriodic, EFFECT_0, SPELL_AURA_DUMMY);
        OnEffectPeriodic += AuraEffectPeriodicFn(classic_spell_haunting_phantoms::HandleDummyTick, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

// 16381 - Summon Rockwing Gargoyles
class classic_spell_eye_of_naxxramas_summon_rockwing_gargoyles : public SpellScript
{
    // VMaNGOS SpellScript::OnSummon(spell, summon): summon->AI()->AttackStart(caster->GetAttackerForHelper()).
    // TC has no per-summon spell hook, so after the cast the fresh (not yet engaged) summons of this caster are sent in.
    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster)
            return;

        Unit* target = caster->GetVictim();
        if (!target && !caster->getAttackers().empty())
            target = *caster->getAttackers().begin();
        if (!target)
            return;

        for (SpellEffectInfo const& effect : GetSpellInfo()->GetEffects())
        {
            if (!effect.IsEffect(SPELL_EFFECT_SUMMON) || effect.MiscValue <= 0)
                continue;

            std::list<Creature*> summons;
            caster->GetCreatureListWithEntryInGrid(summons, uint32(effect.MiscValue), 50.0f);
            for (Creature* summon : summons)
            {
                TempSummon* tempSummon = summon->ToTempSummon();
                if (!tempSummon || tempSummon->GetSummonerGUID() != caster->GetGUID() || summon->IsInCombat())
                    continue;

                if (summon->AI())
                    summon->AI()->AttackStart(target);
            }
        }
    }

    void Register() override
    {
        AfterCast += SpellCastFn(classic_spell_eye_of_naxxramas_summon_rockwing_gargoyles::HandleAfterCast);
    }
};

void AddSC_classic_stratholme()
{
    RegisterGameObjectAI(classic_go_gauntlet_gate);
    RegisterGameObjectAI(classic_go_stratholme_postbox);
    RegisterCreatureAI(classic_mob_freed_soul);
    RegisterCreatureAI(classic_mob_restless_soul);
    RegisterCreatureAI(classic_mobs_spectral_ghostly_citizen);
    RegisterCreatureAI(classic_mobs_cristal_zuggurat);
    RegisterGameObjectAI(classic_go_supply_crate);
    RegisterSpellScript(classic_spell_haunting_phantoms);
    RegisterSpellScript(classic_spell_eye_of_naxxramas_summon_rockwing_gargoyles);
}
