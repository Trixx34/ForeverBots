/*
 * Tier 1 wire codec. See ModuleWire.h for the contract and the protobuf-2.6.1 rationale.
 */

#include "ModuleWire.h"

namespace ModuleWire
{

namespace
{

constexpr uint32 WireVarint = 0;
constexpr uint32 WireFixed64 = 1;
constexpr uint32 WireLengthDelimited = 2;
constexpr uint32 WireFixed32 = 5;

class Reader
{
    public:
        explicit Reader(std::string_view data) : _data(data) { }

        bool AtEnd() const { return _pos >= _data.size(); }

        bool ReadVarint(uint64& out)
        {
            out = 0;
            for (uint32 shift = 0; shift < 64; shift += 7)
            {
                if (_pos >= _data.size())
                    return false;
                uint8 const byte = static_cast<uint8>(_data[_pos++]);
                out |= static_cast<uint64>(byte & 0x7F) << shift;
                if (!(byte & 0x80))
                    return true;
            }
            return false; // more than 10 bytes: malformed
        }

        bool ReadTag(uint32& field, uint32& wire)
        {
            uint64 tag = 0;
            if (!ReadVarint(tag))
                return false;
            field = static_cast<uint32>(tag >> 3);
            wire = static_cast<uint32>(tag & 0x7);
            return field != 0;
        }

        bool ReadBytes(std::string_view& out)
        {
            uint64 length = 0;
            if (!ReadVarint(length) || length > _data.size() - _pos)
                return false;
            out = _data.substr(_pos, static_cast<size_t>(length));
            _pos += static_cast<size_t>(length);
            return true;
        }

        // Skips a field value of the given wire type. Groups (types 3/4) are not supported.
        bool Skip(uint32 wire)
        {
            switch (wire)
            {
                case WireVarint:
                {
                    uint64 ignored = 0;
                    return ReadVarint(ignored);
                }
                case WireFixed64:
                    return Advance(8);
                case WireLengthDelimited:
                {
                    std::string_view ignored;
                    return ReadBytes(ignored);
                }
                case WireFixed32:
                    return Advance(4);
                default:
                    return false;
            }
        }

    private:
        bool Advance(size_t n)
        {
            if (n > _data.size() - _pos)
                return false;
            _pos += n;
            return true;
        }

