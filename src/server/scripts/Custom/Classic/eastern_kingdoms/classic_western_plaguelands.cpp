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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/western_plaguelands/western_plaguelands.cpp (ScriptDev2 lineage, GPL-2)
// Quest support: 5216, 5219, 5222, 5225, 5229, 5231, 5233, 5235 (scourge cauldrons), Andorhal towers, High Protector Lorik

#include "ScriptMgr.h"
#include "GameObject.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptedCreature.h"

/*######
## npc_the_scourge_cauldron
######*/

enum ScourgeCauldron
{
    CAULDRON_AREA_FELSTONE      = 199,
    CAULDRON_AREA_DALSON        = 200,
    CAULDRON_AREA_GAHRRON       = 201,
    CAULDRON_AREA_WRITHING      = 202,

    CAULDRON_NPC_FELSTONE_LORD  = 11075,
    CAULDRON_NPC_DALSON_LORD    = 11077,
    CAULDRON_NPC_GAHRRON_LORD   = 11078,
    CAULDRON_NPC_WRITHING_LORD  = 11076,

    CAULDRON_MIN_RESPAWN_DELAY  = 600
};

struct classic_npc_the_scourge_cauldron : public ScriptedAI
{
    classic_npc_the_scourge_cauldron(Creature* creature) : ScriptedAI(creature) { }

    void Reset() override { }

    void DoDie()
    {
        // summoner dies here
        me->KillSelf();
        // override any database `spawntimesecs` to prevent duplicated summons
        if (me->GetRespawnDelay() < CAULDRON_MIN_RESPAWN_DELAY)
            me->SetRespawnDelay(CAULDRON_MIN_RESPAWN_DELAY);
    }

    void SummonLordAndDie(uint32 entry)
    {
        // VMaNGOS SummonCreature(entry, 0, 0, 0, ...) summons at the summoner's position
        me->SummonCreature(entry, me->GetPosition(), TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 600000ms);
        DoDie();
    }

    void MoveInLineOfSight(Unit* who) override
    {
        if (!who)
            return;

        Player* player = who->ToPlayer();
        if (!player)
            return;

        switch (me->GetAreaId())
        {
            case CAULDRON_AREA_FELSTONE:
                if (player->GetQuestStatus(5216) == QUEST_STATUS_INCOMPLETE || player->GetQuestStatus(5229) == QUEST_STATUS_INCOMPLETE)
                    SummonLordAndDie(CAULDRON_NPC_FELSTONE_LORD);
                break;
            case CAULDRON_AREA_DALSON:
                if (player->GetQuestStatus(5219) == QUEST_STATUS_INCOMPLETE || player->GetQuestStatus(5231) == QUEST_STATUS_INCOMPLETE)
                    SummonLordAndDie(CAULDRON_NPC_DALSON_LORD);
                break;
            case CAULDRON_AREA_GAHRRON:
                if (player->GetQuestStatus(5225) == QUEST_STATUS_INCOMPLETE || player->GetQuestStatus(5235) == QUEST_STATUS_INCOMPLETE)
                    SummonLordAndDie(CAULDRON_NPC_GAHRRON_LORD);
                break;
            case CAULDRON_AREA_WRITHING:
                if (player->GetQuestStatus(5222) == QUEST_STATUS_INCOMPLETE || player->GetQuestStatus(5233) == QUEST_STATUS_INCOMPLETE)
                    SummonLordAndDie(CAULDRON_NPC_WRITHING_LORD);
                break;
            default:
                break;
        }
    }
};

/*######
## npc_andorhal_tower
######*/

enum AndorhalTower
{
    ANDORHAL_GO_BEACON_TORCH    = 176093
};

struct classic_npc_andorhal_tower : public ScriptedAI
{
    classic_npc_andorhal_tower(Creature* creature) : ScriptedAI(creature)
    {
        SetCombatMovement(false);   // VMaNGOS Scripted_NoMovementAI
    }

    void Reset() override { }

