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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/stranglethorn_vale/zulgurub/boss_jindo.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: boss_jindo, mob_shade_of_jindo, mob_brain_wash

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "ThreatManager.h"
#include "classic_script_text.h"
#include "classic_zulgurub.h"
#include <list>
#include <utility>
#include <vector>

namespace
{
enum ClassicZgJindo : uint32
{
    SAY_JINDO_AGGRO                     = 10449,

    SPELL_JINDO_BRAIN_WASH_TOTEM        = 24262,
    SPELL_JINDO_POWERFULL_HEALING_WARD  = 24309,
    SPELL_JINDO_HEX                     = 17172,
    SPELL_JINDO_DELUSIONS_OF_JINDO      = 24306,
    SPELL_JINDO_SHADE_OF_JINDO          = 24308,
    SPELL_JINDO_BANISH                  = 24466,
    // Brainwash Totem spells
    SPELL_JINDO_BRAINWASH               = 24261,
    // Healing Ward spells
    SPELL_JINDO_HEAL                    = 24311,    // unused
    // Shade of Jindo spells
    SPELL_JINDO_SHADOWSHOCK             = 24458,
    SPELL_JINDO_INVISIBLE               = 24307,
    // Brain Wash Totem: Avoidance (immunity to AoE)
    SPELL_JINDO_TOTEM_AVOIDANCE         = 23198,

    NPC_JINDO_SHADE                     = 14986,
    NPC_JINDO_BRAINWASH_TOTEM           = 15112,
    NPC_JINDO_POWERFULL_HEALING_WARD    = 14987,
    NPC_JINDO_SACRIFICED_TROLL          = 14826     // VMaNGOS DoSummonSkeleton() (unused helper)
};

// VMaNGOS ADD_AURA_PERMANENT
void ClassicZgJindoAddPermanentAura(Creature* me, uint32 spellId)
{
    if (Aura* aura = me->AddAura(spellId, me))
    {
        aura->SetMaxDuration(-1);
        aura->SetDuration(-1);
    }
}
}

struct classic_boss_jindo : public ScriptedAI
{
    classic_boss_jindo(Creature* creature) : ScriptedAI(creature), m_pInstance(creature->GetInstanceScript()) { }

    InstanceScript* m_pInstance;

    uint32 m_brainWashTotemTimer = 0;
    uint32 m_healingWardTimer = 0;
    uint32 m_hexTimer = 0;
    uint32 m_delusionsTimer = 0;
    uint32 m_summonShadeTimer = 0;
    uint32 m_banishTimer = 0;

    uint32 m_checkBrainWashTimer = 0;

    ObjectGuid m_hexGuid;
    ObjectGuid m_delusionGuid;
    float m_hexAggro = 0.0f;

    // VMaNGOS m_brainWashedPlayerGuids + m_brainWashedPlayersAggro (parallel lists)
    std::vector<std::pair<ObjectGuid, float>> m_brainWashedPlayers;
    std::list<ObjectGuid> m_summonedCreatures;

    void DespawnAllSummons()
    {
        while (!m_summonedCreatures.empty())
        {
            ObjectGuid const g = m_summonedCreatures.front();
            m_summonedCreatures.pop_front();
            switch (g.GetEntry())
            {
                case NPC_JINDO_BRAINWASH_TOTEM:
                case NPC_JINDO_SHADE:
                case NPC_JINDO_POWERFULL_HEALING_WARD:
                    if (Creature* c = ObjectAccessor::GetCreature(*me, g))
                        c->DespawnOrUnsummon();
                    break;
                default:
                    break;
            }
        }

        std::list<Creature*> totems;
        me->GetCreatureListWithEntryInGrid(totems, NPC_JINDO_BRAINWASH_TOTEM, 150.0f);
        for (Creature* totem : totems)
            if (totem->IsAlive())
                totem->DisappearAndDie();
    }

    void JustSummoned(Creature* c) override
    {
        if (c->GetEntry() == NPC_JINDO_SHADE)
        {
            // Adds the boss' enemies to its threat list.
            for (ThreatReference const* ref : me->GetThreatManager().GetUnsortedThreatList())
                c->GetThreatManager().AddThreat(ref->GetVictim(), ref->GetThreat(), nullptr, true, true);

            if (Unit* pTarget = ObjectAccessor::GetUnit(*me, m_delusionGuid))
                if (c->IsValidAttackTarget(pTarget) && pTarget->HasAura(SPELL_JINDO_DELUSIONS_OF_JINDO) && c->AI())
                    c->AI()->AttackStart(pTarget);
        }

        m_summonedCreatures.push_back(c->GetGUID());
        ScriptedAI::JustSummoned(c);
    }

