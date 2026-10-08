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

#include "ClassicOpcodes.h"
#include "Opcodes.h"
#include <unordered_map>
#include <unordered_set>

namespace
{
// Opcodes are 0xGGIIII: group byte + index. The connection-level groups appear shifted by one in Classic:
//  - client group 0x45 (retail 0x44): 0x450007 carried a single uint32 (3) = CMSG_LOG_DISCONNECT (retail 0x440007)
//  - server group 0x4D (retail 0x4C, assumed by symmetry)
constexpr uint32 ClassicClientConnectionGroup = 0x45;
constexpr uint32 RetailClientConnectionGroup = 0x44;
constexpr uint32 ClassicServerConnectionGroup = 0x4D;
constexpr uint32 RetailServerConnectionGroup = 0x4C;

// explicit Classic 1.60.1.70009 client opcode -> retail core opcode overrides
std::unordered_map<uint32, uint32> const ClientOpcodes =
{
#include "ClassicClientOpcodes.inc"
};

// explicit retail core opcode -> Classic 1.60.1.70009 client opcode overrides
// Client protocol (0x46): Classic removed one message somewhere before 0x1B5, so around 0x1B5-0x1C0 Classic index = retail index - 1
// (decoder fixed sizes: Classic 0x1B6/0x1B7 = 16 bytes = retail GAME_TIME_UPDATE/GAME_TIME_SET, Classic 0x1B8 = 20 = retail LOGIN_SET_TIME_SPEED).
// Indices from 0x268 on match retail again, so a Classic-only message is inserted somewhere between 0x1C0 and 0x268.
std::unordered_map<uint32, uint32> const ServerOpcodes =
{
    { SMSG_ACCOUNT_DATA_TIMES,      0x4601B5 },  // same layout as retail (guid, int64 server time, 20 x int64), decoder 0x81A300
    { SMSG_GAME_TIME_UPDATE,        0x4601B6 },
    { SMSG_GAME_TIME_SET,           0x4601B7 },
    { SMSG_LOGIN_SET_TIME_SPEED,    0x4601B8 },
    { SMSG_SERVER_TIME_OFFSET,      0x4601BF },  // decoder asserts datasize == 8; 0x1BD = CORPSE_TRANSPORT_QUERY, 0x1BE = ENCHANTMENT_LOG (both retail - 1)
    { SMSG_MIRROR_VARS,             0x460371 },  // decoder 0x83E330: count, then 1 bit + 2 x 24-bit sized strings (0x46036F reads 16-byte records)
    { SMSG_LAST_CATALOG_FETCH_RESPONSE, 0x460382 },  // +2 region like MIRROR_VARS (decoder 0x83FCC0)
};

// sent with the shifted number these hit a different Classic message (client JamClient size asserts)
std::unordered_set<uint32> const BlockedServerOpcodes =
{
    SMSG_HEALTH_UPDATE,     // does not exist in Classic (health is sent through UnitData)
    SMSG_GUILD_CHALLENGE_COMPLETED, // does not exist in Classic (guild switch has one case less, see TranslateServerOpcode)
    SMSG_CHANNEL_LIST,      // Classic chat index 0x1D (reader rva 0xA10E70), layout not verified yet
};

// Classic layout differs; the client waits for the message, so send a placeholder of the expected size
std::unordered_map<uint32, std::vector<uint8>> const StubServerPayloads =
{
    { SMSG_ACCOUNT_ITEM_COLLECTION_DATA, std::vector<uint8>(4, 0) },  // retail 10 bytes, Classic 0x460360 expects 4 (JamClient.cpp:167052)
    { SMSG_DAILY_QUESTS_RESET, {} },  // retail int32 count; Classic expects no data (JamClientQuest.cpp:3723 asserts datasize == 0 at the daily reset)
};

constexpr uint32 OpcodeGroup(uint32 opcode) { return opcode >> 16; }
constexpr uint32 WithOpcodeGroup(uint32 opcode, uint32 group) { return (group << 16) | (opcode & 0xFFFF); }
}

