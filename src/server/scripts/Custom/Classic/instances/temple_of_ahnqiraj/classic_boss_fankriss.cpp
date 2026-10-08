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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/silithus/temple_of_ahnqiraj/boss_fankriss.cpp (Nostalrius / ScriptDev2 lineage, GPL-2)
// Scripts: boss_fankriss, creature_spawn_fankriss, creature_vekniss_hatchling
/*
AQ40 - Fankriss the Unyielding
Reference: http://forum.nostalrius.org/viewtopic.php?f=51&t=35154#p246406
*/

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MapReference.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"
#include "classic_temple_of_ahnqiraj.h"
#include <algorithm>
#include <chrono>
#include <limits>
#include <random>
#include <utility>
#include <vector>

namespace
{
enum ClassicAQ40Fankriss : uint32
{
    SPELL_FANKRISS_MORTAL_WOUND = 25646,
    SPELL_FANKRISS_ENTANGLE_1   = 720,
    SPELL_FANKRISS_ENTANGLE_2   = 731,
    SPELL_FANKRISS_ENTANGLE_3   = 1121,
    //SPELL_SUMMON_WORM_1     = 518,
    //SPELL_SUMMON_WORM_2     = 25831,
    //SPELL_SUMMON_WORM_3     = 25832,
    SPELL_FANKRISS_SPAWN_ENRAGE = 26662,
    NPC_SPAWN_FANKRISS          = 15630,
};

struct SpawnLocation
{
    float m_fX, m_fY, m_fZ;
};

constexpr SpawnLocation aSummonWormLocs[3] =
{
    { -8076.53f, 1120.37f, -88.50f },
    { -8150.18f, 1146.97f, -87.45f },
    { -8023.31f, 1242.42f, -83.47f },
};
constexpr SpawnLocation hatchlingLocations[3] =
{
    {-8043.01f, 1254.20f, -84.19f},
    {-8003.00f, 1222.90f, -82.10f},
    {-8022.68f, 1150.08f, -89.33f}
};
constexpr float  PULL_DISTANCE           = 80.0f;
constexpr SpawnLocation pullCenter       = {-8074.88f, 1193.64f, -92.11f};
constexpr uint32 aIndex[3]               = { 0, 1, 2 };
constexpr size_t MAX_HATCHLINGS          = 20;   // Max hatchlings alive at any one time
constexpr size_t MAX_HATCHLINGS_PER_WEB  = 4;    // Max amount of hatchlings that can spawn at the same time, on one web. Its at least 4, might be 5.
constexpr uint32 HATCHLINGS_ATTACK_DELAY = 2500; // ~2.5sec in curse killvideo.
constexpr uint32 WORM_ENRAGE_BASE_TIMER  = 15000;
constexpr uint32 WORM_ENRAGE_ADDITION    = 5000;

// if defined, 2-MAX_HATCHLINGS_PER_WEB hatchlings are spawned in all 3 locations each time a player is ported,
// otherwise 2-MAX_HATCHLINGS_PER_WEB hatchlings will only spawn on the single location the player was ported to.
#define CLASSIC_AQ40_ALWAYS_HATCHLINGS_IN_3_LOCATIONS

/*

ALWAYS_HATCHLINGS_IN_3_LOCATIONS  when defined will spawn 2-MAX_HATCHLINGS_PER_WEB hatchlings in all 3 locations whenever one player is teleported.
                                  If its not defined 2-MAX_HATCHLINGS_PER_WEB  hatchlings only spawn in the location of the teleported player.
                                  In curse video it clearly looks like hatchlings spawn in all 3 locations, while in later videos they only spawn
                                  in the location of a teleported player.

MAX_HATCHLINGS                    defines how many hatchlings can be alive at the same time. I'm sure it was capped on retail, but not on the amount.

MAX_HATCHLINGS_PER_WEB            defines how many hatchlings can spawn spawn in one web location. Currently 4, possible it should be 5, but I have not seen it.

HATCHLINGS_ATTACK_DELAY           defines how long after hatchlings spawn until they go in combat with zone and attack the closest target.
                                  If the hatchling is already in combat when HATCHLINGS_ATTACK_DELAY runs out, they will not force themself on the closest target.

    If we want to force players periodically aoe down hatchlings instead of just offtanking them we can set this MAX_HATCHLINGS
    to a high value so it's not tankable. This will however require us to adjust down the frequency of players being teleported,
    OR not define ALWAYS_HATCHLINGS_IN_3_LOCATIONS. If we increase MAX_HATCHLINGS  to an amount that is not offtankable,
    the current configuration will most likely overrun the raid.
*/
std::vector<uint32> vIndex(aIndex, aIndex + 3);
}

