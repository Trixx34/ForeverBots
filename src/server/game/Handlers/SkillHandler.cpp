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

#include "WorldSession.h"
#include "Common.h"
#include "DB2Stores.h"
#include "GossipDef.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Pet.h"
#include "Player.h"
#include "SpellMgr.h"
#include "SpellPackets.h"
#include "TalentPackets.h"

void WorldSession::HandleLearnTalentsOpcode(WorldPackets::Talent::LearnTalents& packet)
{
    WorldPackets::Talent::LearnTalentFailed learnTalentFailed;
    bool anythingLearned = false;
    for (uint32 talentId : packet.Talents)
    {
        if (TalentLearnResult result = _player->LearnTalent(talentId, &learnTalentFailed.SpellID))
        {
            if (!learnTalentFailed.Reason)
                learnTalentFailed.Reason = result;

            learnTalentFailed.Talents.push_back(talentId);
        }
        else
            anythingLearned = true;
    }

    if (learnTalentFailed.Reason)
        SendPacket(learnTalentFailed.Write());

    if (anythingLearned)
        _player->SendTalentsInfoData();
}

void WorldSession::HandleLearnPvpTalentsOpcode(WorldPackets::Talent::LearnPvpTalents& packet)
{
    WorldPackets::Talent::LearnPvpTalentFailed learnPvpTalentFailed;
    bool anythingLearned = false;
    for (WorldPackets::Talent::PvPTalent pvpTalent : packet.Talents)
    {
        if (TalentLearnResult result = _player->LearnPvpTalent(pvpTalent.PvPTalentID, pvpTalent.Slot, &learnPvpTalentFailed.SpellID))
        {
            if (!learnPvpTalentFailed.Reason)
                learnPvpTalentFailed.Reason = result;

            learnPvpTalentFailed.Talents.push_back(pvpTalent);
        }
        else
            anythingLearned = true;
    }

    if (learnPvpTalentFailed.Reason)
        SendPacket(learnPvpTalentFailed.Write());

    if (anythingLearned)
        _player->SendTalentsInfoData();
}

void WorldSession::HandleConfirmRespecWipeOpcode(WorldPackets::Talent::ConfirmRespecWipe& confirmRespecWipe)
{
    Creature* unit = GetPlayer()->GetNPCIfCanInteractWith(confirmRespecWipe.RespecMaster, UNIT_NPC_FLAG_TRAINER, UNIT_NPC_FLAG_2_NONE);
    if (!unit)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleConfirmRespecWipeOpcode - {} not found or you can't interact with him.", confirmRespecWipe.RespecMaster);
        return;
    }

    if (confirmRespecWipe.RespecType != SPEC_RESET_TALENTS)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleConfirmRespecWipeOpcode - reset type {} is not implemented.", confirmRespecWipe.RespecType);
        return;
    }

    if (!unit->CanResetTalents(_player))
        return;

    int64 cost = _player->GetNextResetTalentsCost();
    if (!_player->HasEnoughMoney(cost))
        return; // // silently return, client should display the error by itself

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    if (!_player->ResetTalents())
    {
        _player->SendRespecWipeConfirm(ObjectGuid::Empty, 0, static_cast<SpecResetType>(confirmRespecWipe.RespecType));
        return;
    }

    _player->ModifyMoney(-cost);
    _player->IncreaseResetTalentsCostAndCounters(cost);
    _player->SendTalentsInfoData();

    unit->CastSpell(_player, 14867 /*SPELL_UNTALENT_VISUAL_EFFECT*/, true);
}

void WorldSession::HandleUnlearnSkillOpcode(WorldPackets::Spells::UnlearnSkill& packet)
{
    SkillRaceClassInfoEntry const* rcEntry = sDB2Manager.GetSkillRaceClassInfo(packet.SkillLine, GetPlayer()->GetRace(), GetPlayer()->GetClass());
    if (!rcEntry || !(rcEntry->Flags & SKILL_FLAG_UNLEARNABLE))
        return;

    GetPlayer()->SetSkill(packet.SkillLine, 0, 0, 0);
}

void WorldSession::HandleTradeSkillSetFavorite(WorldPackets::Spells::TradeSkillSetFavorite const& tradeSkillSetFavorite)
{
    if (!_player->HasSpell(tradeSkillSetFavorite.RecipeID))
        return;

    _player->SetSpellFavorite(tradeSkillSetFavorite.RecipeID, tradeSkillSetFavorite.IsFavorite);
}

// Classic 1.60: a click on another player's profession link in chat. The answer lists that player's profession line and its
// Classic child line (e.g. Enchanting 333 + 2940), their ranks and every recipe known on them (official beta sniff, build 70170).
void WorldSession::HandleShowTradeSkill(WorldPackets::Spells::ShowTradeSkill& packet)
{
    Player* target = ObjectAccessor::FindConnectedPlayer(packet.PlayerGUID);
    if (!target)
        return;

    uint32 skill = Player::GetClassicProfessionSkill(uint32(packet.SkillLineID));
    if (!target->HasSkill(skill))
        return;

    std::vector<uint32> lines = { skill };
    if (std::vector<SkillLineEntry const*> const* children = sDB2Manager.GetSkillLinesForParentSkill(skill))
        for (SkillLineEntry const* child : *children)
            if (target->HasSkill(child->ID))
                lines.push_back(child->ID);

    WorldPackets::Spells::ShowTradeSkillResponse response;
    response.PlayerGUID = packet.PlayerGUID;
    response.SpellID = packet.SpellID;
    for (uint32 line : lines)
    {
        response.SkillLineIDs.push_back(int32(line));
        response.SkillRanks.push_back(int32(target->GetSkillValue(line)));
        response.SkillMaxRanks.push_back(int32(target->GetMaxSkillValue(line)));
    }

    for (auto const& [spellId, spell] : target->GetSpellMap())
    {
        if (spell.state == PLAYERSPELL_REMOVED || !spell.active || spell.disabled)
            continue;

        SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
        for (auto itr = bounds.first; itr != bounds.second; ++itr)
        {
            if (std::find(lines.begin(), lines.end(), uint32(itr->second->SkillLine)) != lines.end())
            {
                response.KnownAbilitySpellIDs.push_back(int32(spellId));
                break;
            }
        }
    }

    SendPacket(response.Write());
}
