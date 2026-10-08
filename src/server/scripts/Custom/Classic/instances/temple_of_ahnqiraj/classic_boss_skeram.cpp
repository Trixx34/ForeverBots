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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/silithus/temple_of_ahnqiraj/boss_skeram.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: boss_skeram

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "classic_script_text.h"
#include "classic_temple_of_ahnqiraj.h"
#include <algorithm>
#include <list>

namespace
{
enum ClassicAQ40Skeram : uint32
{
    SAY_SKERAM_AGGRO_1          = 11445,
    SAY_SKERAM_SLAY_1           = 11446,
    SAY_SKERAM_DEATH            = 11447,
    // SAY_AGGRO_2 = -1531001, SAY_AGGRO_3 = -1531002, SAY_SLAY_2 = -1531004, SAY_SLAY_3 = -1531005, SAY_SPLIT = -1531006:
    // script_texts (no broadcast text), yells, see ClassicSkeramScriptTexts
    SOUND_SKERAM_AGGRO_2        = 8616,
    SOUND_SKERAM_AGGRO_3        = 8621,
    SOUND_SKERAM_SLAY_2         = 8619,
    SOUND_SKERAM_SLAY_3         = 8620,
    SOUND_SKERAM_SPLIT          = 8618,

    SPELL_ARCANE_EXPLOSION      = 26192,
    SPELL_EARTH_SHOCK           = 26194,

    SPELL_TRUE_FULFILLMENT      = 785,
    SPELL_TF_HASTE              = 2313,
    SPELL_TF_MOD_HEAL           = 26525,
    SPELL_TF_IMMUNITY           = 26526,
    SPELL_TF_CANCEL             = 26589,

    SPELL_BLINK_0               = 4801,
    SPELL_BLINK_1               = 8195,
    SPELL_BLINK_2               = 20449,

    SPELL_SUMMON_IMAGES         = 747
};

// FULFILLMENT_RANGE 90.0f (unused in VMaNGOS)

char const* const TEXT_SKERAM_AGGRO_2 = "Cower mortals! The age of darkness is at hand.";
char const* const TEXT_SKERAM_AGGRO_3 = "Tremble! The end is upon you.";
char const* const TEXT_SKERAM_SLAY_2  = "Spineless wretches! You will drown in rivers of blood!";
char const* const TEXT_SKERAM_SLAY_3  = "The screams of the dying will fill the air. A symphony of terror is about to begin!";
char const* const TEXT_SKERAM_SPLIT   = "Prepare for the return of the ancient ones!";

void ClassicSkeramYell(Creature* me, char const* text, uint32 sound)
{
    me->Yell(text, LANG_UNIVERSAL);
    me->PlayDirectSound(sound);
}
}

/**
* Source videos for Skeram behaviour:
* https://www.youtube.com/watch?v=K-A9l8bL_Fw
* https://www.youtube.com/watch?v=q9XHXbFEniw
* https://www.youtube.com/watch?v=xh5wbv3yRH4
* https://www.youtube.com/watch?v=YpQwYIr1wFY
*
* Skeram's maximum health is 556509
* Skeram's illusions gain health each subsequent split
* Skeram mind controls the CLOSEST target, including tanks
* Skeram only casts Arcane Explosion if more than 4 targets are in melee range of him
* Earthshock is spammed on his current target if they are not in melee range
* Earthshock has a slight delay after teleports and splits before it is casted
* *** There is possibly a mechanic where Skeram is pacified for ~500ms after teleporting,
* however in the above videos he is seen to sometimes move immediately, and sometimes
* remain stationary ***
*
* ILLUSION HEALTH:
* https://www.youtube.com/watch?v=0-8zXfKmPvY (From 4.x when exact HP values were networked)
* This video puts the image percentages around 7.27%, 9.87% and 11.6% (or 7.5, 10 and 12.5)
* Estimating health from Vanilla based on % changes and spell damage, we can
* get values approximately 5% higher. eg. a Frostbolt damage of 700 reduces
* slightly less than 1% in the 2nd split, indicating it has slightly more than
* 70k health.
* From this, we can estimate that the illusions have 12.5%, 15% and 17.5% per split
*/
struct classic_boss_skeram : public ScriptedAI
{
    classic_boss_skeram(Creature* pCreature) : ScriptedAI(pCreature)
    {
        m_pInstance = pCreature->GetInstanceScript();
        IsImage = false;
        m_maxMeleeAllowed = 4;
        Reset();
    }

