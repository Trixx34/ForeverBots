/*
 * Interop test: ModuleWire (core, proto2 hand-written codec) against forever-module-sdk's generated
 * protobuf code. Messages are checked in both directions, so a field-number or type mismatch on
 * either side fails here.
 *
 * Not part of the worldserver build. Build and run it against an SDK build tree, for example:
 *   g++ -std=c++17 -Isrc/common -Isrc/server/game/Modules \
 *       -I<sdk>/include -I<sdk-build>/generated/forever_module \
 *       src/server/game/Modules/ModuleWire.cpp \
 *       src/server/game/Modules/tests/ModuleWireInterop.cpp \
 *       <sdk-build>/generated/forever_module/{handshake,events,control,envelope}.pb.cc \
 *       -lprotobuf -o module_wire_interop && ./module_wire_interop
 */

#include "ModuleWire.h"

#include "envelope.pb.h"
#include "actions.pb.h"
#include "control.pb.h"
#include "events.pb.h"
#include "handshake.pb.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{

int g_failures = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            ++g_failures;                                                           \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                           \
    } while (0)

} // namespace

int main()
{
    // SDK -> core: HandshakeRequest wrapped in an Envelope.
    {
        forever_module::Envelope sdk;
        sdk.set_message_id(5);
        forever_module::HandshakeRequest* req = sdk.mutable_handshake_request();
        req->set_module_name("minimal-module");
        req->set_module_version("0.1.0");
        req->set_protocol_version("5");
        req->add_tier1_hooks("WorldScript.OnStartup");
        req->add_tier1_hooks("CommandScript");
        req->set_sdk_package_version("0.1.0");
        req->set_tier2_participates(true);
        req->set_requested_max_bots(64);
        req->set_requested_tick_interval(2);

        std::string bytes;
        CHECK(sdk.SerializeToString(&bytes));

        ModuleWire::Envelope env;
        CHECK(ModuleWire::DecodeEnvelope(bytes, env));
        CHECK(env.MessageId == 5);
        CHECK(env.Kind == ModuleWire::PayloadKind::HandshakeRequest);

        ModuleWire::HandshakeRequest core;
        CHECK(ModuleWire::Decode(env.Payload, core));
        CHECK(core.ModuleName == "minimal-module");
        CHECK(core.ModuleVersion == "0.1.0");
        CHECK(core.ProtocolVersion == "5");
        CHECK(core.Tier1Hooks.size() == 2);
        CHECK(core.Tier1Hooks.size() == 2 && core.Tier1Hooks[0] == "WorldScript.OnStartup");
        CHECK(core.Tier1Hooks.size() == 2 && core.Tier1Hooks[1] == "CommandScript");
        CHECK(core.SdkPackageVersion == "0.1.0");
        CHECK(core.Tier2Participates);
        CHECK(core.RequestedMaxBots == 64);
        CHECK(core.RequestedTickInterval == 2);
    }

    // SDK -> core: Heartbeat.
    {
        forever_module::Envelope sdk;
        sdk.mutable_heartbeat()->set_sequence(1234567890123ULL);
        std::string bytes;
        CHECK(sdk.SerializeToString(&bytes));

        ModuleWire::Envelope env;
        CHECK(ModuleWire::DecodeEnvelope(bytes, env));
        CHECK(env.Kind == ModuleWire::PayloadKind::Heartbeat);
        ModuleWire::Heartbeat hb;
        CHECK(ModuleWire::Decode(env.Payload, hb));
        CHECK(hb.Sequence == 1234567890123ULL);
    }

    // core -> SDK: HandshakeResponse (accepted, with negotiated bounds).
    {
        ModuleWire::HandshakeResponse resp;
        resp.Accepted = true;
        resp.ModuleId = 7;
        resp.NegotiatedMaxBots = 16;
        resp.NegotiatedTickInterval = 2;
        std::string bytes = ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::HandshakeResponse,
                                                       ModuleWire::Encode(resp), 3);

        forever_module::Envelope sdk;
        CHECK(sdk.ParseFromString(bytes));
        CHECK(sdk.message_id() == 3);
        CHECK(sdk.payload_case() == forever_module::Envelope::kHandshakeResponse);
        CHECK(sdk.handshake_response().accepted());
        CHECK(sdk.handshake_response().module_id() == 7);
        CHECK(sdk.handshake_response().negotiated_max_bots() == 16);
        CHECK(sdk.handshake_response().negotiated_tick_interval() == 2);
        CHECK(sdk.handshake_response().reason().empty());
    }

    // core -> SDK: rejection carries the reason.
    {
        ModuleWire::HandshakeResponse resp;
        resp.Accepted = false;
        resp.Reason = "protocol_version mismatch";
        std::string bytes = ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::HandshakeResponse,
                                                       ModuleWire::Encode(resp));
        forever_module::Envelope sdk;
        CHECK(sdk.ParseFromString(bytes));
        CHECK(!sdk.handshake_response().accepted());
        CHECK(sdk.handshake_response().reason() == "protocol_version mismatch");
    }

    // core -> SDK: ModuleConfig with two entries.
    {
        std::vector<ModuleWire::ConfigEntry> values = {{"greeting", "hello"}, {"empty", ""}};
        std::string bytes = ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::ModuleConfig,
                                                       ModuleWire::Encode(values));
        forever_module::Envelope sdk;
        CHECK(sdk.ParseFromString(bytes));
        CHECK(sdk.payload_case() == forever_module::Envelope::kModuleConfig);
        CHECK(sdk.module_config().values_size() == 2);
        CHECK(sdk.module_config().values(0).key() == "greeting");
        CHECK(sdk.module_config().values(0).value() == "hello");
        CHECK(sdk.module_config().values(1).key() == "empty");
        CHECK(sdk.module_config().values(1).value().empty());
    }

    // core -> SDK: Shutdown with a reason.
    {
        ModuleWire::Shutdown shutdown{"server stopping"};
        std::string bytes = ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::Shutdown,
                                                       ModuleWire::Encode(shutdown));
        forever_module::Envelope sdk;
        CHECK(sdk.ParseFromString(bytes));
        CHECK(sdk.payload_case() == forever_module::Envelope::kShutdown);
        CHECK(sdk.shutdown().reason() == "server stopping");
    }

    // Unknown fields are skipped, as protobuf does: append an unknown varint field (number 99) and an
    // unknown length-delimited field (number 98) to a heartbeat envelope.
    {
        std::string bytes = ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::Heartbeat,
                                                       ModuleWire::Encode(ModuleWire::Heartbeat{42}));
        bytes += std::string("\xD8\x06\x05", 3);          // field 99, varint, value 5
        bytes += std::string("\xC2\x06\x02hi", 5);         // field 98, length-delimited, "hi"

        ModuleWire::Envelope env;
        CHECK(ModuleWire::DecodeEnvelope(bytes, env));
        CHECK(env.Kind == ModuleWire::PayloadKind::Heartbeat);
        ModuleWire::Heartbeat hb;
        CHECK(ModuleWire::Decode(env.Payload, hb));
        CHECK(hb.Sequence == 42);

        forever_module::Envelope sdk;
        CHECK(sdk.ParseFromString(bytes));
        CHECK(sdk.heartbeat().sequence() == 42);
    }

    // Malformed input is rejected rather than read past the end.
    {
        ModuleWire::Envelope env;
        CHECK(!ModuleWire::DecodeEnvelope(std::string("\x0A\x10", 2), env)); // length 16, no bytes
        CHECK(!ModuleWire::DecodeEnvelope(std::string("\xFF\xFF\xFF", 3), env)); // truncated varint
    }

    // core -> SDK: EventNotify for every hook payload kind the core sends.
    {
        struct Case { ModuleWire::NotifyKind kind; uint64 value; };
        Case const cases[] = {
            {ModuleWire::NotifyKind::PlayerLogin, 42},
            {ModuleWire::NotifyKind::PlayerLogout, 43},
            {ModuleWire::NotifyKind::AccountLogin, 7},
            {ModuleWire::NotifyKind::WorldStartup, 0},
            {ModuleWire::NotifyKind::WorldShutdown, 0},
        };
        for (Case const& c : cases)
        {
            std::string bytes = ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::EventNotify,
                                                           ModuleWire::EncodeEventNotify("Hook.Name", c.kind, c.value));
            forever_module::Envelope sdk;
            CHECK(sdk.ParseFromString(bytes));
            CHECK(sdk.payload_case() == forever_module::Envelope::kEventNotify);
            CHECK(sdk.event_notify().hook_name() == "Hook.Name");
            switch (c.kind)
            {
                case ModuleWire::NotifyKind::PlayerLogin:
                    CHECK(sdk.event_notify().event_payload_case() == forever_module::EventNotify::kPlayerLogin);
                    CHECK(sdk.event_notify().player_login().player_guid() == c.value);
                    break;
                case ModuleWire::NotifyKind::PlayerLogout:
                    CHECK(sdk.event_notify().event_payload_case() == forever_module::EventNotify::kPlayerLogout);
                    CHECK(sdk.event_notify().player_logout().player_guid() == c.value);
                    break;
                case ModuleWire::NotifyKind::AccountLogin:
                    CHECK(sdk.event_notify().event_payload_case() == forever_module::EventNotify::kAccountLogin);
                    CHECK(sdk.event_notify().account_login().account_id() == c.value);
                    break;
                case ModuleWire::NotifyKind::WorldStartup:
                    CHECK(sdk.event_notify().event_payload_case() == forever_module::EventNotify::kWorldStartup);
                    break;
                case ModuleWire::NotifyKind::WorldShutdown:
                    CHECK(sdk.event_notify().event_payload_case() == forever_module::EventNotify::kWorldShutdown);
                    break;
                case ModuleWire::NotifyKind::PlayerChat:
                    break; // has its own body, checked below
            }
        }
    }

    // core -> SDK: PlayerChat notify.
    {
        ModuleWire::ChatEvent chat;
        chat.PlayerGuid = 55;
        chat.Channel = 3;
        chat.Text = "follow";
        chat.RecipientGuid = 66;
        chat.MapId = 36;
        chat.PosX = 1.5f;
        chat.PosY = -2.5f;
        chat.PosZ = 3.25f;
        chat.TargetRef = 99;
        std::string bytes = ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::EventNotify,
                                                       ModuleWire::EncodeEventNotifyChat("PlayerScript.OnChat", chat));
        forever_module::Envelope sdk;
        CHECK(sdk.ParseFromString(bytes));
        CHECK(sdk.event_notify().hook_name() == "PlayerScript.OnChat");
        CHECK(sdk.event_notify().event_payload_case() == forever_module::EventNotify::kPlayerChat);
        CHECK(sdk.event_notify().player_chat().player_guid() == 55);
        CHECK(sdk.event_notify().player_chat().channel() == 3);
        CHECK(sdk.event_notify().player_chat().text() == "follow");
        CHECK(sdk.event_notify().player_chat().recipient_guid() == 66);
        CHECK(sdk.event_notify().player_chat().map_id() == 36);
        CHECK(sdk.event_notify().player_chat().pos_x() == 1.5f && sdk.event_notify().player_chat().pos_y() == -2.5f &&
              sdk.event_notify().player_chat().pos_z() == 3.25f);
        CHECK(sdk.event_notify().player_chat().target_ref() == 99);
    }

    // core -> SDK: EventRequest carrying a command, with the envelope message id set.
    {
        std::string bytes = ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::EventRequest,
                                                       ModuleWire::EncodeEventRequestCommand(77, "CommandScript", "ping", 1234), 77);
        forever_module::Envelope sdk;
        CHECK(sdk.ParseFromString(bytes));
        CHECK(sdk.message_id() == 77);
        CHECK(sdk.payload_case() == forever_module::Envelope::kEventRequest);
        CHECK(sdk.event_request().message_id() == 77);
        CHECK(sdk.event_request().hook_name() == "CommandScript");
        CHECK(sdk.event_request().command().command_text() == "ping");
        CHECK(sdk.event_request().command().issuer_guid() == 1234);
    }

    // SDK -> core: EventReply with and without a command payload.
    {
        forever_module::EventReply reply;
        reply.set_message_id(77);
        reply.mutable_command()->set_success(true);
        reply.mutable_command()->set_result_text("pong");
        std::string bytes;
        CHECK(reply.SerializeToString(&bytes));

        ModuleWire::EventReplyMsg core;
        CHECK(ModuleWire::Decode(bytes, core));
        CHECK(core.MessageId == 77);
        CHECK(core.HasCommand);
        CHECK(core.Success);
        CHECK(core.ResultText == "pong");

        forever_module::EventReply failed;
        failed.set_message_id(78);
        failed.mutable_command()->set_success(false);
        std::string failedBytes;
        CHECK(failed.SerializeToString(&failedBytes));
        ModuleWire::EventReplyMsg core2;
        CHECK(ModuleWire::Decode(failedBytes, core2));
        CHECK(core2.MessageId == 78 && core2.HasCommand && !core2.Success && core2.ResultText.empty());

        forever_module::EventReply empty;
        empty.set_message_id(79);
        std::string emptyBytes;
        CHECK(empty.SerializeToString(&emptyBytes));
        ModuleWire::EventReplyMsg core3;
        CHECK(ModuleWire::Decode(emptyBytes, core3));
        CHECK(core3.MessageId == 79 && !core3.HasCommand);
    }

    // SDK -> core: SellItem and SendPlayerMessage action requests.
    {
        forever_module::Envelope sdk;
        sdk.set_message_id(31);
        forever_module::ActionRequest* req = sdk.mutable_action_request();
        req->set_message_id(31);
        forever_module::SellItemAction* sell = req->mutable_sell_item();
        sell->set_player_guid(42);
        sell->set_item_guid(5001);
        sell->set_item_entry(2589);
        sell->set_count(3);
        std::string bytes;
        CHECK(sdk.SerializeToString(&bytes));

        ModuleWire::Envelope env;
        CHECK(ModuleWire::DecodeEnvelope(bytes, env));
        CHECK(env.Kind == ModuleWire::PayloadKind::ActionRequest);
        CHECK(env.MessageId == 31);
        ModuleWire::ActionRequestMsg core;
        CHECK(ModuleWire::Decode(env.Payload, core));
        CHECK(core.MessageId == 31);
        CHECK(core.Kind == ModuleWire::ActionKind::SellItem);
        CHECK(core.PlayerGuid == 42);
        CHECK(core.ItemGuid == 5001);
        CHECK(core.ItemEntry == 2589);
        CHECK(core.Count == 3);

        forever_module::ActionRequest msg;
        msg.set_message_id(32);
        msg.mutable_send_message()->set_player_guid(42);
        msg.mutable_send_message()->set_text("sold for 60 copper");
        std::string msgBytes;
        CHECK(msg.SerializeToString(&msgBytes));
        ModuleWire::ActionRequestMsg core2;
        CHECK(ModuleWire::Decode(msgBytes, core2));
        CHECK(core2.MessageId == 32);
        CHECK(core2.Kind == ModuleWire::ActionKind::SendPlayerMessage);
        CHECK(core2.PlayerGuid == 42);
        CHECK(core2.Text == "sold for 60 copper");
    }

    // SDK -> core: ShareQuestItem action request.
    {
        forever_module::ActionRequest msg;
        msg.set_message_id(33);
        forever_module::ShareQuestItemAction* share = msg.mutable_share_quest_item();
        share->set_player_guid(42);
        share->set_item_entry(3000);
        share->set_count(1);
        share->set_range_yards(80);
        std::string bytes;
        CHECK(msg.SerializeToString(&bytes));
        ModuleWire::ActionRequestMsg core;
        CHECK(ModuleWire::Decode(bytes, core));
        CHECK(core.MessageId == 33);
        CHECK(core.Kind == ModuleWire::ActionKind::ShareQuestItem);
        CHECK(core.PlayerGuid == 42);
        CHECK(core.ItemEntry == 3000);
        CHECK(core.Count == 1);
        CHECK(core.RangeYards == 80);
    }

    // SDK -> core: Log action request.
    {
        forever_module::ActionRequest msg;
        msg.set_message_id(34);
        forever_module::LogAction* log = msg.mutable_log();
        log->set_level(1);
        log->set_text("42 items sold in the last 10 minutes. Total value 1g 20s 5c.");
        std::string bytes;
        CHECK(msg.SerializeToString(&bytes));
        ModuleWire::ActionRequestMsg core;
        CHECK(ModuleWire::Decode(bytes, core));
        CHECK(core.MessageId == 34);
        CHECK(core.Kind == ModuleWire::ActionKind::Log);
        CHECK(core.LogLevel == 1);
        CHECK(core.Text == "42 items sold in the last 10 minutes. Total value 1g 20s 5c.");
    }

    // Core -> SDK: ActionReply inside an Envelope, parsed by the SDK's protobuf.
    {
        ModuleWire::ActionReplyMsg reply;
        reply.MessageId = 31;
        reply.Success = true;
        reply.CopperGained = 60;
        reply.Recipients = 2;
        std::string bytes = ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::ActionReply, ModuleWire::Encode(reply), 31);

        forever_module::Envelope sdk;
        CHECK(sdk.ParseFromString(bytes));
        CHECK(sdk.payload_case() == forever_module::Envelope::kActionReply);
        CHECK(sdk.message_id() == 31);
        CHECK(sdk.action_reply().message_id() == 31);
        CHECK(sdk.action_reply().success());
        CHECK(sdk.action_reply().copper_gained() == 60);
        CHECK(sdk.action_reply().recipients() == 2);
        CHECK(!sdk.action_reply().has_reason());

        ModuleWire::ActionReplyMsg failed;
        failed.MessageId = 32;
        failed.Reason = "item is gone";
        std::string failedBytes = ModuleWire::Encode(failed);
        forever_module::ActionReply parsed;
        CHECK(parsed.ParseFromString(failedBytes));
        CHECK(parsed.message_id() == 32);
        CHECK(!parsed.success());
        CHECK(parsed.reason() == "item is gone");
        CHECK(parsed.copper_gained() == 0);
    }

    // Core -> SDK: PlayerItemLooted notify.
    {
        ModuleWire::ItemLootedEvent loot;
        loot.PlayerGuid = 42;
        loot.ItemGuid = 5001;
        loot.ItemEntry = 2589;
        loot.Count = 3;
        loot.Quality = 0;
        loot.ItemClass = 7;
        loot.SellPrice = 20;
        loot.ItemName = "Tattered Cloth";
        std::string body = ModuleWire::EncodeEventNotifyItemLooted("PlayerScript.OnLootItem", loot);

        forever_module::EventNotify notify;
        CHECK(notify.ParseFromString(body));
        CHECK(notify.hook_name() == "PlayerScript.OnLootItem");
        CHECK(notify.event_payload_case() == forever_module::EventNotify::kPlayerItemLooted);
        forever_module::PlayerItemLootedEvent const& sdk = notify.player_item_looted();
        CHECK(sdk.player_guid() == 42);
        CHECK(sdk.item_guid() == 5001);
        CHECK(sdk.item_entry() == 2589);
        CHECK(sdk.count() == 3);
        CHECK(sdk.quality() == 0);
        CHECK(sdk.item_class() == 7);
        CHECK(sdk.sell_price() == 20);
        CHECK(sdk.item_name() == "Tattered Cloth");
    }

    // SDK -> core: ControlRequest (spawn) and ReleaseControl.
    {
        forever_module::Envelope sdk;
        sdk.set_message_id(41);
        forever_module::ControlRequest* req = sdk.mutable_control_request();
        req->set_bot_guid(0);
        req->mutable_spawn_params()->set_race(2);
        req->mutable_spawn_params()->set_class_id(3);
        req->mutable_spawn_params()->set_level(80);
        req->mutable_spawn_params()->set_team(1);
        req->mutable_spawn_params()->set_place(true);
        req->mutable_spawn_params()->set_map_id(36);
        req->mutable_spawn_params()->set_pos_x(-16.5f);
        req->mutable_spawn_params()->set_pos_y(-383.0f);
        req->mutable_spawn_params()->set_pos_z(61.8f);
        std::string bytes;
        CHECK(sdk.SerializeToString(&bytes));

        ModuleWire::Envelope env;
        CHECK(ModuleWire::DecodeEnvelope(bytes, env));
        CHECK(env.Kind == ModuleWire::PayloadKind::ControlRequest);
        CHECK(env.MessageId == 41);
        ModuleWire::ControlRequestMsg core;
        CHECK(ModuleWire::Decode(env.Payload, core));
        CHECK(core.BotGuid == 0);
        CHECK(core.SpawnParams.Race == 2 && core.SpawnParams.ClassId == 3);
        CHECK(core.SpawnParams.Level == 80 && core.SpawnParams.Team == 1);
        CHECK(core.SpawnParams.Place && core.SpawnParams.MapId == 36);
        CHECK(core.SpawnParams.X == -16.5f && core.SpawnParams.Y == -383.0f && core.SpawnParams.Z == 61.8f);

        forever_module::Envelope release;
        release.mutable_release_control()->set_bot_guid(5005);
        std::string releaseBytes;
        CHECK(release.SerializeToString(&releaseBytes));
        ModuleWire::Envelope releaseEnv;
        CHECK(ModuleWire::DecodeEnvelope(releaseBytes, releaseEnv));
        CHECK(releaseEnv.Kind == ModuleWire::PayloadKind::ReleaseControl);
        ModuleWire::ReleaseControlMsg coreRelease;
        CHECK(ModuleWire::Decode(releaseEnv.Payload, coreRelease));
        CHECK(coreRelease.BotGuid == 5005);
    }

    // Core -> SDK: ControlResponse (granted and refused) and ControlRevoked.
    {
        ModuleWire::ControlResponseMsg granted;
        granted.Granted = true;
        granted.BotGuid = 5005;
        std::string bytes = ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::ControlResponse, ModuleWire::Encode(granted), 41);
        forever_module::Envelope sdk;
        CHECK(sdk.ParseFromString(bytes));
        CHECK(sdk.payload_case() == forever_module::Envelope::kControlResponse);
        CHECK(sdk.message_id() == 41);
        CHECK(sdk.control_response().granted());
        CHECK(sdk.control_response().bot_guid() == 5005);
        CHECK(!sdk.control_response().has_reason());

        ModuleWire::ControlResponseMsg refused;
        refused.Reason = "already controlled by module other";
        forever_module::ControlResponse parsed;
        CHECK(parsed.ParseFromString(ModuleWire::Encode(refused)));
        CHECK(!parsed.granted());
        CHECK(parsed.reason() == "already controlled by module other");

        ModuleWire::ControlRevokedMsg revoked;
        revoked.BotGuid = 5005;
        revoked.Reason = "bot deleted";
        std::string revokedBytes = ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::ControlRevoked, ModuleWire::Encode(revoked));
        forever_module::Envelope revokedSdk;
        CHECK(revokedSdk.ParseFromString(revokedBytes));
        CHECK(revokedSdk.payload_case() == forever_module::Envelope::kControlRevoked);
        CHECK(revokedSdk.control_revoked().bot_guid() == 5005);
        CHECK(revokedSdk.control_revoked().reason() == "bot deleted");
    }

    if (g_failures == 0)
    {
        std::printf("PASS  ModuleWire interop with SDK protobuf\n");
        return 0;
    }
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
}