struct classic_creature_spawn_fankriss : public ScriptedAI
{
    uint32 enrageTimer;
    classic_creature_spawn_fankriss(Creature* pCreature) : ScriptedAI(pCreature)
    {
        Reset();
    }

    void Reset() override
    {
        enrageTimer = 10000;
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (enrageTimer < diff)
        {
            //  World of Warcraft Client Patch 1.10.0 (2006-03-28)
            //  - Spawn of Fankriss will now enrage if not dealt with in a timely manner.
            me->CastSpell(me, SPELL_FANKRISS_SPAWN_ENRAGE, true);
            enrageTimer = std::numeric_limits<uint32>::max();
        }
        else
            enrageTimer -= diff;
    }
};

struct classic_creature_vekniss_hatchling : public ScriptedAI
{
    uint32 engageTimer;
    bool hasEngaged;
    bool wasAttacked;
    classic_creature_vekniss_hatchling(Creature* pCreature) : ScriptedAI(pCreature)
    {
        Reset();
    }

    void Reset() override
    {
        engageTimer = HATCHLINGS_ATTACK_DELAY;
        hasEngaged = false;
        wasAttacked = false;
    }

    // VMaNGOS AttackedBy()
    void DamageTaken(Unit* /*attacker*/, uint32& /*damage*/, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo*/) override
    {
        engageTimer = 0;
        wasAttacked = true;
    }

    void AttackStart(Unit* u) override
    {
        if (hasEngaged)
            ScriptedAI::AttackStart(u);
    }

    void MoveInLineOfSight(Unit* u) override
    {
        if (hasEngaged)
            ScriptedAI::MoveInLineOfSight(u);
    }

    void UpdateAI(uint32 diff) override
    {
        if (engageTimer <= diff && !hasEngaged)
        {
            hasEngaged = true;
            DoZoneInCombat();
            if (!wasAttacked)
            {
                if (Unit* pTarget = SelectTarget(SelectTargetMethod::MinDistance, 0))
                {
                    if (me->GetDistance(pTarget) > 200)
                        return; //avoid running after people far off in the instance somewhere

                    AddThreat(pTarget, 1.0f);
                    AttackStart(pTarget);
                }
            }
        }
        else if (!hasEngaged)
        {
            engageTimer -= diff;
            return;
        }

        if (!UpdateVictim())
            return;
    }
};

struct classic_boss_fankriss : public ScriptedAI
{
    classic_boss_fankriss(Creature* pCreature) :
        ScriptedAI(pCreature),
        worms(3)
    {
        m_pInstance = pCreature->GetInstanceScript();
        Reset();
    }

    InstanceScript* m_pInstance;

    uint32 m_uiMortalWoundTimer;
    uint32 m_uiEvadeCheckTimer;
    uint32 entangleRotationTimer;

    struct Worm
    {
        bool shouldSpawn = false;
        bool haveSpawned = false;
        uint32 enrageTimer = 0;
        uint32 spawnTimer = 0;
    };

    std::vector<Worm> worms;
    uint32 numWormsLastWave;

    struct HatchlingBatch
    {
        HatchlingBatch(GuidList& list) :
            attackTimer(0), hasAttacked(false), hatchlings(std::move(list))
        {}
        uint32 attackTimer;
        bool hasAttacked;
        GuidList hatchlings;
    };
    std::vector<HatchlingBatch> hatchlingVec;
    uint32 aliveHatchlings;
    std::vector<std::pair<uint32, SpawnLocation>> entangleSpells =
    {
        std::make_pair(uint32(SPELL_FANKRISS_ENTANGLE_1), hatchlingLocations[0]),
        std::make_pair(uint32(SPELL_FANKRISS_ENTANGLE_2), hatchlingLocations[1]),
        std::make_pair(uint32(SPELL_FANKRISS_ENTANGLE_3), hatchlingLocations[2]),
    };

    std::pair<uint32, bool> entangleTimers[3]; // timer and bool==true meaning the timer is ready for re-initialization

    void Reset() override
    {
        m_uiMortalWoundTimer    = urand(4000, 8000);

        worms[0].shouldSpawn    = true;
        worms[0].haveSpawned    = false;
        worms[0].spawnTimer     = urand(20000, 30000);
        worms[0].enrageTimer    = WORM_ENRAGE_BASE_TIMER;
        worms[1].shouldSpawn    = false;
        worms[2].shouldSpawn    = false;
        numWormsLastWave = 1;

        m_uiEvadeCheckTimer     = 2500;

        // Delaying first webs by 8 seconds, such that they cannot happen
        // until, at the earliest, 10 seconds after the pull.
        ReinitializeWebTimers(8000);
        aliveHatchlings = 0;
        hatchlingVec.clear();
        auto seed = std::chrono::system_clock::now().time_since_epoch().count();
        std::shuffle(vIndex.begin(), vIndex.end(), std::default_random_engine(uint32(seed)));
    }