    void Reset() override
    {
        m_brainWashTotemTimer    = urand(10, 20) * IN_MILLISECONDS;
        m_healingWardTimer       = urand(20, 30) * IN_MILLISECONDS;
        m_hexTimer               = urand(20, 50) * IN_MILLISECONDS;
        m_delusionsTimer         = urand(3, 6) * IN_MILLISECONDS;
        m_summonShadeTimer       = urand(6, 8) * IN_MILLISECONDS;
        m_banishTimer            = urand(15, 30) * IN_MILLISECONDS;
        m_checkBrainWashTimer    = 1000;

        m_hexAggro               = 0;

        m_brainWashedPlayers.clear();

        DespawnAllSummons();

        if (m_pInstance && me->IsAlive())
            m_pInstance->SetData(CLASSIC_ZG_TYPE_JINDO, NOT_STARTED);
    }

    void SpellHitTarget(WorldObject* target, SpellInfo const* spellInfo) override
    {
        if (spellInfo->Id == SPELL_JINDO_HEX)
        {
            Player* player = target->ToPlayer();
            if (!player)
                return;

            m_hexGuid = player->GetGUID();
            m_hexAggro = GetThreat(player);

            ModifyThreatByPercent(player, -100);
        }
    }

    void JustDied(Unit* /*killer*/) override
    {
        DespawnAllSummons();
        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_ZG_TYPE_JINDO, DONE);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        ClassicScriptText(SAY_JINDO_AGGRO, me);
        if (m_pInstance)
            m_pInstance->SetData(CLASSIC_ZG_TYPE_JINDO, IN_PROGRESS);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (!m_hexGuid.IsEmpty())
        {
            Player* hexPlayer = ObjectAccessor::GetPlayer(*me, m_hexGuid);
            if (hexPlayer && !hexPlayer->HasAura(SPELL_JINDO_HEX))
            {
                me->GetThreatManager().AddThreat(hexPlayer, m_hexAggro, nullptr, true, true);
                m_hexGuid.Clear();
                m_hexAggro = 0;
            }
            else if (!hexPlayer)
            {
                m_hexGuid.Clear();
                m_hexAggro = 0;
            }
        }

        if (m_brainWashTotemTimer < diff)
        {
            // You need at least 2 people in Jin'do's threatlist, otherwise he will reset!
            if (SelectTarget(SelectTargetMethod::MaxThreat, 1))
                if (DoCastSelf(SPELL_JINDO_BRAIN_WASH_TOTEM) == SPELL_CAST_OK)
                    m_brainWashTotemTimer = urand(10, 30) * IN_MILLISECONDS;
        }
        else
            m_brainWashTotemTimer -= diff;

        // Ustaag : Mind controlled player aggro management
        if (!m_brainWashedPlayers.empty())
        {
            if (m_checkBrainWashTimer < diff)
            {
                for (auto itr = m_brainWashedPlayers.begin(); itr != m_brainWashedPlayers.end(); ++itr)
                {
                    Player* pTarget = ObjectAccessor::GetPlayer(*me, itr->first);
                    if (!pTarget)
                        continue;

                    bool playerDead = pTarget->isDead();
                    bool auraRemoved = pTarget->IsAlive() && !pTarget->HasAuraEffect(SPELL_JINDO_BRAINWASH, EFFECT_0);
                    if (!playerDead && !auraRemoved)
                        continue;

                    if (auraRemoved)
                    {
                        me->GetThreatManager().ModifyThreatByPercent(pTarget, -100);
                        me->GetThreatManager().AddThreat(pTarget, itr->second, nullptr, true, true);
                    }

                    m_brainWashedPlayers.erase(itr);
                    break;
                }
            }
            else
                m_checkBrainWashTimer -= diff;
        }

        if (m_healingWardTimer < diff)
        {
            if (!me->FindNearestCreature(NPC_JINDO_POWERFULL_HEALING_WARD, 200.0f))
            {
                DoCastSelf(SPELL_JINDO_POWERFULL_HEALING_WARD);
                m_healingWardTimer = urand(20, 30) * IN_MILLISECONDS;
            }
        }
        else
            m_healingWardTimer -= diff;

        if (m_hexTimer < diff)
        {
            if (DoCastVictim(SPELL_JINDO_HEX) == SPELL_CAST_OK)
                m_hexTimer = urand(20, 60) * IN_MILLISECONDS;
        }
        else
            m_hexTimer -= diff;

        // Casting the delusion curse with a shade. So shade will attack the same target with the curse.
        if (m_delusionsTimer < diff)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 0.0f, true))
            {
                if (DoCast(target, SPELL_JINDO_DELUSIONS_OF_JINDO) == SPELL_CAST_OK)
                {
                    m_delusionGuid = target->GetGUID();
                    m_delusionsTimer = urand(3, 9) * IN_MILLISECONDS;
                }
            }
        }
        else
            m_delusionsTimer -= diff;

        if (m_summonShadeTimer < diff)
        {
            if (Unit* pTarget = ObjectAccessor::GetUnit(*me, m_delusionGuid))
            {
                if (DoCast(pTarget, SPELL_JINDO_SHADE_OF_JINDO) == SPELL_CAST_OK)
                    m_summonShadeTimer = urand(7, 8) * IN_MILLISECONDS;
            }
        }
        else
            m_summonShadeTimer -= diff;

        // Teleporting a random player and spawning 9 skeletons that will attack
        if (m_banishTimer < diff)
        {
            // VMaNGOS SelectAttackingTarget(RANDOM, 0, SPELL_BANISH, SELECT_FLAG_PLAYER): spell range is checked by the cast
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 0.0f, true))
            {
                if (DoCast(target, SPELL_JINDO_BANISH) == SPELL_CAST_OK)
                    m_banishTimer = urand(15, 35) * IN_MILLISECONDS;
            }
        }
        else
            m_banishTimer -= diff;

        // melee: TC master auto-melee
    }
};