    InstanceScript* m_pInstance;
    uint32 m_maxMeleeAllowed;

    uint32 ArcaneExplosion_Timer;
    uint32 EarthShock_Timer;
    uint32 FullFillment_Timer;
    uint32 Blink_Timer;

    float NextSplitPercent;

    ObjectGuid ImageA, ImageB;
    ObjectGuid ControlledPlayerGUID;

    bool IsImage;

    void Reset() override
    {
        ArcaneExplosion_Timer = urand(6000, 8000);
        EarthShock_Timer = 2000;
        FullFillment_Timer = urand(10000, 15000);
        Blink_Timer = urand(15000, 20000);

        NextSplitPercent = 75.0f;

        me->SetVisible(true);

        ImageA.Clear();
        ImageB.Clear();
        ControlledPlayerGUID.Clear();

        // The raised ledges around Skeram's platforms are a pathing nightmare, there is no
        // reasonable way to get onto them with our current implementation (imo Blizzard had
        // an mmap system that let them add invisible ramps for creatures). However, we can
        // obtain partial paths next to it. Normally, these are ignored, but we can set a
        // flag to allow them. They may put us out of LoS so allow autos through them too.
        // TODO(classic): VMaNGOS m_creature->SetMeleeZReach(74.0f) (melee reach through the platform ledges) has no TC equivalent.
    }

    void CancelFulfillment()
    {
        if (Player* FulfilledPlayer = ObjectAccessor::GetPlayer(*me, ControlledPlayerGUID))
        {
            FulfilledPlayer->RemoveAurasDueToSpell(SPELL_TRUE_FULFILLMENT, ObjectGuid::Empty, 0, AURA_REMOVE_BY_CANCEL);
            FulfilledPlayer->RemoveAurasDueToSpell(SPELL_TF_HASTE, ObjectGuid::Empty, 0, AURA_REMOVE_BY_CANCEL);
            FulfilledPlayer->RemoveAurasDueToSpell(SPELL_TF_MOD_HEAL, ObjectGuid::Empty, 0, AURA_REMOVE_BY_CANCEL);
            FulfilledPlayer->RemoveAurasDueToSpell(SPELL_TF_IMMUNITY, ObjectGuid::Empty, 0, AURA_REMOVE_BY_CANCEL);
        }
    }

    void MoveInLineOfSight(Unit* pWho) override
    {
        // The bug trio have a larger than normal aggro radius
        if (pWho->IsPlayer() && !me->IsInCombat() && me->IsWithinDistInMap(pWho, 28.0f, true) && !pWho->HasAuraType(SPELL_AURA_FEIGN_DEATH)
            && me->IsValidAttackTarget(pWho))
        {
            AttackStart(pWho);
        }
        ScriptedAI::MoveInLineOfSight(pWho);
    }

    void KilledUnit(Unit* /*victim*/) override
    {
        switch (urand(0, 8))
        {
            case 0: ClassicScriptText(SAY_SKERAM_SLAY_1, me); break;
            case 1: ClassicSkeramYell(me, TEXT_SKERAM_SLAY_2, SOUND_SKERAM_SLAY_2); break;
            case 2: ClassicSkeramYell(me, TEXT_SKERAM_SLAY_3, SOUND_SKERAM_SLAY_3); break;
            default: break;
        }
    }

