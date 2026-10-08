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

#ifndef TRINITYCORE_BATTLENET_PACKETS_H
#define TRINITYCORE_BATTLENET_PACKETS_H

#include "Packet.h"
#include "BattlenetRpcErrorCodes.h"
#include "MessageBuffer.h"
#include "PacketUtilities.h"
#include <array>

namespace WorldPackets
{
    namespace Battlenet
    {
        struct MethodCall
        {
            uint64 Type = 0;
            uint64 ObjectId = 0;
            uint32 Token = 0;

            uint32 GetServiceHash() const { return uint32(Type >> 32); }
            uint32 GetMethodId() const { return uint32(Type & 0xFFFFFFFF); }
        };

        class Notification final : public ServerPacket
        {
        public:
            explicit Notification() : ServerPacket(SMSG_BATTLENET_NOTIFICATION, 8 + 8 + 4 + 4) { }

            WorldPacket const* Write() override;

            MethodCall Method;
            ByteBuffer Data;
        };

        class Response final : public ServerPacket
        {
        public:
            explicit Response() : ServerPacket(SMSG_BATTLENET_RESPONSE, 4 + 8 + 8 + 4 + 4) { }

            WorldPacket const* Write() override;

            BattlenetRpcErrorCode BnetStatus = ERROR_OK;
            MethodCall Method;
            ByteBuffer Data;
        };

        class ConnectionStatus final : public ServerPacket
        {
        public:
            explicit ConnectionStatus() : ServerPacket(SMSG_BATTLE_NET_CONNECTION_STATUS, 1) { }

            WorldPacket const* Write() override;

            uint8 State = 0;
            bool SuppressNotification = true;
        };

        class ChangeRealmTicketResponse final : public ServerPacket
        {
        public:
            explicit ChangeRealmTicketResponse() : ServerPacket(SMSG_CHANGE_REALM_TICKET_RESPONSE) { }

            WorldPacket const* Write() override;

            uint32 Token = 0;
            bool Allow = false;
            ByteBuffer Ticket;
        };

        class Request final : public ClientPacket
        {
        public:
            explicit Request(WorldPacket&& packet) : ClientPacket(CMSG_BATTLENET_REQUEST, std::move(packet)) { }

            void Read() override;

            MethodCall Method;
            MessageBuffer Data;
        };

        class ChangeRealmTicket final : public ClientPacket
        {
        public:
            explicit ChangeRealmTicket(WorldPacket&& packet) : ClientPacket(CMSG_CHANGE_REALM_TICKET, std::move(packet)) { }

            void Read() override;

            uint32 Token = 0;
            std::array<uint8, 32> Secret = { };
        };

        // Classic 1.60: the in-game browser (Support window) opens <sso url>?token=<Token>&ref=<page>
        class GenerateSsoToken final : public ClientPacket
        {
        public:
            explicit GenerateSsoToken(WorldPacket&& packet) : ClientPacket(CMSG_GENERATE_SSO_TOKEN, std::move(packet)) { }

            void Read() override;

            uint32 RequestID = 0;
            uint32 Usage = 0;   // 0x00417070 ("ppA") in every request seen
        };

        class GenerateSsoTokenResponse final : public ServerPacket
        {
        public:
            explicit GenerateSsoTokenResponse() : ServerPacket(SMSG_GENERATE_SSO_TOKEN_RESPONSE, 4 + 4 + 8 + 8 + 64) { }

            WorldPacket const* Write() override;

            uint32 RequestID = 0;
            uint32 Result = 0;
            Timestamp<> Issued;
            Timestamp<> Expires;
            std::string Token;  // no length prefix, the rest of the packet
        };
    }
}

#endif // TRINITYCORE_BATTLENET_PACKETS_H
