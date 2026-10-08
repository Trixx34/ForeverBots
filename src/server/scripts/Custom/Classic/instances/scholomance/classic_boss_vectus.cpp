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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/western_plaguelands/scholomance/boss_vectus.cpp
// (ScriptDev2 lineage, GPL-2). Dawn's Gambit event (Chakor@Nostalrius).
// Ported: boss_vectus, npc_scholomance_student

#include "ScriptMgr.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "CreatureGroups.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "classic_scholomance.h"
#include <list>

using namespace ClassicScholomance;

namespace
{
    enum ClassicVectusData : uint32
    {
        NPC_STUDENT                                     = 10475,
        NPC_MARDUK_BLACKPOOL                            = 10433,

        VECTUS_SPEECH_GAMBIT_EVENT_START                = 6883,

        VECTUS_FACTION_MONSTER                          = 16,   // VMaNGOS FACTION_MONSTER (TC's FACTION_MONSTER is 14)

        GO_DAWN_S_GAMBIT                                = 177304,

        SPELL_VIEWING_ROOM_STUDENT_TRANSFORM_EFFECT     = 18115,    // transforms the elite students into skeletons
        SPELL_FLAMESTRIKE                               = 18399,
        SPELL_BLAST_WAVE                                = 16046
    };

    // VMaNGOS creature guid 48949 (spawn id = 20000000 + VMaNGOS guid)
    constexpr ObjectGuid::LowType CLASSIC_SCHOLO_STUDENT_RESPAWN_SPAWN_ID = 20000000 + 48949;

    void ClassicScholoRemoveFromFormation(Creature* creature)
    {
        if (CreatureGroup* group = creature->GetFormation())
            FormationMgr::RemoveCreatureFromGroup(group, creature);
    }
}

struct classic_boss_vectus : public ScriptedAI
{
    classic_boss_vectus(Creature* creature) : ScriptedAI(creature)
    {
        _startedDialogue = false;
        _eventGambitDone = false;
        _eventGambitStart = false;
        _findGambit = false;
        _gambitEventTimer = 0;
        Initialize();
    }

    void Initialize()
    {
        _flameStrikeTimer = 2000;
        _blastWaveTimer = 14000;
        _fullAggroDone = false;
    }

    void Reset() override
    {
        Initialize();
    }

    void MoveInLineOfSight(Unit* who) override
    {
        if (!_startedDialogue)
        {
            if (who->IsPlayer() && me->IsWithinDistInMap(who, 32.0f) && me->IsWithinLOSInMap(who))
            {
                // start the (creature_addon) waypoint path
                me->SetDefaultMovementType(WAYPOINT_MOTION_TYPE);
                me->GetMotionMaster()->Initialize();
                _startedDialogue = true;
            }
        }

        ScriptedAI::MoveInLineOfSight(who);
    }

    void UpdateAI(uint32 diff) override
    {
        // Chakor@Nostalrius : Dawn's Gambit event
        if (!_eventGambitDone)
        {
            if (_gambitEventTimer < diff)
            {
                if (!_eventGambitStart)
                {
                    if (!_findGambit)
                    {
                        if (GameObject* gambit = GetClosestGameObjectWithEntry(me, GO_DAWN_S_GAMBIT, 100.0f))
                        {
                            _gambitGUID = gambit->GetGUID();
                            _gambitEventTimer = 2000;
                            _findGambit = true;
                        }
                    }
                    else
                    {
                        if (GameObject* gambit = ObjectAccessor::GetGameObject(*me, _gambitGUID))
                            gambit->DespawnOrUnsummon();   // VMaNGOS GameObject::Delete()
                        _gambitEventTimer = 12000;
                        _eventGambitStart = true;
                    }
                }
                else
                {
                    // TODO(classic): VMaNGOS also calls AIM_Initialize() on Marduk, the students and Vectus after the faction
                    // change (re-creates their AI); not done here (TC would destroy the running AI).
                    if (Creature* marduk = me->FindNearestCreature(NPC_MARDUK_BLACKPOOL, 100.0f))
                    {
                        marduk->SetFaction(VECTUS_FACTION_MONSTER);
                        marduk->SetReactState(REACT_AGGRESSIVE);
                    }

                    std::list<Creature*> creatures;
                    me->GetCreatureListWithEntryInGrid(creatures, NPC_STUDENT, 100.0f);
                    for (Creature* creature : creatures)
                    {
                        creature->CastSpell(creature, SPELL_VIEWING_ROOM_STUDENT_TRANSFORM_EFFECT, true);
                        ClassicScholoRemoveFromFormation(creature);

                        creature->SetFaction(VECTUS_FACTION_MONSTER);
                        creature->SetReactState(REACT_AGGRESSIVE);
                    }

                    me->Yell(VECTUS_SPEECH_GAMBIT_EVENT_START);
                    me->SetFaction(VECTUS_FACTION_MONSTER);
                    me->SetReactState(REACT_AGGRESSIVE);

                    _eventGambitDone = true;
                }
            }
            else
                _gambitEventTimer -= diff;
        }

        if (!UpdateVictim())
            return;

        // NOSTALRIUS: aggro the whole room when he is pulled.
        if (!_fullAggroDone)
        {
            std::list<Creature*> creatures;
            me->GetCreatureListWithEntryInGrid(creatures, NPC_STUDENT, 100.0f);
            for (Creature* creature : creatures)
            {
                creature->SetFaction(VECTUS_FACTION_MONSTER);
                if (creature->AI())
                    creature->AI()->AttackStart(me->GetVictim());
            }
            _fullAggroDone = true;
        }

        if (_flameStrikeTimer < diff)
        {
            DoCastSelf(SPELL_FLAMESTRIKE);
            _flameStrikeTimer = 30000;
        }
        else
            _flameStrikeTimer -= diff;

        if (_blastWaveTimer < diff)
        {
            DoCastVictim(SPELL_BLAST_WAVE);
            _blastWaveTimer = 12000;
        }
        else
            _blastWaveTimer -= diff;
    }

private:
    uint32 _flameStrikeTimer;
    uint32 _blastWaveTimer;
    uint32 _gambitEventTimer;
    bool _fullAggroDone;
    bool _eventGambitDone;
    bool _eventGambitStart;
    bool _findGambit;
    ObjectGuid _gambitGUID;
    bool _startedDialogue;
};