    void JustDied(Unit* /*Killer*/) override
    {
        if (IsImage)
        {
            CancelFulfillment();
            me->DespawnOrUnsummon();
            return;
        }
        // Leave up to 20s of instant casts as reward for vaniquishing the True Prophet
        ClassicScriptText(SAY_SKERAM_DEATH, me);
        me->SetRespawnDelay(7 * DAY);

        // TODO(classic): VMaNGOS BindToInstanceOrRaid(killer player, respawn time, permanent) - TC binds through its instance lock
        // system (DungeonEncounter completion) instead.

        if (m_pInstance)
            m_pInstance->SetData(TYPE_SKERAM, DONE);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        if (IsImage)
            return;

        if (m_pInstance && m_pInstance->GetData(TYPE_SKERAM) == IN_PROGRESS)
            return;

        switch (urand(0, 2))
        {
            case 0: ClassicScriptText(SAY_SKERAM_AGGRO_1, me); break;
            case 1: ClassicSkeramYell(me, TEXT_SKERAM_AGGRO_2, SOUND_SKERAM_AGGRO_2); break;
            default: ClassicSkeramYell(me, TEXT_SKERAM_AGGRO_3, SOUND_SKERAM_AGGRO_3); break;
        }

        if (m_pInstance)
            m_pInstance->SetData(TYPE_SKERAM, IN_PROGRESS);

        // Prophet Skeram will only cast Arcane Explosion if a given number of players are in melee range
        // Initial value was 4+ but it was changed in patch 1.12 to be less dependant on raid
        // We assume value is number of players / 10 (raid of 40 people in Classic -> value of 4)
        // (VMaNGOS: WOW_PATCH_112 and later; this server runs 1.12 content)
        m_maxMeleeAllowed = me->GetMap()->GetPlayersCountExceptGMs() / 10;
    }

    void JustReachedHome() override
    {
        CancelFulfillment();

        if (IsImage)
            me->KillSelf();

        if (m_pInstance)
            m_pInstance->SetData(TYPE_SKERAM, FAIL);
    }

    // VMaNGOS FindNearestHostilePlayer(range)
    Player* FindNearestHostilePlayer(float range) const
    {
        std::list<Player*> players;
        me->GetPlayerListInGrid(players, range);
        Player* nearest = nullptr;
        for (Player* player : players)
        {
            if (!me->IsValidAttackTarget(player))
                continue;
            if (!nearest || me->GetDistanceOrder(player, nearest))
                nearest = player;
        }
        return nearest;
    }

    void UpdateAI(uint32 diff) override
    {
        // Despawn Images instantly if the True Prophet died
        if (IsImage && m_pInstance && m_pInstance->GetData(TYPE_SKERAM) == DONE)
        {
            me->KillSelf();
            return;
        }

        // Return since we have no target
        if (!UpdateVictim())
            return;

        if (ArcaneExplosion_Timer < diff)
        {
            // Only cast arcane explosion if there are more than 4 units within melee reach
            // (VMaNGOS GetMeleeReach(): combat reach, at least 2 yards)
            std::list<Player*> players;
            me->GetPlayerListInGrid(players, std::max(me->GetCombatReach(), 2.0f));

            if (players.size() > m_maxMeleeAllowed)
            {
                if (DoCastVictim(SPELL_ARCANE_EXPLOSION) == SPELL_CAST_OK)
                    ArcaneExplosion_Timer = urand(6000, 14000);
            }
            // Recheck in 1 second
            else
                ArcaneExplosion_Timer = 1000;
        }
        else
            ArcaneExplosion_Timer -= diff;

        // If we are within range, melee the target (TC melees automatically)
        if (!me->IsWithinMeleeRange(me->GetVictim()))
        // Target not in melee range. Spam Earthshock
        {
            if (EarthShock_Timer < diff)
            {
                if (DoCastVictim(SPELL_EARTH_SHOCK) == SPELL_CAST_OK)
                    EarthShock_Timer = 2000;
            }
            else
                EarthShock_Timer -= diff;
        }

        if (FullFillment_Timer < diff)
        {
            // Get closest target
            if (Player* target = FindNearestHostilePlayer(40.0f))
            {
                // VMaNGOS CF_AURA_NOT_PRESENT
                if (!target->HasAura(SPELL_TRUE_FULFILLMENT) && DoCast(target, SPELL_TRUE_FULFILLMENT) == SPELL_CAST_OK)
                {
                    // Cancel buffs on previous victim
                    CancelFulfillment();

                    me->CastSpell(target, SPELL_TF_HASTE, true);
                    me->CastSpell(target, SPELL_TF_MOD_HEAL, true);
                    me->CastSpell(target, SPELL_TF_IMMUNITY, true);

                    FullFillment_Timer = urand(20500, 25000);
                    ControlledPlayerGUID = target->GetGUID();
                }
            }
        }
        else
            FullFillment_Timer -= diff;

        if (Blink_Timer < diff)
        {
            CastBlink(me);
            Blink_Timer = urand(10000, 18000);
        }
        else
            Blink_Timer -= diff;

        // Summon 2 Images and teleport for every 25% hp lost
        if (!IsImage && me->GetHealthPct() < NextSplitPercent)
        {
            if (DoCastSelf(SPELL_SUMMON_IMAGES) == SPELL_CAST_OK)
            {
                if (NextSplitPercent < 26.0f)
                    ClassicSkeramYell(me, TEXT_SKERAM_SPLIT, SOUND_SKERAM_SPLIT);

                NextSplitPercent -= 25.0f;
            }
        }
    }