    void MoveInLineOfSight(Unit* pWho) override
    {
        // Fankriss has an extremely large aggro radius
        if (pWho->IsPlayer() && !me->IsInCombat() && me->IsWithinDistInMap(pWho, 100.0f) && !pWho->HasAuraType(SPELL_AURA_FEIGN_DEATH)
            && me->IsValidAttackTarget(pWho))
        {
            AttackStart(pWho);
        }
        ScriptedAI::MoveInLineOfSight(pWho);
    }

    void JustEngagedWith(Unit* /*pWho*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_FANKRISS, IN_PROGRESS);
    }

    void JustReachedHome() override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_FANKRISS, FAIL);
    }

    void JustDied(Unit* /*pKiller*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_FANKRISS, DONE);
    }

    void JustSummoned(Creature* pSummoned) override
    {
        if (pSummoned->GetEntry() == NPC_VEKNISS_HATCHLING)
        {
            ++aliveHatchlings;
        }
        else if (pSummoned->GetEntry() == NPC_SPAWN_FANKRISS)
        {
            CreatureAI::DoZoneInCombat(pSummoned);
            if (Unit* pTarget = SelectTarget(SelectTargetMethod::Random, 0))
                if (pSummoned->IsAIEnabled())
                    pSummoned->AI()->AttackStart(pTarget);
        }
    }

    void SummonedCreatureDies(Creature* pSummoned, Unit* /*killer*/) override
    {
        if (pSummoned->GetEntry() == NPC_VEKNISS_HATCHLING)
        {
            for (HatchlingBatch& it : hatchlingVec)
                it.hatchlings.remove(pSummoned->GetGUID());
            --aliveHatchlings;
        }
    }

    void ReinitializeWebTimers(uint32 add = 0)
    {
        auto seed = std::chrono::system_clock::now().time_since_epoch().count();
        std::shuffle(entangleSpells.begin(), entangleSpells.end(), std::default_random_engine(uint32(seed)));
        // it's possible the longest cooldown should be able to reach more than the
        // 45 seconds max that it is here. Old nost code was 45sec. Cmangos use 75sec.
        // Should it also be possible that two players are webbed at the same time?
        // If not, we need shorter rand intervals and no overlap between the 3 webs.
        entangleTimers[0] = std::make_pair(urand(2000  + add, 18000 + add), false);
        entangleTimers[1] = std::make_pair(urand(15000 + add, 28000 + add), false);
        entangleTimers[2] = std::make_pair(urand(25000 + add, 45000 + add), false);
        entangleRotationTimer = 45000 + add;
    }

    size_t GetHatchlingSpawnAmount()
    {
        size_t spawnAmount = std::min(MAX_HATCHLINGS_PER_WEB, MAX_HATCHLINGS - aliveHatchlings);
        if (spawnAmount > 2)
            spawnAmount = (size_t)urand((uint32)2, (uint32)spawnAmount);
        return spawnAmount;
    }

    void SummonHatchling(GuidList& batch, SpawnLocation loc)
    {
        if (Creature* hatchling = me->SummonCreature(NPC_VEKNISS_HATCHLING, loc.m_fX, loc.m_fY, loc.m_fZ, 0.0f,
            TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 65s))
        {
            batch.push_back(hatchling->GetGUID());
        }
    }

    void HandleHatchlings(uint32 uiDiff)
    {
        bool reInitWebTimers = true;
        for (size_t i = 0; i < 3; i++)
        {
            bool& webCast = entangleTimers[i].second;
            if (webCast) continue;

            uint32& t = entangleTimers[i].first;
            if (t < uiDiff)
            {
                if (Unit* pTarget = SelectTarget(SelectTargetMethod::Random, 0, 0.0f, true))
                {
                    if (DoCast(pTarget, entangleSpells[i].first) == SPELL_CAST_OK)
                        webCast = true;
                }
            }
            else
                t -= uiDiff;

            if (webCast)
            {
                GuidList batch;
#ifdef CLASSIC_AQ40_ALWAYS_HATCHLINGS_IN_3_LOCATIONS
                for (size_t x = 0; x < 3; x++)
                {
                    SpawnLocation const& sLoc = entangleSpells[x].second;
                    size_t spawnAmount = GetHatchlingSpawnAmount();
                    for (size_t s = 0; s < spawnAmount; s++)
                        SummonHatchling(batch, sLoc);
                }
#else
                SpawnLocation const& sLoc = entangleSpells[i].second;
                size_t spawnAmount = GetHatchlingSpawnAmount();
                for (size_t s = 0; s < spawnAmount; s++)
                    SummonHatchling(batch, sLoc);
#endif
                hatchlingVec.emplace_back(batch);
            }
            else
            {
                reInitWebTimers = entangleTimers[i].second ? reInitWebTimers : false;
            }
        }

        if (reInitWebTimers && entangleRotationTimer < uiDiff)
            ReinitializeWebTimers();
        else
            entangleRotationTimer -= uiDiff;
    }

