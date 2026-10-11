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

#ifndef CLASSIC_DUNGEON_ENCOUNTER_TRACKER_H
#define CLASSIC_DUNGEON_ENCOUNTER_TRACKER_H

// Classic 1.60: the vanilla 5-player instance scripts (VMaNGOS ports) keep their own m_auiEncounter data and most of
// their bosses run SmartAI, so nothing ever called InstanceScript::SetBossState and the client got no ENCOUNTER_START,
// ENCOUNTER_END or BOSS_KILL. This tracker drives the TC boss states from the boss creatures themselves, without
// touching any boss AI:
//   IN_PROGRESS     while any tracked creature of a boss is alive and engaged (polled once per second)
//   FAIL + NOT_STARTED when that boss is no longer engaged without being killed (wipe / evade / despawn)
//   DONE            when a required creature of the boss dies and no other required creature of it is alive
// Group encounters (The Lost Dwarves, The Seven, ...) list several entries under one boss id; creatures that only
// start an encounter (Sneed's Shredder, Gyth, ...) are flagged EngageOnly and do not have to die.
//
// Usage in an InstanceScript: SetBossNumber(N) + LoadDungeonEncounterData(...) in the constructor, a tracker member
// constructed with (*this, bossEntries), and calls from OnCreatureCreate / OnCreatureRemove / OnUnitDeath / Update.

#include "Creature.h"
#include "InstanceScript.h"
#include "Map.h"
#include "ObjectGuid.h"
#include <span>
#include <vector>

enum ClassicDungeonBossFlags : uint8
{
    CLASSIC_DUNGEON_BOSS_REQUIRED    = 0,   // must die for the encounter to be DONE
    CLASSIC_DUNGEON_BOSS_ENGAGE_ONLY = 1    // only starts the encounter (mount / bodyguard / vehicle)
};

struct ClassicDungeonBossEntry
{
    uint32 BossId;
    uint32 Entry;
    uint8 Flags = CLASSIC_DUNGEON_BOSS_REQUIRED;
};

class ClassicDungeonEncounterTracker
{
public:
    ClassicDungeonEncounterTracker(InstanceScript& script, std::span<ClassicDungeonBossEntry const> entries)
        : _script(script), _entries(entries) { }

    void OnCreatureCreate(Creature* creature)
    {
        ClassicDungeonBossEntry const* entry = FindEntry(creature->GetEntry());
        if (!entry)
            return;

        for (Tracked const& tracked : _tracked)
            if (tracked.Guid == creature->GetGUID())
                return;

        _tracked.push_back({ creature->GetGUID(), entry->BossId, entry->Flags });
    }

    void OnCreatureRemove(Creature* creature)
    {
        std::erase_if(_tracked, [creature](Tracked const& tracked) { return tracked.Guid == creature->GetGUID(); });
    }

    void OnUnitDeath(Unit* unit)
    {
        Creature* creature = unit->ToCreature();
        if (!creature)
            return;

        // by guid first: some bosses change entry in combat (Dathrohan -> Balnazzar)
        Tracked const* dead = nullptr;
        for (Tracked const& tracked : _tracked)
            if (tracked.Guid == creature->GetGUID())
                dead = &tracked;

        if (!dead || (dead->Flags & CLASSIC_DUNGEON_BOSS_ENGAGE_ONLY))
            return;

        uint32 const bossId = dead->BossId;
        if (_script.GetBossState(bossId) == DONE)
            return;

        for (Tracked const& tracked : _tracked)
        {
            if (tracked.BossId != bossId || tracked.Guid == creature->GetGUID() || (tracked.Flags & CLASSIC_DUNGEON_BOSS_ENGAGE_ONLY))
                continue;

            if (Creature* other = _script.instance->GetCreature(tracked.Guid))
                if (other->IsAlive())
                    return;
        }

        if (_script.GetBossState(bossId) == TO_BE_DECIDED)
            _script.SetBossState(bossId, NOT_STARTED);
        _script.SetBossState(bossId, DONE);
    }

    void Update(uint32 diff)
    {
        if (_checkTimer > diff)
        {
            _checkTimer -= diff;
            return;
        }
        _checkTimer = 1000;

        for (uint32 bossId = 0; bossId < _script.GetEncounterCount(); ++bossId)
        {
            EncounterState state = _script.GetBossState(bossId);
            if (state == DONE)
                continue;

            // instance saved before these boss states existed: initialize silently
            if (state == TO_BE_DECIDED)
            {
                _script.SetBossState(bossId, NOT_STARTED);
                state = NOT_STARTED;
            }

            bool engaged = false;
            for (Tracked const& tracked : _tracked)
            {
                if (tracked.BossId != bossId)
                    continue;

                if (Creature* creature = _script.instance->GetCreature(tracked.Guid))
                    if (creature->IsAlive() && creature->IsEngaged())
                        engaged = true;
            }

            // a kill sets DONE synchronously from OnUnitDeath, so not engaged here means wipe, evade or despawn
            if (engaged && state != IN_PROGRESS)
                _script.SetBossState(bossId, IN_PROGRESS);
            else if (!engaged && state == IN_PROGRESS)
            {
                _script.SetBossState(bossId, FAIL);
                _script.SetBossState(bossId, NOT_STARTED);
            }
        }
    }

private:
    struct Tracked
    {
        ObjectGuid Guid;
        uint32 BossId;
        uint8 Flags;
    };

    ClassicDungeonBossEntry const* FindEntry(uint32 entry) const
    {
        for (ClassicDungeonBossEntry const& bossEntry : _entries)
            if (bossEntry.Entry == entry)
                return &bossEntry;
        return nullptr;
    }

    InstanceScript& _script;
    std::span<ClassicDungeonBossEntry const> _entries;
    std::vector<Tracked> _tracked;
    uint32 _checkTimer = 1000;
};

#endif
