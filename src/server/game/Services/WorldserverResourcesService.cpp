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

// Classic (1.60+) clients request a content handle (program 'BN', stream 'apft', locale) at character select
// and crash on a null handle when the call fails, so always answer with a handle for the requested stream.
uint32 Battlenet::Services::ResourcesService::HandleGetContentHandle(resources::v1::ContentHandleRequest const* request, ContentHandle* response,
    std::function<void(ServiceBase*, uint32, ::google::protobuf::Message const*)>& /*continuation*/)
{
    TC_LOG_DEBUG("session.rpc", "{} ResourcesService.GetContentHandle({})", GetCallerInfo(), request->ShortDebugString());

    response->set_region(0x5553); // 'US'
    response->set_usage(request->stream());
    response->set_hash(std::string(32, '\0'));
    return ERROR_OK;
}
