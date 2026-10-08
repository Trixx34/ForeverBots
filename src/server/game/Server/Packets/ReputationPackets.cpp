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

#include "ReputationPackets.h"
#include "PacketOperators.h"

namespace WorldPackets::Reputation
{
ByteBuffer& operator<<(ByteBuffer& data, FactionData const& factionData)
{
    data << int32(factionData.FactionID);
    data << uint16(factionData.Flags);
    data << int32(factionData.Standing);

    return data;
}

ByteBuffer& operator<<(ByteBuffer& data, FactionBonusData const& factionBonusData)
{
    data << int32(factionBonusData.FactionID);
    data << Bits<1>(factionBonusData.FactionHasBonus);
    data.FlushBits();

    return data;
}
}

WorldPacket const* WorldPackets::Reputation::InitializeFactions::Write()
{
    _worldPacket << Size<uint32>(Factions);
    _worldPacket << Size<uint32>(Bonuses);

    for (FactionData const& faction : Factions)
        _worldPacket << faction;

    for (FactionBonusData const& bonus : Bonuses)
        _worldPacket << bonus;

    return &_worldPacket;
}

ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::Reputation::FactionStandingData const& factionStanding)
{
    // Classic 1.60 (official sniff 70205): index, change, new standing, 0 ("(70, 50) 2265", then "(70, 50) 2315"); the client
    // shows the second value as the gain, TC's index/standing/faction made it print the whole standing
    data << int32(factionStanding.Index);
    data << int32(factionStanding.Change);
    data << int32(factionStanding.Standing);
    data << int32(0);

    return data;
}

WorldPacket const* WorldPackets::Reputation::SetFactionStanding::Write()
{
    _worldPacket << float(BonusFromAchievementSystem);
    _worldPacket << Size<uint32>(Faction);
    for (FactionStandingData const& factionStanding : Faction)
        _worldPacket << factionStanding;

    _worldPacket << Bits<1>(ShowVisual);
    _worldPacket.FlushBits();

    return &_worldPacket;
}