/*######
## npc_scholomance_student
######*/

struct classic_npc_scholomance_student : public ScriptedAI
{
    classic_npc_scholomance_student(Creature* creature) : ScriptedAI(creature), _instance(creature->GetInstanceScript()), _isTransformed(false) { }

    void Reset() override { }

    void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
    {
        if (spellInfo->Id == SPELL_VIEWING_ROOM_STUDENT_TRANSFORM_EFFECT)
        {
            _isTransformed = true;

            if (me->GetSpawnId() == CLASSIC_SCHOLO_STUDENT_RESPAWN_SPAWN_ID)
            {
                // TODO(classic): VMaNGOS ForcedDespawn() + Respawn() keeps this AI (and _isTransformed) alive; in TC the respawned
                // creature gets a fresh AI, so the JustAppeared() aura re-apply below will not see the flag.
                me->DespawnOrUnsummon(0ms, 1s);
            }
        }
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        // set the viewing room and Marduk and Vectus to hostile on aggro
        std::list<Creature*> creatures;
        me->GetCreatureListWithEntryInGrid(creatures, NPC_STUDENT, 100.0f);
        for (Creature* creature : creatures)
            creature->SetFaction(VECTUS_FACTION_MONSTER);

        if (Creature* marduk = me->FindNearestCreature(NPC_MARDUK_BLACKPOOL, 100.0f))
            marduk->SetFaction(VECTUS_FACTION_MONSTER);

        if (Creature* vectus = me->FindNearestCreature(NPC_VECTUS, 100.0f))
            vectus->SetFaction(VECTUS_FACTION_MONSTER);
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (_isTransformed)
        {
            if (!me->FindNearestCreature(NPC_STUDENT, 100.0f, true) && _instance)
            {
                // unlink the two bosses
                Creature* vectus = ObjectAccessor::GetCreature(*me, _instance->GetGuidData(DATA_VECTUS));
                Creature* marduke = ObjectAccessor::GetCreature(*me, _instance->GetGuidData(DATA_MARDUKE));
                if (vectus && marduke)
                {
                    if (CreatureGroup* group = vectus->GetFormation())
                    {
                        if (group == marduke->GetFormation())
                        {
                            ClassicScholoRemoveFromFormation(vectus);
                            ClassicScholoRemoveFromFormation(marduke);
                        }
                    }
                }
            }

            me->DespawnOrUnsummon();
        }
    }

    void JustAppeared() override
    {
        ScriptedAI::JustAppeared();

        if (_isTransformed)
            me->AddAura(SPELL_VIEWING_ROOM_STUDENT_TRANSFORM_EFFECT, me);
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        ScriptedAI::EnterEvadeMode(why);

        if (_isTransformed)
            me->AddAura(SPELL_VIEWING_ROOM_STUDENT_TRANSFORM_EFFECT, me);
    }

    void UpdateAI(uint32 /*diff*/) override
    {
        UpdateVictim();
    }

private:
    InstanceScript* _instance;
    bool _isTransformed;
};

void AddSC_classic_boss_vectus()
{
    RegisterCreatureAI(classic_boss_vectus);
    RegisterCreatureAI(classic_npc_scholomance_student);
}
