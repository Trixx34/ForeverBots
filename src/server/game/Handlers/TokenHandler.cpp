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

#include "Opcodes.h"
#include "TokenPackets.h"
#include "WorldPacket.h"
#include "WorldSession.h"

void WorldSession::HandleCommerceTokenGetLog(WorldPackets::Token::CommerceTokenGetLog& commerceTokenGetLog)
{
    WorldPackets::Token::CommerceTokenGetLogResponse response;

    response.ClientToken = commerceTokenGetLog.ClientToken;
    response.Result = TOKEN_RESULT_ERROR_DISABLED;

    SendPacket(response.Write());
}

void WorldSession::HandleCommerceTokenGetMarketPrice(WorldPackets::Token::CommerceTokenGetMarketPrice& commerceTokenGetMarketPrice)
{
    WorldPackets::Token::CommerceTokenGetMarketPriceResponse response;

    response.Price = 0;
    response.ClientToken = commerceTokenGetMarketPrice.ClientToken;
    response.Result = TOKEN_RESULT_ERROR_DISABLED;

    SendPacket(response.Write());
}

// Classic 1.60 catalog shop: the client asks at login when it last fetched the catalog. CLASSIC_CATALOG_PROBE: layout not
// known yet (decoder captured while this reply arrives); send "never" as an int64 timestamp.
void WorldSession::HandleGetLastCatalogFetch(WorldPackets::Null& /*null*/)
{
    WorldPacket data(SMSG_LAST_CATALOG_FETCH_RESPONSE, 8);
    data << int64(0);
    SendPacket(&data);
}
