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

#include "WhoPackets.h"
#include "PacketOperators.h"

namespace WorldPackets::Who
{
void WhoIsRequest::Read()
{
    _worldPacket >> SizedString::BitsSize<6>(CharName);
    _worldPacket >> SizedString::Data(CharName);
}

WorldPacket const* WhoIsResponse::Write()
{
    _worldPacket << SizedString::BitsSize<11>(AccountName);
    _worldPacket.FlushBits();

    _worldPacket << SizedString::Data(AccountName);

    return &_worldPacket;
}

ByteBuffer& operator>>(ByteBuffer& data, WhoWord& word)
{
    data.ResetBitPos();
    data >> SizedString::BitsSize<7>(word.Word);
    data >> SizedString::Data(word.Word);

    return data;
}

// Classic 1.60 layout (client writer rva 0x98DF70, message serializer 0x8BB4C0): ServerInfo is uint8, int32, 6 x uint32, bool
ByteBuffer& operator>>(ByteBuffer& data, WhoRequestServerInfo& serverInfo)
{
    data >> serverInfo.FactionGroup;
    data >> serverInfo.Locale;
    data >> serverInfo.RequesterVirtualRealmAddress;
    data.read_skip(5 * sizeof(uint32) + sizeof(uint8));

    return data;
}

// Classic 1.60: an extra 9 bit sized string (surname) after the realm name, a fourth flag bit, and the areas (6 bit count)
// moved into the request
static void ReadWhoRequest(ByteBuffer& data, WhoRequest& request, Array<int32, 63>& areas)
{
    data >> request.MinLevel;
    data >> request.MaxLevel;
    for (int32& rawValue : request.RaceFilter.RawValue)
        data >> rawValue;
    data >> request.ClassFilter;
    data >> SizedString::BitsSize<6>(request.Name);
    data >> SizedString::BitsSize<9>(request.VirtualRealmName);
    data >> SizedString::BitsSize<9>(request.Surname);
    data >> SizedString::BitsSize<7>(request.Guild);
    data >> SizedString::BitsSize<9>(request.GuildVirtualRealmName);
    data >> BitsSize<3>(request.Words);
    data >> Bits<1>(request.ShowEnemies);
    data >> Bits<1>(request.ShowArenaPlayers);
    data >> Bits<1>(request.ExactName);
    data >> Bits<1>(request.Unknown);
    data >> BitsSize<6>(areas);
    data >> OptionalInit(request.ServerInfo);
    data.ResetBitPos();

    data >> SizedString::Data(request.Name);
    data >> SizedString::Data(request.VirtualRealmName);
    data >> SizedString::Data(request.Surname);
    data >> SizedString::Data(request.Guild);
    data >> SizedString::Data(request.GuildVirtualRealmName);

    for (size_t i = 0; i < request.Words.size(); ++i)
    {
        data >> SizedString::BitsSize<7>(request.Words[i].Word);
        data.ResetBitPos();
        data >> SizedString::Data(request.Words[i].Word);
    }

    for (size_t i = 0; i < areas.size(); ++i)
        data >> areas[i];

    if (request.ServerInfo)
        data >> *request.ServerInfo;
}

void WhoRequestPkt::Read()
{
    ReadWhoRequest(_worldPacket, Request, Areas);
    _worldPacket >> Token;
    _worldPacket >> Origin;
    _worldPacket >> Bits<1>(IsAddon);
}

ByteBuffer& operator<<(ByteBuffer& data, WhoEntry const& entry)
{
    data << entry.PlayerData;

    // Classic 1.60 (client reader rva 0x98ECC0): two more int32 after the area, a {4 x uint32, int8} block and one more bit
    data << entry.GuildGUID;
    data << uint32(entry.GuildVirtualRealmAddress);
    data << int32(entry.AreaID);
    data << int32(0);
    data << int32(0);
    for (uint8 i = 0; i < 4; ++i)
        data << uint32(0);
    data << int8(0);

    data << SizedString::BitsSize<7>(entry.GuildName);
    data << Bits<1>(entry.IsGM);
    data << Bits<1>(false);
    data.FlushBits();

    data << SizedString::Data(entry.GuildName);

    return data;
}

ByteBuffer& operator<<(ByteBuffer& data, WhoResponse const& response)
{
    data << Size<uint32>(response.Entries);     // Classic 1.60: uint32 count

    for (WhoEntry const& whoEntry : response.Entries)
        data << whoEntry;

    return data;
}

WorldPacket const* WhoResponsePkt::Write()
{
    _worldPacket << uint32(Token);
    _worldPacket << Response;

    return &_worldPacket;
}
}
