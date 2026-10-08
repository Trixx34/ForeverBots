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

// Classic 1.60 port of VMaNGOS src/scripts/kalimdor/silithus/temple_of_ahnqiraj/mob_anubisath_sentinel.cpp (ScriptDev2 lineage, GPL-2)
// Scripts: mob_anubisath_sentinel
// SDComment: Shadow storm is not properly implemented in core it should only target ppl outside of melee range.

#include "ScriptMgr.h"
#include "Creature.h"
#include "Map.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "classic_script_text.h"
#include "classic_temple_of_ahnqiraj.h"
#include <cstdlib>
#include <list>
#include <vector>

namespace
{
enum ClassicAQ40Sentinel : uint32
{
    SPELL_SENTINEL_MENDING_BUFF     = 2147,
    SPELL_SENTINEL_KNOCK_BUFF       = 21737,
    SPELL_SENTINEL_KNOCK            = 23382,
    SPELL_SENTINEL_MANAB_BUFF       = 812,
    SPELL_SENTINEL_MANAB            = 25779,

    SPELL_SENTINEL_REFLECTAF_BUFF   = 13022,
    SPELL_SENTINEL_REFLECTSFR_BUFF  = 19595,
    SPELL_SENTINEL_THORNS_BUFF      = 25777,

    SPELL_SENTINEL_THUNDER_BUFF     = 2834,
    SPELL_SENTINEL_THUNDER          = 23931,

    SPELL_SENTINEL_MSTRIKE_BUFF     = 9347,
    SPELL_SENTINEL_MSTRIKE          = 24573,

    SPELL_SENTINEL_STORM_BUFF       = 2148,
    SPELL_SENTINEL_STORM            = 26546,

    SPELL_SENTINEL_ENRAGE           = 24318,
    EMOTE_SENTINEL_ENRAGE           = 2384,

    SPELL_SENTINEL_TRANSFER         = 2400,
    // EMOTE_TRANSFER = -1388101 (script_texts, no broadcast text): "%s shares his powers with his brethren.", emote

    SPELL_SENTINEL_HEAL_BRETHREN    = 26565,

    NPC_SENTINEL                    = 15264
};

char const* const TEXT_SENTINEL_TRANSFER = "%s shares his powers with his brethren.";
}

struct classic_mob_anubisath_sentinel : public ScriptedAI
{
    uint32 m_abilitySpellId;
    uint32 m_uiKnock_Timer;
    bool m_bEnraged;
    bool m_bAlone;
    bool gatherOthersWhenAggro;
    GuidSet nearby;

    void selectAbility(int asel)
    {
        switch (asel)
        {
            case 0: m_abilitySpellId = SPELL_SENTINEL_MENDING_BUFF; break;
            case 1: m_abilitySpellId = SPELL_SENTINEL_MSTRIKE_BUFF; break;
            case 2: m_abilitySpellId = SPELL_SENTINEL_STORM_BUFF; break;
            case 3: m_abilitySpellId = SPELL_SENTINEL_REFLECTAF_BUFF; break;
            case 4: m_abilitySpellId = SPELL_SENTINEL_REFLECTSFR_BUFF; break;
            case 5: m_abilitySpellId = SPELL_SENTINEL_THORNS_BUFF; break;
            case 6: m_abilitySpellId = SPELL_SENTINEL_THUNDER_BUFF; break;
            case 7: m_abilitySpellId = SPELL_SENTINEL_KNOCK_BUFF; break;
            case 8: m_abilitySpellId = SPELL_SENTINEL_MANAB_BUFF; break;
            default: break;
        }
    }

    classic_mob_anubisath_sentinel(Creature* pCreature) : ScriptedAI(pCreature)
    {
        ClearBuddyList();
        m_bEnraged = false;
        m_abilitySpellId = 0;
        m_bAlone = false;
        gatherOthersWhenAggro = true;
        m_uiKnock_Timer = 13000;
    }

    void MoveInLineOfSight(Unit* pWho) override
    {
        // Increase aggro radius
        if (pWho->IsPlayer() && !me->IsInCombat() && me->IsWithinDistInMap(pWho, 45.0f) && me->IsWithinLOSInMap(pWho)
            && !pWho->HasAuraType(SPELL_AURA_FEIGN_DEATH) && me->IsValidAttackTarget(pWho))
        {
            AttackStart(pWho);
        }
        ScriptedAI::MoveInLineOfSight(pWho);
    }

    void ClearBuddyList()
    {
        nearby.clear();
    }

    void AddBuddyToList(Creature* buddy)
    {
        if (buddy == me)
            return;

        if (nearby.find(buddy->GetGUID()) != nearby.end())
            return;

        nearby.insert(buddy->GetGUID());
    }

    void GiveBuddyMyList(ObjectGuid buddyGuid)
    {
        // Spread the buddy list. Add whoever is in my list to the buddy
        // as well.
        if (Creature* buddy = me->GetMap()->GetCreature(buddyGuid))
        {
            if (classic_mob_anubisath_sentinel* sentinelAI = dynamic_cast<classic_mob_anubisath_sentinel*>(buddy->AI()))
            {
                for (ObjectGuid const& guid : nearby)
                    if (Creature* otherBuddy = me->GetMap()->GetCreature(guid))
                        sentinelAI->AddBuddyToList(otherBuddy);

                sentinelAI->AddBuddyToList(me);
            }
        }
    }

