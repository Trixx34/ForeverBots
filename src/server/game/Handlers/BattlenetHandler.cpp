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
#include "BattlenetPackets.h"
#include "CryptoRandom.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Player.h"
#include "RealmList.h"
#include "StringFormat.h"
#include "Util.h"
#include "WorldserverServiceDispatcher.h"
#include "ObjectDefines.h"

void WorldSession::HandleBattlenetChangeRealmTicket(WorldPackets::Battlenet::ChangeRealmTicket& changeRealmTicket)
{
    SetRealmListSecret(changeRealmTicket.Secret);

    WorldPackets::Battlenet::ChangeRealmTicketResponse realmListTicket;
    realmListTicket.Token = changeRealmTicket.Token;
    realmListTicket.Allow = true;
    realmListTicket.Ticket << "WorldserverRealmListTicket";

    SendPacket(realmListTicket.Write());
}

void WorldSession::HandleBattlenetRequest(WorldPackets::Battlenet::Request& request)
{
    sServiceDispatcher.Dispatch(this, request.Method.GetServiceHash(), request.Method.Token, request.Method.GetMethodId(), std::move(request.Data));
}

// Classic 1.60: tokens for the in-game browser (Support window), stored in battlenet_sso_tokens so our support site can tell who opened it.
// Same shape as the official ones: "TUS-" + 32 lowercase hex + "-" + battle.net account id, valid for 4 hours.
std::string WorldSession::CreateSsoToken(time_t issued, time_t expires)
{
    std::string secret = ByteArrayToHexStr(Trinity::Crypto::GetRandomBytes<16>());
    strToLower(secret);
    std::string token = Trinity::StringFormat("TUS-{}-{}", secret, GetBattlenetAccountId());

    LoginDatabaseTransaction trans = LoginDatabase.BeginTransaction();
    trans->Append(LoginDatabase.GetPreparedStatement(LOGIN_DEL_BNET_SSO_TOKENS_EXPIRED));

    LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_INS_BNET_SSO_TOKEN);
    stmt->setString(0, token);
    stmt->setUInt32(1, GetBattlenetAccountId());
    stmt->setUInt32(2, GetAccountId());
    stmt->setUInt32(3, sRealmList->GetCurrentRealmId().Realm);
    stmt->setUInt64(4, _player ? _player->GetGUID().GetCounter() : 0);
    stmt->setInt64(5, issued);
    stmt->setInt64(6, expires);
    stmt->setString(7, GetRemoteAddress());
    trans->Append(stmt);
    LoginDatabase.CommitTransaction(trans);

    return token;
}

void WorldSession::HandleGenerateSsoToken(WorldPackets::Battlenet::GenerateSsoToken& generateSsoToken)
{
    WorldPackets::Battlenet::GenerateSsoTokenResponse response;
    response.RequestID = generateSsoToken.RequestID;
    response.Issued = GameTime::GetGameTime();
    response.Expires = response.Issued + SsoTokenDuration;
    response.Token = CreateSsoToken(response.Issued, response.Expires);

    SendPacket(response.Write());
}

void WorldSession::SendBattlenetResponse(uint32 serviceHash, uint32 methodId, uint32 token, pb::Message const* response)
{
    WorldPackets::Battlenet::Response bnetResponse;
    bnetResponse.BnetStatus = ERROR_OK;
    bnetResponse.Method.Type = MAKE_PAIR64(methodId, serviceHash);
    bnetResponse.Method.ObjectId = 1;
    bnetResponse.Method.Token = token;

    if (int32 size = response->ByteSize(); size > 0)
    {
        bnetResponse.Data.resize(size);
        response->SerializePartialToArray(bnetResponse.Data.data(), size);
    }

    SendPacket(bnetResponse.Write());
}

void WorldSession::SendBattlenetResponse(uint32 serviceHash, uint32 methodId, uint32 token, uint32 status)
{
    WorldPackets::Battlenet::Response bnetResponse;
    bnetResponse.BnetStatus = BattlenetRpcErrorCode(status);
    bnetResponse.Method.Type = MAKE_PAIR64(methodId, serviceHash);
    bnetResponse.Method.ObjectId = 1;
    bnetResponse.Method.Token = token;

    SendPacket(bnetResponse.Write());
}

void WorldSession::SendBattlenetRequest(uint32 serviceHash, uint32 methodId, pb::Message const* request, std::function<void(MessageBuffer)> callback)
{
    _battlenetResponseCallbacks[_battlenetRequestToken] = std::move(callback);
    SendBattlenetRequest(serviceHash, methodId, request);
}

void WorldSession::SendBattlenetRequest(uint32 serviceHash, uint32 methodId, pb::Message const* request)
{
    WorldPackets::Battlenet::Notification notification;
    notification.Method.Type = MAKE_PAIR64(methodId, serviceHash);
    notification.Method.ObjectId = 1;
    notification.Method.Token = _battlenetRequestToken++;

    if (int32 size = request->ByteSize(); size > 0)
    {
        notification.Data.resize(size);
        request->SerializePartialToArray(notification.Data.data(), size);
    }

    SendPacket(notification.Write());
}
