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

#include "WorldserverResourcesService.h"
#include "BattlenetRpcErrorCodes.h"
#include "Log.h"

Battlenet::Services::ResourcesService::ResourcesService(WorldSession* session) : BaseService(session)
{
}

// Classic (1.60+) clients request a content handle (program 'BN', stream 'apft', locale) at login. Unanswered, the client prints
// "Unable to complete your request at this time"; a made-up handle (zero hash) made it wait forever on "Retrieving character list",
// because it downloads the file from Blizzard's depot. So only the handle the official beta returns (sniff 1.60.1.70291, 2026-10-09)
// is sent, for the request it answers; anything else stays unanswered as before.
uint32 Battlenet::Services::ResourcesService::HandleGetContentHandle(resources::v1::ContentHandleRequest const* request, ContentHandle* response,
    std::function<void(ServiceBase*, uint32, ::google::protobuf::Message const*)>& /*continuation*/)
{
    TC_LOG_DEBUG("session.rpc", "{} ResourcesService.GetContentHandle({})", GetCallerInfo(), request->ShortDebugString());

    static uint8 const OfficialHash[32] =
    {
        0x50, 0x63, 0x2D, 0x92, 0xDA, 0x0B, 0x7C, 0x34, 0xBA, 0x82, 0xAF, 0xF0, 0x31, 0x9E, 0x83, 0x76,
        0x06, 0x1D, 0xC0, 0xD0, 0x9B, 0x10, 0x94, 0xE2, 0xD3, 0x6D, 0x39, 0x22, 0x8C, 0x2D, 0xAC, 0x0C
    };

    if (request->program() == 0x424E && request->stream() == 0x61706674 && request->version() == 0x656E5553)   // BN, apft, enUS
    {
        response->set_region(0x5858);                       // 'XX'
        response->set_usage(0x70667479);
        response->set_hash(OfficialHash, sizeof(OfficialHash));
        response->set_proto_url("https://prod.depot.battle.net/${hash}.${usage}");
        return ERROR_OK;
    }

    return ERROR_RPC_NOT_IMPLEMENTED;
}