    void SendMyListToBuddies()
    {
        for (ObjectGuid const& guid : nearby)
            GiveBuddyMyList(guid);
    }

    void CallBuddiesToAttack(Unit* who)
    {
        for (ObjectGuid const& guid : nearby)
        {
            if (Creature* creature = me->GetMap()->GetCreature(guid))
            {
                if (creature->IsInCombat())
                    continue;

                creature->SetNoCallAssistance(true);
                if (creature->IsAIEnabled())
                    creature->AI()->AttackStart(who);
            }
        }
    }

    void AddSentinelsNear(Unit* source)
    {
        std::list<Creature*> assistList;
        GetCreatureListWithEntryInGrid(assistList, source, NPC_SENTINEL, 90.0f);

        for (Creature* iter : assistList)
            AddBuddyToList(iter);
    }

    int pickAbilityRandom(std::vector<bool>& chosenAbilities)
    {
        for (int t = 0; t < 2; ++t)
        {
            for (int i = !t ? (rand() % 9) : 0; i < 9; ++i)
            {
                if (!chosenAbilities[i])
                {
                    chosenAbilities[i] = true;
                    return i;
                }
            }
        }
        return 0;                                           // should never happen
    }

    void GetOtherSentinels(Unit* who)
    {
        std::vector<bool> chosenAbilities(9);
        selectAbility(pickAbilityRandom(chosenAbilities));

        ClearBuddyList();
        AddSentinelsNear(me);

        // AddSentinelsNear(buddy) inserts into nearby while iterating (std::set: iterators stay valid, like VMaNGOS)
        for (ObjectGuid const& guid : nearby)
        {
            if (Creature* buddy = me->GetMap()->GetCreature(guid))
            {
                if (classic_mob_anubisath_sentinel* sentinelAI = dynamic_cast<classic_mob_anubisath_sentinel*>(buddy->AI()))
                {
                    AddSentinelsNear(buddy);
                    sentinelAI->gatherOthersWhenAggro = false;
                    sentinelAI->selectAbility(pickAbilityRandom(chosenAbilities));
                }
            }
        }

        SendMyListToBuddies();
        CallBuddiesToAttack(who);
    }

    void Reset() override
    {
        if (!me->isDead())
        {
            for (ObjectGuid const& guid : nearby)
            {
                if (Creature* buddy = me->GetMap()->GetCreature(guid))
                {
                    if (buddy->isDead())
                        buddy->Respawn();
                }
            }
        }

        ClearBuddyList();
        gatherOthersWhenAggro = true;
        m_uiKnock_Timer = 13000;
        m_bEnraged = false;
    }

    void GainSentinelAbility(uint32 id)
    {
        if (id)
            me->AddAura(id, me);
    }

    // Threat reduction for Knock Away
    void SpellHitTarget(WorldObject* pTarget, SpellInfo const* pSpell) override
    {
        if (pSpell->Id == SPELL_SENTINEL_KNOCK && pTarget->IsPlayer())
        {
            if (Unit* victim = me->GetVictim())
                if (me->GetThreatManager().GetThreat(victim))
                    me->GetThreatManager().ModifyThreatByPercent(victim, -20);
        }
    }

    void JustEngagedWith(Unit* pWho) override
    {
        if (gatherOthersWhenAggro)
            GetOtherSentinels(pWho);

        GainSentinelAbility(m_abilitySpellId);

        DoZoneInCombat();
    }

    // Transfer powers on death
    void JustDied(Unit* /*killer*/) override
    {
        m_bAlone = true;
        for (ObjectGuid const& guid : nearby)
        {
            if (Creature* buddy = me->GetMap()->GetCreature(guid))
            {
                if (buddy->isDead())
                    continue;

                m_bAlone = false;

                DoCast(buddy, SPELL_SENTINEL_TRANSFER, true);
                DoCast(buddy, SPELL_SENTINEL_HEAL_BRETHREN, true);

                if (classic_mob_anubisath_sentinel* sentinelAI = dynamic_cast<classic_mob_anubisath_sentinel*>(buddy->AI()))
                    sentinelAI->GainSentinelAbility(m_abilitySpellId);
            }
        }

        if (!m_bAlone)
            me->TextEmote(TEXT_SENTINEL_TRANSFER);
    }

    void UpdateAI(uint32 uiDiff) override
    {
        // Return since we have no target
        if (!UpdateVictim())
            return;

        if (m_abilitySpellId == SPELL_SENTINEL_KNOCK_BUFF)
        {
            if (m_uiKnock_Timer < uiDiff)
            {
                DoCastVictim(SPELL_SENTINEL_KNOCK);
                m_uiKnock_Timer = 13000;
            }
            else
                m_uiKnock_Timer -= uiDiff;
        }

        if (!m_bEnraged && me->GetHealthPct() < 30.0f)
        {
            if (DoCastSelf(SPELL_SENTINEL_ENRAGE) == SPELL_CAST_OK)
            {
                ClassicScriptText(EMOTE_SENTINEL_ENRAGE, me);
                m_bEnraged = true;
            }
        }
    }
};

void AddSC_classic_mob_anubisath_sentinel()
{
    RegisterCreatureAI(classic_mob_anubisath_sentinel);
}
