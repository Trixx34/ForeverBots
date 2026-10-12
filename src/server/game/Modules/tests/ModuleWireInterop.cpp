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
        req->set_protocol_version("1");
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
        CHECK(core.ProtocolVersion == "1");
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

    if (g_failures == 0)
    {
        std::printf("PASS  ModuleWire interop with SDK protobuf\n");
        return 0;
    }
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
}