    void SummonWorm(SpawnLocation const& loc, uint32 enrageTimer)
    {
        if (Creature* pC = me->SummonCreature(NPC_SPAWN_FANKRISS, loc.m_fX, loc.m_fY, loc.m_fZ, 0.0f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 50s))
        {
            classic_creature_spawn_fankriss* ai = dynamic_cast<classic_creature_spawn_fankriss*>(pC->AI());
            if (!ai)
                return; // unable to cast spawn of fankriss AI to creature_spawn_fankrissAI
            ai->enrageTimer = enrageTimer;
        }
    }

    void UpdateAI(uint32 uiDiff) override
    {
        if (!me->IsInCombat())
        {
            for (MapReference const& ref : me->GetMap()->GetPlayers())
            {
                Player* pPlayer = ref.GetSource();
                if (pPlayer && pPlayer->IsAlive() && !pPlayer->IsGameMaster())
                {
                    // cheap way of quickly disgarding the check most of the time. No point
                    // where he can be pulled should be higher than this point (he is at roughly -100)
                    if (pPlayer->GetPositionZ() > -70.0f)
                        continue;

                    float distToPull = pPlayer->GetDistance(pullCenter.m_fX, pullCenter.m_fY, pullCenter.m_fZ);
                    if (distToPull < PULL_DISTANCE && pPlayer->IsWithinLOSInMap(me))
                        AttackStart(pPlayer);
                }
            }
        }

        if (!UpdateVictim())
            return;

        //
        // Mortal Wound
        if (m_uiMortalWoundTimer < uiDiff)
        {
            if (DoCastVictim(SPELL_FANKRISS_MORTAL_WOUND) == SPELL_CAST_OK)
                m_uiMortalWoundTimer = urand(4000, 8000);
        }
        else
            m_uiMortalWoundTimer -= uiDiff;

        bool allWormsSpawned = true;
        for (size_t i = 0; i < worms.size(); i++)
        {
            Worm& w = worms[i];
            if (w.shouldSpawn && !w.haveSpawned)
            {
                if (w.spawnTimer < uiDiff)
                {
                    w.haveSpawned = true;
                    SummonWorm(aSummonWormLocs[vIndex[i]], w.enrageTimer);
                }
                else
                {
                    allWormsSpawned = false;
                    w.spawnTimer -= uiDiff;
                }
            }
        }

        if (allWormsSpawned)
        {
            auto seed = std::chrono::system_clock::now().time_since_epoch().count();
            std::shuffle(vIndex.begin(), vIndex.end(), std::default_random_engine(uint32(seed)));
            uint32 spawnCount = urand(1, 3);
            for (size_t i = 0; i < 3; i++)
            {
                Worm& w = worms[i];
                w.haveSpawned = false;
                w.shouldSpawn = i < spawnCount;
                if (w.shouldSpawn)
                {
                    w.enrageTimer = WORM_ENRAGE_BASE_TIMER + WORM_ENRAGE_ADDITION * uint32(i); // 15sec for first, 20 for second and 25 sec for last.
                    /*  How long after each wave has fully spawned will next wave start spawning. (minTime,maxTime)
                        1x snake in prev wave = (18, 23)
                        2x snake in prev wave = (25  30)
                        3x snake in prev wave = (32  37)
                    */
                    if (i == 0)
                        w.spawnTimer = 18000 + ((numWormsLastWave - 1) * 7000) + urand(0, 5000);
                    else
                        w.spawnTimer = worms[i - 1].spawnTimer + urand(4000, 8000); // Each snake in a wave comes 4-8s after the previous one
                }
            }
            numWormsLastWave = spawnCount;
        }

        HandleHatchlings(uiDiff);

        // Evade in case Fankriss starts running after someone at zone in
        if (m_uiEvadeCheckTimer < uiDiff)
        {
            m_uiEvadeCheckTimer = 2500;
            if (me->GetPositionY() > 1400)
                EnterEvadeMode(EvadeReason::Boundary);
        }
        else
            m_uiEvadeCheckTimer -= uiDiff;
    }
};

void AddSC_classic_boss_fankriss()
{
    RegisterCreatureAI(classic_boss_fankriss);
    RegisterCreatureAI(classic_creature_spawn_fankriss);
    RegisterCreatureAI(classic_creature_vekniss_hatchling);
}
