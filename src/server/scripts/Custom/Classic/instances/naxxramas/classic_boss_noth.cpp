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

// Classic 1.60 port of VMaNGOS src/scripts/eastern_kingdoms/eastern_plaguelands/naxxramas/boss_noth.cpp (GPL-2)
// Scripts: boss_noth, spell_noth_curse_of_the_plaguebringer

#include "ScriptMgr.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "EventMap.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Random.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "TemporarySummon.h"
#include "ThreatManager.h"
#include "classic_naxxramas.h"
#include "classic_script_text.h"
#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <list>
#include <vector>

// https://pastebin.com/AXPeUXub
// pastbin link above has various noted timers and sources. Not much useful, but it's some of my notes
// from when researching the encounter.

// Regarding summoning add spells, there are a few spells which summon both two types of mobs,
// or multiple of the same mob in different locations with the same spell. However
// our core does not support multiple spell target coordinates due to limitations with the DB, so
// we have to use the spells which summon a single creature.

namespace
{
enum ClassicNaxxNothSpells : uint32
{
    SPELL_NOTH_TP_CENTER           = 29231,
    SPELL_NOTH_TP_BALC             = 29216,
    SPELL_NOTH_CRIPPLE             = 29212, // used on players where noth blinked FROM
    SPELL_NOTH_CURSE_PLAGUEBRINGER = 29213,
    SPELL_NOTH_IMMUNE_ALL          = 29230, // used on TP to balc

    SPELL_NOTH_BLINK_1 = 29208,
    SPELL_NOTH_BLINK_2 = 29209,
    SPELL_NOTH_BLINK_3 = 29210,
    SPELL_NOTH_BLINK_4 = 29211,

    SPELL_NOTH_SUM_WARR_SW = 29247,
    SPELL_NOTH_SUM_WARR_NW = 29248,
    SPELL_NOTH_SUM_WARR_NE = 29249,

    SPELL_NOTH_SUM_CHAMP_SW1 = 29217,
    SPELL_NOTH_SUM_CHAMP_SW2 = 29224,
    SPELL_NOTH_SUM_CHAMP_SW3 = 29225,
    SPELL_NOTH_SUM_CHAMP_SW4 = 29227,

    SPELL_NOTH_SUM_CHAMP_W   = 29238,

    SPELL_NOTH_SUM_CHAMP_NW1 = 29255,
    SPELL_NOTH_SUM_CHAMP_NW2 = 29267,
    SPELL_NOTH_SUM_CHAMP_NW3 = 29257,

    SPELL_NOTH_SUM_CHAMP_NE1 = 29258,
    SPELL_NOTH_SUM_CHAMP_NE2 = 29262,

    SPELL_NOTH_SUM_GUARD_NE  = 29226,
    SPELL_NOTH_SUM_GUARD_NW  = 29239,
    SPELL_NOTH_SUM_GUARD_SW1 = 29256,
    SPELL_NOTH_SUM_GUARD_SW2 = 29268
};

uint32 const NaxxNothChampionSpells[10] =
{
    //g1
    SPELL_NOTH_SUM_CHAMP_SW1,
    SPELL_NOTH_SUM_CHAMP_SW2,
    SPELL_NOTH_SUM_CHAMP_SW3,
    SPELL_NOTH_SUM_CHAMP_SW4,
    //g2
    SPELL_NOTH_SUM_CHAMP_W,
    SPELL_NOTH_SUM_CHAMP_NW1,
    SPELL_NOTH_SUM_CHAMP_NW2,
    SPELL_NOTH_SUM_CHAMP_NW3,
    //g3
    SPELL_NOTH_SUM_CHAMP_NE1,
    SPELL_NOTH_SUM_CHAMP_NE2
};

uint8 const naxxNothG1Start = 0, naxxNothG1Size = 4;
uint8 const naxxNothG2Start = 4, naxxNothG2Size = 4;
uint8 const naxxNothG3Start = 8, naxxNothG3Size = 2;

// BroadcastText ids
enum ClassicNaxxNothTexts : uint32
{
    SAY_NOTH_AGGRO1                     = 13061,
    SAY_NOTH_AGGRO2                     = 13062,
    SAY_NOTH_AGGRO3                     = 13063,
    SAY_NOTH_SUMMON                     = 13067,