/*######
## mob_shade_of_jindo
######*/

struct classic_mob_shade_of_jindo : public ScriptedAI
{
    classic_mob_shade_of_jindo(Creature* creature) : ScriptedAI(creature) { }

    uint32 ShadowShock_Timer = 0;

    void Reset() override
    {
        ShadowShock_Timer = 1000;
        ClassicZgJindoAddPermanentAura(me, SPELL_JINDO_INVISIBLE);
    }

    void DamageTaken(Unit* attacker, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo*/) override
    {
        if (attacker && !attacker->HasAura(SPELL_JINDO_DELUSIONS_OF_JINDO))
            damage = 0;
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (Unit* victim = me->GetVictim())
            if (victim->HasAura(SPELL_JINDO_HEX))
                ModifyThreatByPercent(victim, -100);

        // ShadowShock_Timer
        if (ShadowShock_Timer < diff)
        {
            DoCastVictim(SPELL_JINDO_SHADOWSHOCK);
            ShadowShock_Timer = 2000;
        }
        else
            ShadowShock_Timer -= diff;

        // melee: TC master auto-melee
    }
};

/*######
## mob_brain_wash (Brain Wash Totem)
######*/

struct classic_mob_brain_wash : public ScriptedAI
{
    classic_mob_brain_wash(Creature* creature) : ScriptedAI(creature), m_pInstance(creature->GetInstanceScript()) { }

    InstanceScript* m_pInstance;

    ObjectGuid PlayerMCGuid;
    uint32 CheckTimer = 0;

    void Reset() override
    {
        PlayerMCGuid.Clear();
        CheckTimer = 0;

        ClassicZgJindoAddPermanentAura(me, SPELL_JINDO_TOTEM_AVOIDANCE); // Avoidance : immunity to AoE
        me->SetControlled(true, UNIT_STATE_ROOT);
        SetCombatMovement(false);
    }

    void UpdateAI(uint32 /*diff*/) override
    {
        // TODO(classic): VMaNGOS checks SelectHostileTarget() before SetInCombatWithZone(); the TC totem has no threat
        // list right after being summoned, so the zone combat is done first here (otherwise it would die instantly).
        if (!me->IsInCombat())
            DoZoneInCombat();

        if (!UpdateVictim() || !m_pInstance)
        {
            me->DisappearAndDie();
            return;
        }

        me->SetControlled(true, UNIT_STATE_ROOT);

        // Already mind controlled the player
        if (!PlayerMCGuid.IsEmpty())
            if (Player* pPlayer = ObjectAccessor::GetPlayer(*me, PlayerMCGuid))
                if (pPlayer->IsAlive() && pPlayer->HasAuraEffect(SPELL_JINDO_BRAINWASH, EFFECT_0))
                    return;

        if (Creature* pJindo = ObjectAccessor::GetCreature(*me, m_pInstance->GetGuidData(CLASSIC_ZG_DATA_JINDO)))
        {
            CreatureAI* jindoAI = pJindo->AI();
            if (!jindoAI)
                return;

            // At least 2 players must be in Jin'do's threat list, otherwise he resets!
            if (jindoAI->SelectTarget(SelectTargetMethod::MaxThreat, 1))
            {
                Unit* pTarget = jindoAI->SelectTarget(SelectTargetMethod::Random, 0);
                if (pTarget && pTarget->IsAlive() && pTarget->IsPlayer() && !pTarget->HasAura(SPELL_JINDO_HEX) && !pTarget->HasAura(SPELL_JINDO_BRAINWASH))
                {
                    if (classic_boss_jindo* pJindoAI = dynamic_cast<classic_boss_jindo*>(jindoAI))
                    {
                        pJindoAI->m_brainWashedPlayers.emplace_back(pTarget->GetGUID(), pJindo->GetThreatManager().GetThreat(pTarget));
                        if (DoCast(pTarget, SPELL_JINDO_BRAINWASH) == SPELL_CAST_OK)
                        {
                            PlayerMCGuid = pTarget->GetGUID();
                            pJindoAI->m_checkBrainWashTimer = 1000;
                        }
                    }
                }
            }
        }
    }
};

void AddSC_classic_boss_jindo()
{
    RegisterCreatureAI(classic_boss_jindo);
    RegisterCreatureAI(classic_mob_shade_of_jindo);
    RegisterCreatureAI(classic_mob_brain_wash);
}
