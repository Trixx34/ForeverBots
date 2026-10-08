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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/eastern_plaguelands/stratholme/boss_baroness_anastari.cpp (ScriptDev2 lineage, GPL-2)

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "ThreatManager.h"
#include "classic_stratholme.h"

namespace
{
enum ClassicBaronessAnastari : uint32
{
    SPELL_BANSHEEWAIL   = 16565,
    SPELL_BANSHEECURSE  = 16867,
    SPELL_ANASTARI_SILENCE = 18327,
    SPELL_ANASTARI_POSSESS = 17244
};

struct ClassicAnastariSpawnLocation
{
    float x, y, z;
};
}

struct classic_boss_baroness_anastari : public ScriptedAI
{
    classic_boss_baroness_anastari(Creature* creature) : ScriptedAI(creature) { }

    uint32 BansheeWail_Timer = 0;
    uint32 BansheeCurse_Timer = 0;
    uint32 Silence_Timer = 0;
    uint32 Possess_Timer = 0;
    uint32 CheckPossess_Timer = 0;

    ObjectGuid PossessedPlayerGuid;
    bool Possessed = false;
    bool Position_memorized = false;

    ObjectGuid PlayerGuids[10];
    float PlayerAggro[10] = { };
    ClassicAnastariSpawnLocation old_Position = { };
    float PlayerHealth = 0.0f;

    void Reset() override
    {
        BansheeWail_Timer       = 1000;
        BansheeCurse_Timer      = 11000;
        Silence_Timer           = 13000;
        Possess_Timer           = 20000;
        CheckPossess_Timer      = 1000;
        PlayerHealth            = 0;

        PossessedPlayerGuid.Clear();
        Possessed = false;
        Position_memorized = false;

        for (int i = 0; i < 5; i++)
        {
            PlayerGuids[i].Clear();
            PlayerAggro[i] = 0;
        }

        me->SetVisible(true);
        me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);           // VMaNGOS UNIT_FLAG_UNINTERACTIBLE
        me->ApplySpellImmune(0, IMMUNITY_DAMAGE, SPELL_SCHOOL_MASK_ALL, false);

        /** Memorize old position of the Banshee */
        old_Position.x = me->GetPositionX();
        old_Position.y = me->GetPositionY();
        old_Position.z = me->GetPositionZ();
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (InstanceScript* instance = me->GetInstanceScript())
            instance->SetData(TYPE_BARONESS, DONE);