    void MoveInLineOfSight(Unit* who) override
    {
        if (!who)
            return;

        Player* player = who->ToPlayer();
        if (!player)
            return;

        if (me->FindNearestGameObject(ANDORHAL_GO_BEACON_TORCH, 20.0f))
            player->KilledMonsterCredit(me->GetEntry(), me->GetGUID());
    }
};

/*######
## npc_highprotectorlorik
######*/

enum HighProtectorLorik
{
    LORIK_SPELL_RETRIBUTION_AURA    = 8990,
    LORIK_SPELL_ARCANE_BLAST        = 10833,
    LORIK_SPELL_DIVINE_SHIELD       = 13874,
    LORIK_SPELL_HOLY_LIGHT          = 15493,
    LORIK_SPELL_SHIELD_SLAM         = 15655
};

struct classic_npc_highprotectorlorik : public ScriptedAI
{
    classic_npc_highprotectorlorik(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        _globalCooldown     = 0;
        _arcaneBlastTimer   = 7000;
        _divineShieldTimer  = 2000;
        _holyLightTimer     = 2000;
        _shieldSlamTimer    = 2000;
    }

    void Reset() override
    {
        Initialize();
    }

    void UpdateAI(uint32 diff) override
    {
        if (!me->HasAura(LORIK_SPELL_RETRIBUTION_AURA))
            me->CastSpell(me, LORIK_SPELL_RETRIBUTION_AURA, true);

        // Return since we have no target
        if (!UpdateVictim())
            return;

        // global cooldown
        if (_globalCooldown > diff)
            _globalCooldown -= diff;
        else
        {
            if (me->IsNonMeleeSpellCast(false))
                _globalCooldown = 1;
            else
                _globalCooldown = 0;
        }

        // Divine Shield
        if (_divineShieldTimer < diff)
        {
            if (_globalCooldown == 0 && me->GetHealthPct() <= 15.0f)
            {
                if (DoCastSelf(LORIK_SPELL_DIVINE_SHIELD) == SPELL_CAST_OK)
                {
                    _divineShieldTimer = 45000;
                    _globalCooldown = 1000;
                }
            }
        }
        else
            _divineShieldTimer -= diff;

        // Arcane Blast
        if (_arcaneBlastTimer < diff)
        {
            if (!_globalCooldown)
            {
                if (DoCastVictim(LORIK_SPELL_ARCANE_BLAST) == SPELL_CAST_OK)
                {
                    _arcaneBlastTimer = urand(10000, 12000);
                    _globalCooldown = 1000;
                }
            }
        }
        else
            _arcaneBlastTimer -= diff;

        // Holy Light
        if (_holyLightTimer < diff)
        {
            if (!_globalCooldown && me->GetHealthPct() <= 60.0f && me->GetPower(POWER_MANA) > 700)
            {
                if (DoCastSelf(LORIK_SPELL_HOLY_LIGHT) == SPELL_CAST_OK)
                {
                    _holyLightTimer = urand(2000, 6000);
                    _globalCooldown = 1000;
                }
            }
        }
        else
            _holyLightTimer -= diff;

        // Shield Slam (interrupt)
        if (_shieldSlamTimer < diff)
        {
            Unit* victim = me->GetVictim();
            if (!_globalCooldown && victim && victim->IsNonMeleeSpellCast(false))
            {
                if (DoCast(victim, LORIK_SPELL_SHIELD_SLAM) == SPELL_CAST_OK)
                {
                    _shieldSlamTimer = 9000;
                    _globalCooldown = 1000;
                }
            }
        }
        else
            _shieldSlamTimer -= diff;
    }

private:
    uint32 _globalCooldown;
    uint32 _arcaneBlastTimer;
    uint32 _divineShieldTimer;
    uint32 _holyLightTimer;
    uint32 _shieldSlamTimer;
};

void AddSC_classic_western_plaguelands()
{
    RegisterCreatureAI(classic_npc_the_scourge_cauldron);
    RegisterCreatureAI(classic_npc_andorhal_tower);
    RegisterCreatureAI(classic_npc_highprotectorlorik);
}
