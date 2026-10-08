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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/uldaman/uldaman.cpp (ScriptDev2 lineage, GPL-2)
// Quest support: 2278 + trash
// Ported: go_keystone_chamber, mob_stone_keeper, mob_jadespine_basilisk, mob_annora, spell_uldaman_awaken_vault_warder

#include "ScriptMgr.h"
#include "Containers.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellScript.h"
#include "classic_uldaman.h"
#include <list>

/*######
 ## go_keystone_chamber
 ######*/

struct classic_go_keystone_chamber : public GameObjectAI
{
    classic_go_keystone_chamber(GameObject* go) : GameObjectAI(go) { }

    bool OnGossipHello(Player* player) override
    {
        InstanceScript* instance = me->GetInstanceScript();

        if (!instance)
            return false;

        if (player)
            instance->SetGuidData(SET_DATA64_IRONAYA_WAKER, player->GetGUID()); // Ironaya first victim

        me->ReplaceAllFlags(GO_FLAG_INTERACT_COND);

        // save state
        instance->SetData(ULDAMAN_ENCOUNTER_IRONAYA_DOOR, DONE);

        return false;
    }
};

/*######
## mob_stone_keeper
######*/

struct classic_mob_stone_keeper : public ScriptedAI
{
    classic_mob_stone_keeper(Creature* creature) : ScriptedAI(creature)
    {
        _instance = creature->GetInstanceScript();
        _trampleTimer = urand(4000, 9000);
    }

    void Reset() override
    {
        _trampleTimer = urand(4000, 9000);
    }

    void EnterEvadeMode(EvadeReason /*why*/) override
    {
        if (Unit* target = me->SelectNearestHostileUnitInAggroRange(true))
        {
            AttackStart(target);
            return;
        }
        Reset();
        if (_instance)
            _instance->SetData(ULDAMAN_ENCOUNTER_STONE_KEEPERS, FAIL);
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (_instance)
            _instance->SetData(ULDAMAN_ENCOUNTER_STONE_KEEPERS, IN_PROGRESS);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (_trampleTimer < diff)
        {
            if (DoCastVictim(SPELL_TRAMPLE) == SPELL_CAST_OK)
                _trampleTimer = urand(4000, 10000);
        }
        else
            _trampleTimer -= diff;
    }

private:
    InstanceScript* _instance;
    uint32 _trampleTimer;
};

/*######
## mob_jadespine_basilisk
######*/

struct classic_mob_jadespine_basilisk : public ScriptedAI
{
    classic_mob_jadespine_basilisk(Creature* creature) : ScriptedAI(creature), _cslumberTimer(2000) { }

    void Reset() override
    {
        _cslumberTimer = 2000;
    }

    void UpdateAI(uint32 diff) override
    {
        //Return since we have no target
        if (!UpdateVictim())
            return;

        //Cslumber_Timer
        if (_cslumberTimer < diff)
        {
            //Cast
            me->CastSpell(me->GetVictim(), SPELL_CRYSTALLINE_SLUMBER, false);
            ModifyThreatByPercent(me->GetVictim(), -100);

            //Stop attacking target thats asleep and pick new target
            _cslumberTimer = 28000;

            Unit* target = SelectTarget(SelectTargetMethod::MaxThreat, 0);

            if (!target || target == me->GetVictim())
                target = SelectTarget(SelectTargetMethod::MaxThreat, 1);

            if (target)
                AttackStart(target);
        }
        else
            _cslumberTimer -= diff;
    }

private:
    uint32 _cslumberTimer;
};

/*######
## mob_annora
######*/

enum Annora
{
    NPC_ULDAMAN_SCORPION        = 7078      // VMaNGOS hardcoded escort entry
};

struct classic_mob_annora : public ScriptedAI
{
    classic_mob_annora(Creature* creature) : ScriptedAI(creature), _nbScorpion(0), _isSpawned(false), _hidden(false) { }

    // VMaNGOS does this in the AI constructor (once per creature, not on respawn)
    void JustAppeared() override
    {
        ScriptedAI::JustAppeared();

        if (_hidden)
            return;
        _hidden = true;

        me->SetVisible(false);
        // TODO(classic): VMaNGOS sets UNIT_FLAG_IMMUNE_TO_PLAYER | UNIT_FLAG_IMMUNE_TO_NPC here and never removes them.
        me->SetImmuneToPC(true);
        me->SetImmuneToNPC(true);
    }

    void Reset() override { }

    void JustEngagedWith(Unit* /*who*/) override { }

    void UpdateAI(uint32 /*diff*/) override
    {
        if (!_isSpawned)
        {
            std::list<Creature*> escortList;
            _nbScorpion = 0;

            me->GetCreatureListWithEntryInGrid(escortList, NPC_ULDAMAN_SCORPION, 30.0f);
            for (Creature* creature : escortList)
            {
                if (creature->IsAlive())
                    _nbScorpion++;
            }

            if (_nbScorpion == 0)
            {
                me->SetVisible(true);
                me->GetMotionMaster()->MovePoint(1, -164.3657f, 210.7687f, -49.572f);
                _isSpawned = true;
            }
        }

        UpdateVictim();
    }

private:
    uint32 _nbScorpion;
    bool _isSpawned;
    bool _hidden;
};

// 10258 - Awaken Vault Warder (Uldaman)
class classic_spell_uldaman_awaken_vault_warder : public SpellScript
{
    // VMaNGOS OnSetTargetMap: unMaxTargets = 2
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        if (targets.size() > 2)
            Trinity::Containers::RandomResize(targets, 2);
    }

    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(classic_spell_uldaman_awaken_vault_warder::FilterTargets, EFFECT_ALL, TARGET_UNIT_SRC_AREA_ENTRY);
    }
};

void AddSC_classic_uldaman()
{
    RegisterCreatureAI(classic_mob_annora);
    RegisterCreatureAI(classic_mob_jadespine_basilisk);
    RegisterCreatureAI(classic_mob_stone_keeper);
    RegisterGameObjectAI(classic_go_keystone_chamber);
    RegisterSpellScript(classic_spell_uldaman_awaken_vault_warder);
}