        std::string_view _data;
        size_t _pos = 0;
};

void AppendVarint(std::string& out, uint64 value)
{
    while (value >= 0x80)
    {
        out.push_back(static_cast<char>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    out.push_back(static_cast<char>(value));
}

void AppendTag(std::string& out, uint32 field, uint32 wire)
{
    AppendVarint(out, (static_cast<uint64>(field) << 3) | wire);
}

void AppendVarintField(std::string& out, uint32 field, uint64 value)
{
    AppendTag(out, field, WireVarint);
    AppendVarint(out, value);
}

void AppendBytesField(std::string& out, uint32 field, std::string_view value)
{
    AppendTag(out, field, WireLengthDelimited);
    AppendVarint(out, value.size());
    out.append(value.data(), value.size());
}

bool IsPayloadField(uint32 field)
{
    switch (static_cast<PayloadKind>(field))
    {
        case PayloadKind::Shutdown:
        case PayloadKind::HandshakeRequest:
        case PayloadKind::HandshakeResponse:
        case PayloadKind::Heartbeat:
        case PayloadKind::ModuleConfig:
        case PayloadKind::EventNotify:
        case PayloadKind::EventRequest:
        case PayloadKind::EventReply:
        case PayloadKind::ControlRequest:
        case PayloadKind::ControlResponse:
        case PayloadKind::ReleaseControl:
        case PayloadKind::ControlRevoked:
            return true;
        default:
            return false;
    }
}

bool DecodeConfigEntry(std::string_view bytes, ConfigEntry& out)
{
    Reader r(bytes);
    while (!r.AtEnd())
    {
        uint32 field = 0, wire = 0;
        if (!r.ReadTag(field, wire))
            return false;

        if (field == 1 && wire == WireLengthDelimited)
        {
            std::string_view value;
            if (!r.ReadBytes(value))
                return false;
            out.Key.assign(value);
        }
        else if (field == 2 && wire == WireLengthDelimited)
        {
            std::string_view value;
            if (!r.ReadBytes(value))
                return false;
            out.Value.assign(value);
        }
        else if (!r.Skip(wire))
            return false;
    }
    return true;
}

} // namespace

std::string Frame(std::string_view body)
{
    std::string out;
    out.reserve(4 + body.size());
    auto const length = static_cast<uint32>(body.size());
    out.push_back(static_cast<char>(length & 0xFF));
    out.push_back(static_cast<char>((length >> 8) & 0xFF));
    out.push_back(static_cast<char>((length >> 16) & 0xFF));
    out.push_back(static_cast<char>((length >> 24) & 0xFF));
    out.append(body.data(), body.size());
    return out;
}

std::string EncodeEnvelope(PayloadKind kind, std::string_view body, uint64 messageId)
{
    std::string out;
    if (messageId != 0)
        AppendVarintField(out, 1, messageId);
    AppendBytesField(out, static_cast<uint32>(kind), body);
    return out;
}

bool DecodeEnvelope(std::string_view bytes, Envelope& out)
{
    Reader r(bytes);
    while (!r.AtEnd())
    {
        uint32 field = 0, wire = 0;
        if (!r.ReadTag(field, wire))
            return false;

        if (field == 1 && wire == WireVarint)
        {
            if (!r.ReadVarint(out.MessageId))
                return false;
        }
        else if (IsPayloadField(field) && wire == WireLengthDelimited)
        {
            // oneof: the last member on the wire wins, as in protobuf.
            std::string_view payload;
            if (!r.ReadBytes(payload))
                return false;
            out.Kind = static_cast<PayloadKind>(field);
            out.Payload.assign(payload);
        }
        else if (!r.Skip(wire))
            return false;
    }
    return true;
}

std::string Encode(HandshakeResponse const& msg)
{
    std::string out;
    AppendVarintField(out, 1, msg.Accepted ? 1 : 0);
    if (!msg.Reason.empty())
        AppendBytesField(out, 2, msg.Reason);
    AppendVarintField(out, 3, msg.ModuleId);
    if (!msg.Tier2RegionName.empty())
        AppendBytesField(out, 4, msg.Tier2RegionName);
    AppendVarintField(out, 5, msg.NegotiatedMaxBots);
    AppendVarintField(out, 6, msg.NegotiatedMaxNearbyUnits);
    AppendVarintField(out, 7, msg.NegotiatedMaxActions);
    AppendVarintField(out, 8, msg.NegotiatedTickInterval);
    return out;
}

std::string Encode(std::vector<ConfigEntry> const& values)
{
    std::string out;
    for (ConfigEntry const& entry : values)
    {
        std::string sub;
        AppendBytesField(sub, 1, entry.Key);
        AppendBytesField(sub, 2, entry.Value);
        AppendBytesField(out, 1, sub);
    }
    return out;
}

std::string Encode(Heartbeat const& msg)
{
    std::string out;
    AppendVarintField(out, 1, msg.Sequence);
    return out;
}

std::string Encode(Shutdown const& msg)
{
    std::string out;
    if (!msg.Reason.empty())
        AppendBytesField(out, 1, msg.Reason);
    return out;
}

bool Decode(std::string_view bytes, HandshakeRequest& out)
{
    Reader r(bytes);
    while (!r.AtEnd())
    {
        uint32 field = 0, wire = 0;
        if (!r.ReadTag(field, wire))
            return false;

        std::string_view value;
        uint64 number = 0;
        if (field >= 1 && field <= 5 && wire == WireLengthDelimited)
        {
            if (!r.ReadBytes(value))
                return false;
            switch (field)
            {
                case 1: out.ModuleName.assign(value); break;
                case 2: out.ModuleVersion.assign(value); break;
                case 3: out.ProtocolVersion.assign(value); break;
                case 4: out.Tier1Hooks.emplace_back(value); break;
                case 5: out.SdkPackageVersion.assign(value); break;
            }
        }
        else if (field == 10 && wire == WireVarint)
        {
            if (!r.ReadVarint(number))
                return false;
            out.Tier2Participates = number != 0;
        }
        else if (field >= 11 && field <= 14 && wire == WireVarint)
        {
            if (!r.ReadVarint(number))
                return false;
            switch (field)
            {
                case 11: out.RequestedMaxBots = static_cast<uint32>(number); break;
                case 12: out.RequestedMaxNearbyUnits = static_cast<uint32>(number); break;
                case 13: out.RequestedMaxActions = static_cast<uint32>(number); break;
                case 14: out.RequestedTickInterval = static_cast<uint32>(number); break;
            }
        }
        else if (!r.Skip(wire))
            return false;
    }
    return true;
}

bool Decode(std::string_view bytes, Heartbeat& out)
{
    Reader r(bytes);
    while (!r.AtEnd())
    {
        uint32 field = 0, wire = 0;
        if (!r.ReadTag(field, wire))
            return false;

        if (field == 1 && wire == WireVarint)
        {
            if (!r.ReadVarint(out.Sequence))
                return false;
        }
        else if (!r.Skip(wire))
            return false;
    }
    return true;
}

bool Decode(std::string_view bytes, std::vector<ConfigEntry>& out)
{
    Reader r(bytes);
    while (!r.AtEnd())
    {
        uint32 field = 0, wire = 0;
        if (!r.ReadTag(field, wire))
            return false;

        if (field == 1 && wire == WireLengthDelimited)
        {
            std::string_view entryBytes;
            if (!r.ReadBytes(entryBytes))
                return false;
            ConfigEntry entry;
            if (!DecodeConfigEntry(entryBytes, entry))
                return false;
            out.push_back(std::move(entry));
        }
        else if (!r.Skip(wire))
            return false;
    }
    return true;
}

} // namespace ModuleWire
