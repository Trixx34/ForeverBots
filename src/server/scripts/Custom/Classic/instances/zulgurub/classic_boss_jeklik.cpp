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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/stranglethorn_vale/zulgurub/boss_jeklik.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: boss_jeklik, mob_batrider, npc_guru_bat_rider

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "Log.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"
#include "SpellInfo.h"
#include "classic_script_text.h"
#include "classic_zulgurub.h"

namespace
{
enum ClassicZgJeklik : uint32
{
    SAY_JEKLIK_AGGRO                = 10027,
    SAY_JEKLIK_RAIN_FIRE            = 10369,
    SAY_JEKLIK_DEATH                = 10452,
    TEXT_JEKLIK_GREAT_HEAL          = 10494,
    TEXT_JEKLIK_SUMMON_BATS         = 10370,

    SPELL_JEKLIK_GREENCHANNELING    = 13540, // Green Spell. [ChanneledInstant]
    SPELL_JEKLIK_BAT_FORM           = 23966, // Transform to bat. [Instant]
    SPELL_JEKLIK_BOMB               = 19629, // Summon Flames gobject 177764. [Instant]
    SPELL_JEKLIK_THROW_LIQUID_FIRE  = 23970, // [Instant] [50000 yd range]
    SPELL_JEKLIK_TRANSFORMVISUAL    = 24085, // [Instant] (unused)
    SPELL_JEKLIK_BLOODLEECH         = 22644, // (unused)

    // P1
    SPELL_JEKLIK_SWOOP              = 23919,
    SPELL_JEKLIK_CHARGE             = 24408,
    SPELL_JEKLIK_SONICBURST         = 23918,
    SPELL_JEKLIK_PIERCEARMOR        = 12097,

    // P2
    SPELL_JEKLIK_SHADOW_WORD_PAIN   = 23952,
    SPELL_JEKLIK_MIND_FLAY          = 23953,
    SPELL_JEKLIK_GREAT_HEAL         = 23954,
    SPELL_JEKLIK_CURSE_OF_BLOOD     = 16098,
    SPELL_JEKLIK_SCREECH            = 6605,

    NPC_JEKLIK_BLOODSEEKER_BAT      = 11368,
    NPC_JEKLIK_FRENZIED_BAT         = 14965
};

enum ClassicZgGuruBatRider : uint32
{
    SPELL_BAT_RIDER_EXPLOSION           = 24024, // [3 sec cast]
    SPELL_BAT_RIDER_DEMORALIZING_SHOUT  = 23511,
    SPELL_BAT_RIDER_BATTLE_COMBAT       = 5115,
    SPELL_BAT_RIDER_INFECTED_BITE       = 16128,
    SPELL_BAT_RIDER_THRASH              = 3391
};
}

struct classic_boss_jeklik : public ScriptedAI
{
    classic_boss_jeklik(Creature* creature) : ScriptedAI(creature), m_pInstance(creature->GetInstanceScript()) { }

    InstanceScript* m_pInstance;

    uint32 Charge_Timer = 0;
    uint32 SonicBurst_Timer = 0;
    uint32 Swoop_Timer = 0;
    uint32 PierceArmor_Timer = 0;
    uint32 SpawnBats_Timer = 0;
    uint32 ShadowWordPain_Timer = 0;
    uint32 CurseOfBlood_Timer = 0;
    uint32 MindFlay_Timer = 0;
    uint32 GreatHeal_Timer = 0;
    uint32 Screech_Timer = 0;
    uint32 SpawnFlyingBats_Timer = 0;
    uint32 GlobalCooldown = 0;
    uint32 Diff_Add = 0;
    bool skillStarted = false;

    bool PhaseTwo = false;

    void Reset() override
    {
        SpawnBats_Timer = 40000;

        Charge_Timer      = 10000;
        SonicBurst_Timer  = 12000;
        Swoop_Timer       = 8000;
        PierceArmor_Timer = 9000;

        Screech_Timer = 12000;

        ShadowWordPain_Timer  = 9000;
        CurseOfBlood_Timer    = 26000;
        MindFlay_Timer        = 2000;
        GreatHeal_Timer       = 20000;
        SpawnFlyingBats_Timer = 10000;

        GlobalCooldown = 0;
        Diff_Add       = 0;
        PhaseTwo       = false;
        skillStarted   = false;

        if (m_pInstance && me->IsAlive())
            m_pInstance->SetData(CLASSIC_ZG_TYPE_JEKLIK, FAIL);

        me->SetObjectScale(1.5f);
    }

    // VMaNGOS casts the channel from the constructor
    void JustAppeared() override
    {
        ScriptedAI::JustAppeared();
        me->CastSpell(me, SPELL_JEKLIK_GREENCHANNELING, false);
    }