    void JustSummoned(Creature* skeramImage) override
    {
        if (me->GetEntry() != skeramImage->GetEntry())
            return;

        if (classic_boss_skeram* imageAI = dynamic_cast<classic_boss_skeram*>(skeramImage->AI()))
            imageAI->IsImage = true;

        float healthPct = me->GetHealthPct();
        float maxHealthPct;

        // The max health depends on the split phase. It's percent * original boss health
        if (healthPct < 25.0f)
            maxHealthPct = 0.50f;
        else if (healthPct < 50.0f)
            maxHealthPct = 0.20f;
        else
            maxHealthPct = 0.10f;

        // Set the same health percent as the original boss
        skeramImage->SetMaxHealth(uint64(skeramImage->GetMaxHealth() * maxHealthPct));
        skeramImage->SetHealth(skeramImage->CountPctFromMaxHealth(healthPct));
        CreatureAI::DoZoneInCombat(skeramImage);
        skeramImage->SetVisible(false);

        if (ImageA.IsEmpty())
            ImageA = skeramImage->GetGUID();
        else
        {
            ImageB = skeramImage->GetGUID();
            UnisonBlink();
        }
    }

    void UnisonBlink()
    // At least two images present. Blink self and newest two images
    {
        uint32 mask = 0x7;

        // Get Skeram ready for blink
        // Prevent players being able to track the real skeram by things such as
        // combo points, existing spells and pets
        me->RemoveAllAuras();
        // TODO(classic): VMaNGOS ClearTargetIcon(), ClearComboPointHolders() and InterruptSpellsCastedOnMe(true) have no TC equivalent.
        me->RemoveAllAttackers();

        me->SetVisible(false);

        if (Creature* imageA = me->GetMap()->GetCreature(ImageA))
            CastBlink(imageA, mask);
        if (Creature* imageB = me->GetMap()->GetCreature(ImageB))
            CastBlink(imageB, mask);
        CastBlink(me, mask);

        ImageA.Clear();
        ImageB.Clear();
    }

    void CastBlink(Creature* caster)
    // Teleport to any of three positions
    {
        uint32 mask = 0x7;
        CastBlink(caster, mask);
    }

    void CastBlink(Creature* caster, uint32& choiceMask)
    // Teleport to a random position in mask
    // Can teleport to any position if mask = 0x7
    {
        uint32 position = urand(0, 2);

        while (!(1 << position & choiceMask))                       // Bogo select
            position = urand(0, 2);

        choiceMask &= ~(1 << position);                             // Remove used position from mask

        DoStopAttack();

        // Blink to one of the three platforms
        switch (position)
        {
            case 0: caster->CastSpell(caster, SPELL_BLINK_0, true); break;
            case 1: caster->CastSpell(caster, SPELL_BLINK_1, true); break;
            case 2: caster->CastSpell(caster, SPELL_BLINK_2, true); break;
        }

        ResetThreatList();
        // Reset Earthshock timer on blink.
        if (classic_boss_skeram* casterAI = dynamic_cast<classic_boss_skeram*>(caster->AI()))
            casterAI->EarthShock_Timer = 2000;
        caster->SetVisible(true);
    }
};

void AddSC_classic_boss_skeram()
{
    RegisterCreatureAI(classic_boss_skeram);
}