    SAY_NOTH_SLAY1                      = 13065,
    SAY_NOTH_SLAY2                      = 13066,
    SAY_NOTH_DEATH                      = 13064
};

enum ClassicNaxxNothNPCs : uint32
{
    NPC_NOTH_PLAGUED_GUARDIAN  = 16981,
    NPC_NOTH_PLAGUED_CONSTRUCT = 16982, // unknown if this was ever used
    NPC_NOTH_PLAGUED_CHAMPION  = 16983,
    NPC_NOTH_PLAGUED_WARRIOR   = 16984
};

enum ClassicNaxxNothEvents : uint32
{
    EVENT_NOTH_BLINK = 1,
    EVENT_NOTH_CURSE,
    EVENT_NOTH_TP_BALC,
    EVENT_NOTH_TP_GROUND,
    EVENT_NOTH_RMV_INVULN,
    EVENT_NOTH_BALC_ADDS,
    EVENT_NOTH_WARRIORS
};
}

struct classic_boss_noth : public ScriptedAI
{
    classic_boss_noth(Creature* creature) : ScriptedAI(creature), m_pInstance(GetClassicNaxxInstance(creature))
    {
        isOnBalc = false;
        phaseCounter = 0;
        killSayCooldown = 5000;
    }

    classic_instance_naxxramas_InstanceScript* m_pInstance;
    uint8 phaseCounter;
    uint32 killSayCooldown;
    EventMap m_events;
    bool isOnBalc;

    void Reset() override
    {
        isOnBalc = false;
        phaseCounter = 0;
        m_events.Reset();
        killSayCooldown = 5000;
    }