    void JustReachedHome() override
    {
        me->CastSpell(me, SPELL_JEKLIK_GREENCHANNELING, false);
        me->SetObjectScale(1.0f);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        me->AddUnitState(UNIT_STATE_IGNORE_PATHFINDING);
        ClassicScriptText(SAY_JEKLIK_AGGRO, me);
        me->AddAura(SPELL_JEKLIK_BAT_FORM, me);
        // VMaNGOS SetFly(true)
        me->SetCanFly(true);
        me->SetDisableGravity(true);
        me->SetObjectScale(2.0f);

        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_ZG_TYPE_JEKLIK, IN_PROGRESS);
    }

    void JustDied(Unit* /*killer*/) override
    {
        ClassicScriptText(SAY_JEKLIK_DEATH, me);

        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_ZG_TYPE_JEKLIK, DONE);

        // Remove a Hakkar Power stack.
        me->CastSpell(me, CLASSIC_ZG_SPELL_HAKKAR_POWER_DOWN, true);
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        me->ClearUnitState(UNIT_STATE_IGNORE_PATHFINDING);
        me->RemoveAurasDueToSpell(SPELL_JEKLIK_BAT_FORM);
        me->SetCanFly(false);
        me->SetDisableGravity(false);

        ScriptedAI::EnterEvadeMode(why);
        // VMaNGOS: m_creature->Respawn() (full reset of the living creature) + NearTeleportTo(home)
        // TODO(classic): TC Creature::Respawn() does nothing on a living creature; only the teleport home is kept.
        me->NearTeleportTo(me->GetHomePosition());
    }

    void UpdateAI(uint32 lastDiff) override
    {
        if (!m_pInstance || !UpdateVictim())
            return;

        if (!PhaseTwo && me->GetHealthPct() < 50.0f)
        {
            // VMaNGOS SetInvincibilityHpThreshold(0): no TC equivalent needed (no threshold is set by this port)
            me->RemoveAurasDueToSpell(SPELL_JEKLIK_BAT_FORM);
            me->SetCanFly(false);
            me->SetDisableGravity(false);
            me->SetObjectScale(1.5f);
            ResetThreatList();
            PhaseTwo = true;
        }

        // SUMMON_BATS
        if (me->GetHealthPct() > 50.0f)
        {
            if (SpawnBats_Timer < lastDiff)
            {
                ClassicScriptText(TEXT_JEKLIK_SUMMON_BATS, me);
                for (uint8 i = 0; i < 6; ++i)
                {
                    Creature* Bat = me->SummonCreature(NPC_JEKLIK_BLOODSEEKER_BAT, -12294.0f + frand(0.0f, 5.0f), -1382.0f + frand(0.0f, 5.0f), 144.8304f, 5.483f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 15s);
                    if (Bat && Bat->AI())
                    {
                        if (Unit* pTarget = SelectTarget(SelectTargetMethod::Random, 0))
                            Bat->AI()->AttackStart(pTarget);
                    }
                }
                SpawnBats_Timer = 65000;
            }
            else
                SpawnBats_Timer -= lastDiff;
        }

        // SPAWN_FLYING_BAT
        if (PhaseTwo)
        {
            if (SpawnFlyingBats_Timer < lastDiff)
            {
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                {
                    Creature* FlyingBat = me->SummonCreature(NPC_JEKLIK_FRENZIED_BAT, target->GetPositionX(), target->GetPositionY(), target->GetPositionZ() + 15, 0, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 30min);
                    if (FlyingBat && FlyingBat->AI())
                    {
                        FlyingBat->AI()->AttackStart(target);
                        ClassicScriptText(SAY_JEKLIK_RAIN_FIRE, me);
                    }
                }
                SpawnFlyingBats_Timer = 10000;
            }
            else
                SpawnFlyingBats_Timer -= lastDiff;
        }

        if (me->IsNonMeleeSpellCast(false))
        {
            Diff_Add += lastDiff;
            return;
        }

        // melee: TC master auto-melee

        if (GlobalCooldown > lastDiff)
        {
            Diff_Add += lastDiff;
            GlobalCooldown -= lastDiff;
            return;
        }

        uint32 diff = Diff_Add + lastDiff;
        Diff_Add = 0;

        skillStarted = false;
        // BAT FORM COMBAT (PHASE 1)
        if (!PhaseTwo)
        {
            // CHARGE
            if (Charge_Timer < diff)
            {
                if (!skillStarted)
                {
                    // VMaNGOS CastSpellOnNearestVictim(SPELL_CHARGE, 10.0f, 40.0f, false)
                    Unit* chargeTarget = SelectTarget(SelectTargetMethod::MinDistance, 0, [this](Unit* unit)
                    {
                        float dist = me->GetDistance(unit);
                        return dist >= 10.0f && dist <= 40.0f;
                    });
                    if (chargeTarget && DoCast(chargeTarget, SPELL_JEKLIK_CHARGE) == SPELL_CAST_OK)
                    {
                        skillStarted   = true;
                        Charge_Timer   = urand(15000, 30000);
                        GlobalCooldown = 1000;
                    }
                    else
                        Charge_Timer = 2000;
                }
            }
            else
                Charge_Timer -= diff;

            // SCREECH
            if (Screech_Timer < diff)
            {
                if (!skillStarted)
                {
                    if (DoCastVictim(SPELL_JEKLIK_SCREECH) == SPELL_CAST_OK)
                    {
                        skillStarted   = true;
                        Screech_Timer  = 30000;
                        GlobalCooldown = 1000;
                    }
                    else
                        Charge_Timer = 1000;    // sic (VMaNGOS)
                }
            }
            else
                Screech_Timer -= diff;

            // SONICBURST
            if (SonicBurst_Timer < diff)
            {
                if (!skillStarted)
                {
                    if (DoCastVictim(SPELL_JEKLIK_SONICBURST) == SPELL_CAST_OK)
                    {
                        skillStarted     = true;
                        SonicBurst_Timer = urand(20000, 24000);
                        GlobalCooldown   = 1000;
                    }
                }
            }
            else
                SonicBurst_Timer -= diff;

            // SWOOP
            if (Swoop_Timer < diff)
            {
                if (!skillStarted)
                {
                    if (DoCastVictim(SPELL_JEKLIK_SWOOP) == SPELL_CAST_OK)
                    {
                        skillStarted   = true;
                        Swoop_Timer    = urand(12000, 15000);
                        GlobalCooldown = 1000;
                    }
                }
            }
            else
                Swoop_Timer -= diff;

            // PIERCEARMOR
            if (PierceArmor_Timer < diff)
            {
                if (!skillStarted)
                {
                    if (DoCastVictim(SPELL_JEKLIK_PIERCEARMOR) == SPELL_CAST_OK)
                    {
                        skillStarted      = true;
                        PierceArmor_Timer = urand(16000, 18000);
                        GlobalCooldown    = 1000;
                    }
                }
            }
            else
                PierceArmor_Timer -= diff;
        }
        // P2
        else
        {
            // SHADOW_WORD_PAIN
            if (ShadowWordPain_Timer < diff)
            {
                if (!skillStarted)
                {
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                    {
                        if (DoCast(target, SPELL_JEKLIK_SHADOW_WORD_PAIN) == SPELL_CAST_OK)
                        {
                            ShadowWordPain_Timer = urand(8000, 12000);
                            GlobalCooldown       = 1000;
                            skillStarted         = true;
                        }
                    }
                }
            }
            else
                ShadowWordPain_Timer -= diff;

            // GREAT_HEAL
            if (GreatHeal_Timer < diff)
            {
                if (!skillStarted)
                {
                    me->InterruptNonMeleeSpells(false);
                    if (DoCastSelf(SPELL_JEKLIK_GREAT_HEAL) == SPELL_CAST_OK)
                    {
                        ClassicScriptText(TEXT_JEKLIK_GREAT_HEAL, me);
                        skillStarted = true;
                        GreatHeal_Timer = urand(20000, 25000);
                    }
                }
            }
            else
                GreatHeal_Timer -= diff;

            // MIND_FLAY
            if (MindFlay_Timer < diff)
            {
                if (!skillStarted)
                {
                    me->InterruptNonMeleeSpells(false);
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                    {
                        if (DoCast(target, SPELL_JEKLIK_MIND_FLAY) == SPELL_CAST_OK)
                        {
                            MindFlay_Timer = urand(25000, 30000);
                            skillStarted   = true;
                        }
                    }
                }
            }
            else
                MindFlay_Timer -= diff;

            // CURSE_OF_BLOOD
            if (CurseOfBlood_Timer < diff)
            {
                if (!skillStarted)
                {
                    me->InterruptNonMeleeSpells(false);
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                    {
                        if (DoCast(target, SPELL_JEKLIK_CURSE_OF_BLOOD) == SPELL_CAST_OK)
                        {
                            CurseOfBlood_Timer = urand(25000, 30000);
                            skillStarted = true;
                        }
                    }
                }
            }
            else
                CurseOfBlood_Timer -= diff;
        }
    }
};