// Hypothesis under test: every opcode group is shifted by +1 in Classic (verified for the connection groups 0x44/0x4C)
constexpr uint32 FirstRetailClientGroup = 0x2A;
constexpr uint32 LastRetailClientGroup = RetailClientConnectionGroup;
constexpr uint32 FirstRetailServerGroup = 0x45;
constexpr uint32 LastRetailServerGroup = 0x6A;
constexpr uint32 ChatRetailServerGroup = 0x4A;
constexpr uint32 GuildRetailServerGroup = 0x51;

uint32 ClassicOpcodes::TranslateClientOpcode(uint32 classicOpcode)
{
    if (auto itr = ClientOpcodes.find(classicOpcode); itr != ClientOpcodes.end())
        return itr->second;

    uint32 group = OpcodeGroup(classicOpcode);
    if (group >= FirstRetailClientGroup + 1 && group <= ClassicClientConnectionGroup)
        return WithOpcodeGroup(classicOpcode, group - 1);

    return classicOpcode;
}

uint32 ClassicOpcodes::TranslateServerOpcode(uint32 coreOpcode)
{
    if (auto itr = ServerOpcodes.find(coreOpcode); itr != ServerOpcodes.end())
        return itr->second;

    uint32 group = OpcodeGroup(coreOpcode);
    if (group >= FirstRetailServerGroup && group <= LastRetailServerGroup)
    {
        uint32 index = coreOpcode & 0xFFFF;
        // Client protocol (retail 0x45): Classic removed SMSG_HEALTH_UPDATE (retail 0x17D), so Classic index = retail - 1 for retail
        // 0x17E..0x1C0 (signature alignment, classic_re/smsg_align.py: POWER_UPDATE at Classic 0x17D, PLAYED_TIME 0x180, LOG_XP_GAIN 0x18F,
        // mirror timers, SERVER_TIME_OFFSET 0x1BF; MINIMAP_PING 0x17A and FISH_* 0x17B/0x17C keep their index)
        if (group == FirstRetailServerGroup && index >= 0x17E && index <= 0x1C0)
            --index;
        // Chat (retail 0x4A, Classic 0x4B, client switch rva 0xA11168, 39 cases): Classic has two extra messages at indices 4-5, so
        // Classic index = retail + 2 from retail 4 on (CHAT_PLAYER_NOTFOUND/AMBIGUOUS name readers at 6/15, USERLIST_* 17-19,
        // CHANNEL_NOTIFY_JOINED 0x1B, CHANNEL_NOTIFY_LEFT 0x1C, CHAT_SERVER_MESSAGE 0x1E; CHAT and MOTD keep 1 and 3)
        if (group == ChatRetailServerGroup && index >= 4)
            index += 2;
        // Guild (retail 0x51, Classic 0x52, client switch rva 0xA1F9E5 in 70058, table 0xA233EC, 0x47 cases vs retail 0x48): Classic has
        // no GUILD_CHALLENGE_COMPLETED (retail 0x1B, blocked), so Classic index = retail - 1 from retail 0x1C on (Classic 0x1A int32 =
        // CHALLENGE_UPDATE, 0x1B guid = ITEM_LOOTED_NOTIFY, 0x22 = NAME_CHANGED, 0x23 bit = FLAGGED_FOR_RENAME, 0x25 = BANK_QUERY_RESULTS,
        // 0x2C = QUERY_GUILD_INFO_RESPONSE, 0x31 = EVENT_PLAYER_JOINED, 0x35 = EVENT_MOTD, 0x36 = EVENT_PRESENCE_CHANGE)
        if (group == GuildRetailServerGroup && index >= 0x1C)
            --index;
        return ((group + 1) << 16) | index;
    }

    return coreOpcode;
}

bool ClassicOpcodes::IsServerOpcodeBlocked(uint32 coreOpcode)
{
    return BlockedServerOpcodes.contains(coreOpcode);
}

std::vector<uint8> const* ClassicOpcodes::GetServerStubPayload(uint32 coreOpcode)
{
    auto itr = StubServerPayloads.find(coreOpcode);
    return itr != StubServerPayloads.end() ? &itr->second : nullptr;
}
