/*
 * Tier 1 wire codec for the module system (doc/module-system/03-phase1-protocol-design.md).
 *
 * Hand-written proto2 wire format for the messages the core host handles. Field numbers and types
 * must match the forever-module-sdk proto files exactly. The core bundles protobuf 2.6.1, which has no
 * protoc frontend in-tree, so generated code is not an option; ModuleWire is verified against the
 * SDK's protobuf in src/server/game/Modules/tests/ModuleWireInterop.cpp.
 *
 * Decoding follows protobuf rules: unknown fields are skipped by wire type, and the last occurrence
 * of a scalar wins. Envelope payloads are kept as raw bytes and decoded by the caller, so payload
 * types this core doesn't handle yet are carried through without being parsed.
 */

#ifndef TRINITYCORE_MODULE_WIRE_H
#define TRINITYCORE_MODULE_WIRE_H

#include "Define.h"
#include <string>
#include <string_view>
#include <vector>

namespace ModuleWire
{

// Frames are a 4-byte little-endian body length followed by the body.
inline constexpr uint32 MaxFrameBytes = 1024 * 1024;

// Oneof field numbers of forever_module.Envelope.payload.
enum class PayloadKind : uint32
{
    None = 0,
    Shutdown = 30,
    HandshakeRequest = 10,
    HandshakeResponse = 11,
    Heartbeat = 12,
    ModuleConfig = 13,
    EventNotify = 20,
    EventRequest = 21,
    EventReply = 22,
    ControlRequest = 40,
    ControlResponse = 41,
    ReleaseControl = 42,
    ControlRevoked = 43,
};

struct Envelope
{
    uint64 MessageId = 0;
    PayloadKind Kind = PayloadKind::None;
    std::string Payload; // serialized body of the oneof member; decode with the matching Decode*
};

struct HandshakeRequest
{
    std::string ModuleName;
    std::string ModuleVersion;
    std::string ProtocolVersion;
    std::vector<std::string> Tier1Hooks;
    std::string SdkPackageVersion;
    bool Tier2Participates = false;
    uint32 RequestedMaxBots = 0;
    uint32 RequestedMaxNearbyUnits = 0;
    uint32 RequestedMaxActions = 0;
    uint32 RequestedTickInterval = 0;
};

struct HandshakeResponse
{
    bool Accepted = false;
    std::string Reason;
    uint64 ModuleId = 0;
    std::string Tier2RegionName;
    uint32 NegotiatedMaxBots = 0;
    uint32 NegotiatedMaxNearbyUnits = 0;
    uint32 NegotiatedMaxActions = 0;
    uint32 NegotiatedTickInterval = 0;
};

struct ConfigEntry
{
    std::string Key;
    std::string Value;
};

struct Heartbeat
{
    uint64 Sequence = 0;
};

struct Shutdown
{
    std::string Reason;
};

// Envelope framing. EncodeEnvelope wraps an already-encoded payload body; use the Encode* helpers
// below to produce bodies.
std::string EncodeEnvelope(PayloadKind kind, std::string_view body, uint64 messageId = 0);
bool DecodeEnvelope(std::string_view bytes, Envelope& out);

// Payload body encoders.
std::string Encode(HandshakeResponse const& msg);
std::string Encode(std::vector<ConfigEntry> const& values); // ModuleConfig body
std::string Encode(Heartbeat const& msg);
std::string Encode(Shutdown const& msg);

// Payload body decoders. Return false on malformed input.
bool Decode(std::string_view bytes, HandshakeRequest& out);
bool Decode(std::string_view bytes, Heartbeat& out);
bool Decode(std::string_view bytes, std::vector<ConfigEntry>& out); // ModuleConfig body

// EventNotify (core -> module). The oneof member numbers are the EventNotify payload fields in events.proto.
enum class NotifyKind : uint32
{
    PlayerLogin = 10,
    PlayerLogout = 11,
    WorldStartup = 12,
    WorldShutdown = 13,
    AccountLogin = 14,
};

// Body of an EventNotify. `value` is the player GUID for player events, the account id for AccountLogin,
// and ignored for world events.
std::string EncodeEventNotify(std::string_view hookName, NotifyKind kind, uint64 value);

// Body of an EventRequest carrying a CommandRequestPayload.
std::string EncodeEventRequestCommand(uint64 messageId, std::string_view hookName, std::string_view commandText,
                                      uint64 issuerGuid);

// EventReply body. Success is only meaningful when HasCommand is set; see the command routing rules
// in ModuleMgr::RouteCommand.
struct EventReplyMsg
{
    uint64 MessageId = 0;
    bool HasCommand = false;
    bool Success = false;
    std::string ResultText;
};

bool Decode(std::string_view bytes, EventReplyMsg& out);

// Frame helpers for a byte stream: prepend the length prefix.
std::string Frame(std::string_view body);

} // namespace ModuleWire

#endif // TRINITYCORE_MODULE_WIRE_H