/*######
## mob_batrider (Frenzied Bloodseeker Bat, flying bomber)
######*/

struct classic_mob_batrider : public ScriptedAI
{
    classic_mob_batrider(Creature* creature) : ScriptedAI(creature), m_pInstance(creature->GetInstanceScript())
    {
        me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
    }

    InstanceScript* m_pInstance;

    uint32 Bomb_Timer = 0;

    void Reset() override
    {
        Bomb_Timer = 2000;
    }

    void AttackStart(Unit* /*who*/) override { }

    void MoveInLineOfSight(Unit* /*who*/) override { }

    void DoAttack()
    {
        if (Bomb_Timer)
            return;

        Bomb_Timer = urand(5000, 10000);

        if (Creature* pJeklik = me->FindNearestCreature(CLASSIC_ZG_NPC_JEKLIK, 150.0f))
        {
            Unit* pTarget = pJeklik->AI() ? pJeklik->AI()->SelectTarget(SelectTargetMethod::Random, 0) : nullptr;
            if (pTarget)
                me->CastSpell(pTarget, SPELL_JEKLIK_THROW_LIQUID_FIRE, false);
            else
                TC_LOG_DEBUG("scripts", "classic_mob_batrider: Unable to find a target");
        }
        else
            TC_LOG_DEBUG("scripts", "classic_mob_batrider: Jeklik not found.");
    }