        me->SetVisible(true);
        me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);

        for (int i = 0; i < 5; i++)
            if (Unit* pTarget = ObjectAccessor::GetUnit(*me, PlayerGuids[i]))
                pTarget->RestoreFaction();
    }

    void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo*/) override
    {
        if (Possessed)
            damage = 0;
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (Possessed)
        {
            if (CheckPossess_Timer < diff)
            {
                if (Unit* pTarget = ObjectAccessor::GetUnit(*me, PossessedPlayerGuid))
                {
                    if (pTarget->GetHealthPct() < 25.0f || !pTarget->HasAura(SPELL_ANASTARI_POSSESS))
                    {
                        pTarget->RemoveAurasDueToSpell(SPELL_ANASTARI_POSSESS);
                        pTarget->RestoreFaction();

                        /** Teleport the banshee to his old position */
                        me->NearTeleportTo(old_Position.x, old_Position.y, old_Position.z, 0.0f);

                        /** Set the player health back to its old state unless their party members killed them, whoops! */
                        if (pTarget->IsAlive())
                            pTarget->SetHealth(pTarget->CountPctFromMaxHealth(PlayerHealth));

                        me->SetVisible(true);
                        me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                        me->ApplySpellImmune(0, IMMUNITY_DAMAGE, SPELL_SCHOOL_MASK_ALL, false);

                        for (int i = 0; i < 5; i++)
                        {
                            if (Unit* pTarg = ObjectAccessor::GetUnit(*me, PlayerGuids[i]))
                            {
                                me->GetThreatManager().AddThreat(pTarg, PlayerAggro[i], nullptr, true, true);
                                if (pTarg->IsAlive())
                                    me->SetInCombatWith(pTarg);
                            }
                        }
                        me->Attack(pTarget, false);

                        if (DoCast(me, SPELL_ANASTARI_SILENCE) == SPELL_CAST_OK)
                            Silence_Timer = 13000;

                        Possess_Timer = urand(13000, 18000);
                        Possessed = false;
                    }
                }
                CheckPossess_Timer = 1000;
            }
            else
                CheckPossess_Timer -= diff;

            return;
        }

        if (!me->IsVisible())
            me->SetVisible(true);

        //Possess
        if (Possess_Timer < diff)
        {
            if (SelectTarget(SelectTargetMethod::MaxThreat, 1)) // at least 2 players present
            {
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                {
                    if (target->IsPlayer() && target->GetHealthPct() > 30.0f)
                    {
                        PossessedPlayerGuid = target->GetGUID();

                        for (int i = 0; i < 5; i++)
                        {
                            PlayerGuids[i].Clear();
                            PlayerAggro[i] = 0;
                        }

                        // TODO(classic): kept as in VMaNGOS - the inner loop has no break, so the first player on the threat list
                        //                fills every slot and later players are never stored.
                        for (ThreatReference const* ref : me->GetThreatManager().GetUnsortedThreatList())
                        {
                            if (ref->GetVictim()->GetGUID().IsPlayer())
                            {
                                for (int i = 0; i < 5; i++)
                                {
                                    if (PlayerGuids[i].IsEmpty())
                                    {
                                        PlayerGuids[i] = ref->GetVictim()->GetGUID();
                                        PlayerAggro[i] = ref->GetThreat();
                                    }
                                }
                            }
                        }

                        if (!Position_memorized)
                        {
                            /** Memorize old position of the Banshee */
                            old_Position.x = me->GetPositionX();
                            old_Position.y = me->GetPositionY();
                            old_Position.z = me->GetPositionZ();

                            /** Memorize current health of the possessed player */
                            PlayerHealth = target->GetHealthPct();

                            Position_memorized = true;
                        }

                        me->NearTeleportTo(target->GetPosition());
                        if (DoCast(target, SPELL_ANASTARI_POSSESS) == SPELL_CAST_OK)
                        {
                            me->ApplySpellImmune(0, IMMUNITY_DAMAGE, SPELL_SCHOOL_MASK_ALL, true);
                            me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                            me->SetVisible(false);

                            for (int i = 0; i < 5; i++)
                            {
                                if (PlayerGuids[i] != PossessedPlayerGuid)
                                    if (Unit* unit = ObjectAccessor::GetUnit(*me, PlayerGuids[i]))
                                        me->GetThreatManager().AddThreat(unit, PlayerAggro[i], nullptr, true, true);
                            }

                            Possessed = true;
                            CheckPossess_Timer = 2000;
                            return;
                        }
                    }
                }
            }
        }
        else
            Possess_Timer -= diff;

        //BansheeWail
        if (BansheeWail_Timer < diff)
        {
            if (DoCastVictim(SPELL_BANSHEEWAIL) == SPELL_CAST_OK)
                BansheeWail_Timer = 4000;
        }
        else
            BansheeWail_Timer -= diff;

        //BansheeCurse
        if (BansheeCurse_Timer < diff)
        {
            // VMaNGOS CF_AURA_NOT_PRESENT
            if (!me->GetVictim()->HasAura(SPELL_BANSHEECURSE) && DoCastVictim(SPELL_BANSHEECURSE) == SPELL_CAST_OK)
                BansheeCurse_Timer = 18000;
        }
        else
            BansheeCurse_Timer -= diff;

        //Silence
        if (Silence_Timer < diff)
        {
            if (DoCast(me, SPELL_ANASTARI_SILENCE) == SPELL_CAST_OK)
                Silence_Timer = urand(13000, 18000);
        }
        else
            Silence_Timer -= diff;
    }
};

void AddSC_classic_boss_baroness_anastari()
{
    RegisterCreatureAI(classic_boss_baroness_anastari);
}