    void JustReachedHome() override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_NOTH, FAIL);

        std::list<Creature*> clist;
        for (uint32 entry : { NPC_NOTH_PLAGUED_GUARDIAN, NPC_NOTH_PLAGUED_CONSTRUCT, NPC_NOTH_PLAGUED_CHAMPION, NPC_NOTH_PLAGUED_WARRIOR })
            me->GetCreatureListWithEntryInGrid(clist, entry, 150.0f);
        for (Creature* pC : clist)
        {
            pC->DespawnOrUnsummon(); // VMaNGOS: DeleteLater()
        }
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        DoZoneInCombat();

        m_events.ScheduleEvent(EVENT_NOTH_CURSE,    Seconds(urand(8, 12)));
        m_events.ScheduleEvent(EVENT_NOTH_BLINK,    Seconds(urand(30, 40)));
        m_events.ScheduleEvent(EVENT_NOTH_WARRIORS, Seconds(10));
        m_events.ScheduleEvent(EVENT_NOTH_TP_BALC,  Seconds(90));

        ClassicScriptText(urand(SAY_NOTH_AGGRO1, SAY_NOTH_AGGRO3), me);

        if (m_pInstance)
            m_pInstance->SetData(TYPE_NOTH, IN_PROGRESS);
    }

    void SpawnWarriorsAndRepeatEvent()
    {
        ClassicScriptText(SAY_NOTH_SUMMON, me);

        DoCast(me, SPELL_NOTH_SUM_WARR_SW, true);
        DoCast(me, SPELL_NOTH_SUM_WARR_NW, true);
        DoCast(me, SPELL_NOTH_SUM_WARR_NE, true);

        m_events.Repeat(Seconds(30));
    }

    void BlinkAndRepeatEvent()
    {
        static uint32 const auiSpellBlink[4] =
        {
            SPELL_NOTH_BLINK_1, SPELL_NOTH_BLINK_2, SPELL_NOTH_BLINK_3, SPELL_NOTH_BLINK_4
        };

        // VMaNGOS CF_TRIGGERED | CF_FORCE_CAST
        DoCast(me, SPELL_NOTH_CRIPPLE, true);
        DoCast(me, auiSpellBlink[urand(0, 3)], true);
        m_events.Repeat(Seconds(urand(30, 40)));
        ResetThreatList();
        if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
            AttackStart(target);
    }

    void CurseAndRepeatEvent()
    {
        // From what I can understand, the plague should always target the current target,
        // and will then also target the next 19 closest players to the current target
        if (Unit* pTarget = me->GetVictim())
        {
            DoCast(pTarget, SPELL_NOTH_CURSE_PLAGUEBRINGER);
            m_events.Repeat(Seconds(urand(50, 60))); //It's somewhere around 50seconds+
        }
        else
        {
            m_events.Repeat(Milliseconds(100));
        }
    }

    void TeleportToBalc()
    {
        // VMaNGOS CF_TRIGGERED | CF_FORCE_CAST
        if (DoCast(me, SPELL_NOTH_TP_BALC, true) != SPELL_CAST_OK)
        {
            m_events.Repeat(Milliseconds(100)); // try again
            return;
        }

        isOnBalc = true;
        m_events.Reset();
        // 70, 95 and 120 seconds for 1st, 2nd and 3rd balc phase respectively.
        m_events.ScheduleEvent(EVENT_NOTH_TP_GROUND, Seconds(70 + 25 * phaseCounter));

        Milliseconds first_spawn = Seconds(urand(5, 7));
        m_events.ScheduleEvent(EVENT_NOTH_BALC_ADDS, first_spawn);
        switch (phaseCounter)
        {
            case 0:
                m_events.ScheduleEvent(EVENT_NOTH_BALC_ADDS, first_spawn + Seconds(25 + urand(0, 5)));
                break;
            case 1:
                m_events.ScheduleEvent(EVENT_NOTH_BALC_ADDS, first_spawn + Seconds(44 + urand(0, 5)));
                break;
            case 2:
                m_events.ScheduleEvent(EVENT_NOTH_BALC_ADDS, first_spawn + Seconds(57 + urand(0, 5)));
                break;
        }

        me->CastSpell(me, SPELL_NOTH_IMMUNE_ALL, true);

        me->GetMotionMaster()->MoveIdle();
        DoStopAttack();
    }

    void TeleportFromBalc()
    {
        if (DoCast(me, SPELL_NOTH_TP_CENTER, true) != SPELL_CAST_OK)
        {
            m_events.Repeat(Milliseconds(100)); // try again
            return;
        }
        m_events.Reset();
        m_events.ScheduleEvent(EVENT_NOTH_RMV_INVULN, Seconds(2));
    }

    void Summon4Champions()
    {
        // We need to spawn 4 champions. We have 10 different possible locations,
        // and the adds need to be somewhat evenly spread out, yet somewhat randomized.
        std::vector<uint32> champs(std::begin(NaxxNothChampionSpells), std::end(NaxxNothChampionSpells));

        // First selecting one random champ from each of the 3 main groups
        uint32 champ1 = champs[urand(naxxNothG1Start, naxxNothG1Start + naxxNothG1Size - 1)];
        uint32 champ2 = champs[urand(naxxNothG2Start, naxxNothG2Start + naxxNothG2Size - 1)];
        uint32 champ3 = champs[urand(naxxNothG3Start, naxxNothG3Start + naxxNothG3Size - 1)];

        // Moving the selected champions to the end of the vector
        auto nend = std::remove(champs.begin(), champs.end(), champ1);
        nend = std::remove(champs.begin(), nend, champ2);
        nend = std::remove(champs.begin(), nend, champ3);

        // Summoning the selected 3 guardians
        DoCast(me, champ1, true);
        DoCast(me, champ2, true);
        DoCast(me, champ3, true);

        // Choosing the final champion at random between the remaining 7 locations
        // (VMaNGOS upper bound is inclusive: index 7 can also be picked; kept as is)
        uint32 champ4Idx = urand(0, uint32(std::distance(champs.begin(), nend)));
        uint32 champ4 = champs[champ4Idx];
        DoCast(me, champ4, true);
    }

    void Summon2Guardians()
    {
        // Choose one of two locations south-west, and either the north-east or north-west location
        DoCast(me, urand(0, 1) ? SPELL_NOTH_SUM_GUARD_SW1 : SPELL_NOTH_SUM_GUARD_SW2, true);
        DoCast(me, urand(0, 1) ? SPELL_NOTH_SUM_GUARD_NE : SPELL_NOTH_SUM_GUARD_NW, true);
    }

    void Summon3Constructs()
    {
        me->SummonCreature(NPC_NOTH_PLAGUED_CONSTRUCT, 2649.0f, -3456.0f, 264.0f, 5.0f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, Milliseconds(25000));
        me->SummonCreature(NPC_NOTH_PLAGUED_CONSTRUCT, 2727.0f, -3458.0f, 263.5f, 3.8f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, Milliseconds(25000));
        me->SummonCreature(NPC_NOTH_PLAGUED_CONSTRUCT, 2727.0f, -3534.0f, 268.0f, 0.1f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, Milliseconds(25000));
    }

    void SpawnBalcAdds()
    {
        ClassicScriptText(SAY_NOTH_SUMMON, me);
        switch (phaseCounter)
        {
            case 0:
                Summon4Champions();
                break;
            case 1:
                Summon4Champions();
                Summon2Guardians();
                break;
            default: // third balc phase and onwards
                Summon4Champions();
                Summon2Guardians();
                Summon3Constructs();
                break;
        }
    }

    void OnRemoveVulnerability()
    {
        isOnBalc = false;

        m_events.Reset();
        m_events.ScheduleEvent(EVENT_NOTH_BLINK, Seconds(urand(2, 10)));
        m_events.ScheduleEvent(EVENT_NOTH_CURSE, Seconds(urand(2, 10)));
        m_events.ScheduleEvent(EVENT_NOTH_WARRIORS, Seconds(urand(2, 10)));
        me->RemoveAurasDueToSpell(SPELL_NOTH_IMMUNE_ALL);

        ResetThreatList();
        if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
            AttackStart(target);

        // note that we increment phaseCounter here
        switch (++phaseCounter)
        {
            // case 0: won't happen, its initialized from Aggro()
            case 1:
                m_events.ScheduleEvent(EVENT_NOTH_TP_BALC, Seconds(110));
                break;
            case 2:
                m_events.ScheduleEvent(EVENT_NOTH_TP_BALC, Seconds(180));
                break;
            default:
                // No good sources on duration of 4th ground phase, all guides explain it as a wipe
                // if you don't kill him during the 3rd ground phase. We'll just repeat previous phase logic
                // after this. It's highly unlikely that any guild get to this stage without killing him or wiping.
                m_events.ScheduleEvent(EVENT_NOTH_TP_BALC, Seconds(180));
                break;
        }
    }

    void SummonedCreatureDies(Creature* unit, Unit* /*killer*/) override
    {
        unit->DespawnOrUnsummon(Milliseconds(3000));
    }

    void JustSummoned(Creature* pSummoned) override
    {
        CreatureAI::DoZoneInCombat(pSummoned);
    }

    void KilledUnit(Unit* /*victim*/) override
    {
        if (!killSayCooldown)
        {
            ClassicScriptText(urand(0, 1) ? SAY_NOTH_SLAY1 : SAY_NOTH_SLAY2, me);
            killSayCooldown = 5000;
        }
    }

    void JustDied(Unit* /*killer*/) override
    {
        ClassicScriptText(SAY_NOTH_DEATH, me);

        if (m_pInstance)
            m_pInstance->SetData(TYPE_NOTH, DONE);
    }

    void AttackStart(Unit* who) override
    {
        if (!isOnBalc)
            ScriptedAI::AttackStart(who);
    }

    void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo*/) override
    {
        if (isOnBalc && damage > 0)
            damage = 0;
    }

    void UpdateAI(uint32 diff) override
    {
        if (!isOnBalc)
        {
            if (!UpdateVictim())
                return;
            if (m_pInstance && !m_pInstance->HandleEvadeOutOfHome(me))
                return;
        }
        else
        {
            // Will make him TP down rather thank walk through the air on a reset/wipe
            if (me->GetThreatManager().IsThreatListEmpty())
            {
                if (m_events.GetTimeUntilEvent(EVENT_NOTH_RMV_INVULN) > Milliseconds(2000))
                {
                    TeleportFromBalc();
                }
            }
        }

        killSayCooldown -= std::min(killSayCooldown, diff);
        m_events.Update(diff);
        while (uint32 eventId = m_events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_NOTH_BLINK:
                    BlinkAndRepeatEvent();
                    break;
                case EVENT_NOTH_CURSE:
                    CurseAndRepeatEvent();
                    break;
                case EVENT_NOTH_TP_BALC:
                    TeleportToBalc();
                    break;
                case EVENT_NOTH_TP_GROUND:
                    TeleportFromBalc();
                    break;
                case EVENT_NOTH_RMV_INVULN:
                    OnRemoveVulnerability();
                    break;
                case EVENT_NOTH_BALC_ADDS:
                    SpawnBalcAdds();
                    break;
                case EVENT_NOTH_WARRIORS:
                    SpawnWarriorsAndRepeatEvent();
                    break;
            }
        }

        // melee: TC master auto-melee (no victim while on the balcony)
    }
};

// 29213 - Curse of the Plaguebringer (Naxxramas, Noth the Plaguebringer)
// VMaNGOS OnSetTargetMap: selectClosestTargets = true (the MaxAffectedTargets closest units are chosen instead of random ones).
// 1.60 client data: effect 0 targets 22/15 (TARGET_SRC_CASTER / TARGET_UNIT_SRC_AREA_ENEMY).
class classic_spell_noth_curse_of_the_plaguebringer : public SpellScript
{
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        uint32 const maxTargets = GetSpellInfo()->MaxAffectedTargets;
        if (!maxTargets || targets.size() <= maxTargets)
            return;

        // Sorting by distance and cutting the list here makes the core's RandomResize a no-op.
        targets.sort(Trinity::ObjectDistanceOrderPred(GetCaster()));
        targets.resize(maxTargets);
    }

    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(classic_spell_noth_curse_of_the_plaguebringer::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENEMY);
    }
};

void AddSC_classic_boss_noth()
{
    RegisterCreatureAI(classic_boss_noth);
    RegisterSpellScript(classic_spell_noth_curse_of_the_plaguebringer);
}