    // Called when spell hits creature's target
    void SpellHitTarget(WorldObject* target, SpellInfo const* spellInfo) override
    {
        // Trigger bomb AoE on the ground
        if (target && spellInfo && spellInfo->Id == SPELL_JEKLIK_THROW_LIQUID_FIRE)
            me->CastSpell(target->GetPosition(), SPELL_JEKLIK_BOMB, false);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!m_pInstance)
            return;

        if (Bomb_Timer < diff)
            Bomb_Timer = 0;
        else
            Bomb_Timer -= diff;

        switch (m_pInstance->GetData(CLASSIC_ZG_TYPE_JEKLIK))
        {
            case IN_PROGRESS:
                DoAttack();
                break;
            default:
                me->DespawnOrUnsummon();
                break;
        }
    }
};

/*######
## npc_guru_bat_rider (Gurubashi Bat Rider, trash)
######*/

struct classic_npc_guru_bat_rider : public ScriptedAI
{
    classic_npc_guru_bat_rider(Creature* creature) : ScriptedAI(creature) { }

    bool GoingToExplose = false;
    uint32 Despawn_Timer = 0;
    uint32 Combat_Timer = 0;
    uint32 InfectedBite_Timer = 0;
    uint32 Thrash_Timer = 0;

    void Reset() override
    {
        GoingToExplose     = false;
        Despawn_Timer      = 0;
        Combat_Timer       = 8000;
        InfectedBite_Timer = 6500;
        Thrash_Timer       = 6000;
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        me->CastSpell(me, SPELL_BAT_RIDER_DEMORALIZING_SHOUT, false);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (!GoingToExplose && me->GetHealthPct() < 40.0f)
        {
            GoingToExplose = true;
            // TODO(classic): hardcoded VMaNGOS emote strings (no broadcast text id); consider BroadcastText ids
            if (urand(0, 1))
                me->TextEmote("Gurubashi Bat Rider becomes fully engulfed in flames.", nullptr, false);
            else
                me->TextEmote("Gurubashi Bat Rider gets a crazed look in his eye.", nullptr, false);
            me->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_FEAR, true); // fear immunity
            me->CastSpell(me, SPELL_BAT_RIDER_EXPLOSION, false);
        }

        if (Combat_Timer < diff)
        {
            if (DoCastSelf(SPELL_BAT_RIDER_BATTLE_COMBAT) == SPELL_CAST_OK)
                Combat_Timer = 25000;
        }
        else
            Combat_Timer -= diff;

        if (InfectedBite_Timer < diff)
        {
            if (DoCastVictim(SPELL_BAT_RIDER_INFECTED_BITE) == SPELL_CAST_OK)
                InfectedBite_Timer = 15000;
        }
        else
            InfectedBite_Timer -= diff;

        if (Thrash_Timer < diff)
        {
            if (DoCastVictim(SPELL_BAT_RIDER_THRASH) == SPELL_CAST_OK)
                Thrash_Timer = 6000;
        }
        else
            Thrash_Timer -= diff;

        // melee: TC master auto-melee
    }
};

void AddSC_classic_boss_jeklik()
{
    RegisterCreatureAI(classic_boss_jeklik);
    RegisterCreatureAI(classic_mob_batrider);
    RegisterCreatureAI(classic_npc_guru_bat_rider);
}
