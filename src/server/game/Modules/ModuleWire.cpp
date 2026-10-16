/*
 * Tier 1 wire codec. See ModuleWire.h for the contract and the protobuf-2.6.1 rationale.
 */

#include "ModuleWire.h"
#include <cstring>

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

        bool ReadFloat(float& out)
        {
            if (_data.size() - _pos < 4)
                return false;
            uint32 bits = 0;
            for (int i = 0; i < 4; ++i)
                bits |= static_cast<uint32>(static_cast<uint8>(_data[_pos + i])) << (8 * i);
            _pos += 4;
            std::memcpy(&out, &bits, sizeof(out));
            return true;
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

void AppendFloatField(std::string& out, uint32 field, float value)
{
    AppendTag(out, field, 5); // wire type 5: fixed32
    uint32 bits;
    std::memcpy(&bits, &value, sizeof(bits));
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<char>((bits >> (8 * i)) & 0xFF));
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
        case PayloadKind::ActionRequest:
        case PayloadKind::ActionReply:
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

std::string EncodeEventNotify(std::string_view hookName, NotifyKind kind, uint64 value)
{
    std::string payload;
    if (kind != NotifyKind::WorldStartup && kind != NotifyKind::WorldShutdown)
    {
        // PlayerLoginEvent/PlayerLogoutEvent use player_guid = 1; AccountLoginEvent uses account_id = 1.
        AppendVarintField(payload, 1, value);
    }

    std::string out;
    AppendBytesField(out, 1, hookName);
    AppendBytesField(out, static_cast<uint32>(kind), payload);
    return out;
}

std::string EncodeEventNotifyChat(std::string_view hookName, ChatEvent const& event)
{
    std::string payload;
    AppendVarintField(payload, 1, event.PlayerGuid);
    AppendVarintField(payload, 2, event.Channel);
    AppendBytesField(payload, 3, event.Text);
    if (event.RecipientGuid != 0)
        AppendVarintField(payload, 4, event.RecipientGuid);
    AppendVarintField(payload, 5, event.MapId);
    AppendFloatField(payload, 6, event.PosX);
    AppendFloatField(payload, 7, event.PosY);
    AppendFloatField(payload, 8, event.PosZ);
    if (event.TargetRef != 0)
        AppendVarintField(payload, 9, event.TargetRef);

    std::string out;
    AppendBytesField(out, 1, hookName);
    AppendBytesField(out, static_cast<uint32>(NotifyKind::PlayerChat), payload);
    return out;
}

std::string EncodeEventNotifyItemLooted(std::string_view hookName, ItemLootedEvent const& event)
{
    std::string payload;
    AppendVarintField(payload, 1, event.PlayerGuid);
    AppendVarintField(payload, 2, event.ItemGuid);
    AppendVarintField(payload, 3, event.ItemEntry);
    AppendVarintField(payload, 4, event.Count);
    AppendVarintField(payload, 5, event.Quality);
    AppendVarintField(payload, 6, event.ItemClass);
    AppendVarintField(payload, 7, event.SellPrice);
    if (!event.ItemName.empty())
        AppendBytesField(payload, 8, event.ItemName);

    std::string out;
    AppendBytesField(out, 1, hookName);
    AppendBytesField(out, static_cast<uint32>(NotifyKind::PlayerItemLooted), payload);
    return out;
}

std::string EncodeEventRequestCommand(uint64 messageId, std::string_view hookName, std::string_view commandText,
                                      uint64 issuerGuid)
{
    std::string command;
    AppendBytesField(command, 1, commandText);
    AppendVarintField(command, 2, issuerGuid);

    std::string out;
    AppendVarintField(out, 1, messageId);
    AppendBytesField(out, 2, hookName);
    AppendBytesField(out, 10, command);
    return out;
}

bool Decode(std::string_view bytes, EventReplyMsg& out)
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
        else if (field == 10 && wire == WireLengthDelimited)
        {
            // CommandReplyPayload: success = 1 (bool), result_text = 2 (string).
            std::string_view commandBytes;
            if (!r.ReadBytes(commandBytes))
                return false;
            out.HasCommand = true;

            Reader c(commandBytes);
            while (!c.AtEnd())
            {
                uint32 cf = 0, cw = 0;
                if (!c.ReadTag(cf, cw))
                    return false;
                if (cf == 1 && cw == WireVarint)
                {
                    uint64 success = 0;
                    if (!c.ReadVarint(success))
                        return false;
                    out.Success = success != 0;
                }
                else if (cf == 2 && cw == WireLengthDelimited)
                {
                    std::string_view text;
                    if (!c.ReadBytes(text))
                        return false;
                    out.ResultText.assign(text);
                }
                else if (!c.Skip(cw))
                    return false;
            }
        }
        else if (!r.Skip(wire))
            return false;
    }
    return true;
}

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

bool Decode(std::string_view bytes, ActionRequestMsg& out)
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
        else if (field >= 12 && field <= 15 && wire == WireLengthDelimited)
        {
            // StoreGetAction (12): key=1. StoreSetAction (13): key=1, value=2. StoreDeleteAction (14): key=1.
            // StoreListAction (15): prefix=1 (carried in Key).
            std::string_view storeBytes;
            if (!r.ReadBytes(storeBytes))
                return false;
            out.Kind = field == 12   ? ActionKind::StoreGet
                       : field == 13 ? ActionKind::StoreSet
                       : field == 14 ? ActionKind::StoreDelete
                                     : ActionKind::StoreList;

            Reader s(storeBytes);
            while (!s.AtEnd())
            {
                uint32 sf = 0, sw = 0;
                if (!s.ReadTag(sf, sw))
                    return false;
                if (sf == 1 && sw == WireLengthDelimited)
                {
                    std::string_view key;
                    if (!s.ReadBytes(key))
                        return false;
                    out.Key.assign(key);
                }
                else if (field == 13 && sf == 2 && sw == WireLengthDelimited)
                {
                    std::string_view value;
                    if (!s.ReadBytes(value))
                        return false;
                    out.Value.assign(value);
                }
                else if (!s.Skip(sw))
                    return false;
            }
        }
        else if (field == 17 && wire == WireLengthDelimited)
        {
            // LogAction (17): level=1 (varint), text=2.
            std::string_view logBytes;
            if (!r.ReadBytes(logBytes))
                return false;
            out.Kind = ActionKind::Log;

            Reader a(logBytes);
            while (!a.AtEnd())
            {
                uint32 af = 0, aw = 0;
                if (!a.ReadTag(af, aw))
                    return false;
                if (af == 1 && aw == WireVarint)
                {
                    uint64 level = 0;
                    if (!a.ReadVarint(level))
                        return false;
                    out.LogLevel = static_cast<uint32>(level);
                }
                else if (af == 2 && aw == WireLengthDelimited)
                {
                    std::string_view text;
                    if (!a.ReadBytes(text))
                        return false;
                    out.Text.assign(text);
                }
                else if (!a.Skip(aw))
                    return false;
            }
        }
        else if (field == 16 && wire == WireLengthDelimited)
        {
            // ShareQuestItemAction (16): player_guid=1, item_entry=2, count=3, range_yards=4.
            std::string_view shareBytes;
            if (!r.ReadBytes(shareBytes))
                return false;
            out.Kind = ActionKind::ShareQuestItem;

            Reader a(shareBytes);
            while (!a.AtEnd())
            {
                uint32 af = 0, aw = 0;
                if (!a.ReadTag(af, aw))
                    return false;
                if (aw == WireVarint && af >= 1 && af <= 4)
                {
                    uint64 value = 0;
                    if (!a.ReadVarint(value))
                        return false;
                    if (af == 1)
                        out.PlayerGuid = value;
                    else if (af == 2)
                        out.ItemEntry = static_cast<uint32>(value);
                    else if (af == 3)
                        out.Count = static_cast<uint32>(value);
                    else
                        out.RangeYards = static_cast<uint32>(value);
                }
                else if (!a.Skip(aw))
                    return false;
            }
        }
        else if (field == 18 && wire == WireLengthDelimited)
        {
            // BuyItemAction (18): vendor_guid=1, item_entry=2, count=3.
            std::string_view buyBytes;
            if (!r.ReadBytes(buyBytes))
                return false;
            out.Kind = ActionKind::BuyItem;

            Reader a(buyBytes);
            while (!a.AtEnd())
            {
                uint32 af = 0, aw = 0;
                if (!a.ReadTag(af, aw))
                    return false;
                if (af == 1 && aw == WireVarint)
                {
                    if (!a.ReadVarint(out.VendorGuid))
                        return false;
                }
                else if (af == 2 && aw == WireVarint)
                {
                    uint64 entry = 0;
                    if (!a.ReadVarint(entry))
                        return false;
                    out.ItemEntry = static_cast<uint32>(entry);
                }
                else if (af == 3 && aw == WireVarint)
                {
                    uint64 count = 0;
                    if (!a.ReadVarint(count))
                        return false;
                    out.Count = static_cast<uint32>(count);
                }
                else if (!a.Skip(aw))
                    return false;
            }
        }
        else if (field == 19 && wire == WireLengthDelimited)
        {
            // AuctionHouseAction (19): op=1, auctioneer_guid=2, item_guid=3, auction_guid=4,
            // item_entry=5, count=6, bid=7, buyout=8.
            std::string_view ahBytes;
            if (!r.ReadBytes(ahBytes))
                return false;
            out.Kind = ActionKind::AuctionHouse;

            Reader a(ahBytes);
            while (!a.AtEnd())
            {
                uint32 af = 0, aw = 0;
                if (!a.ReadTag(af, aw))
                    return false;
                if (af == 1 && aw == WireVarint)
                {
                    uint64 op = 0;
                    if (!a.ReadVarint(op))
                        return false;
                    out.AhOp = static_cast<uint32>(op);
                }
                else if (af == 2 && aw == WireVarint)
                {
                    if (!a.ReadVarint(out.AuctioneerGuid))
                        return false;
                }
                else if (af == 3 && aw == WireVarint)
                {
                    if (!a.ReadVarint(out.ItemGuid))
                        return false;
                }
                else if (af == 4 && aw == WireVarint)
                {
                    if (!a.ReadVarint(out.AuctionGuid))
                        return false;
                }
                else if (af == 5 && aw == WireVarint)
                {
                    uint64 entry = 0;
                    if (!a.ReadVarint(entry))
                        return false;
                    out.ItemEntry = static_cast<uint32>(entry);
                }
                else if (af == 6 && aw == WireVarint)
                {
                    uint64 count = 0;
                    if (!a.ReadVarint(count))
                        return false;
                    out.Count = static_cast<uint32>(count);
                }
                else if (af == 7 && aw == WireVarint)
                {
                    uint64 bid = 0;
                    if (!a.ReadVarint(bid))
                        return false;
                    out.Bid = static_cast<uint32>(bid);
                }
                else if (af == 8 && aw == WireVarint)
                {
                    uint64 buyout = 0;
                    if (!a.ReadVarint(buyout))
                        return false;
                    out.Buyout = static_cast<uint32>(buyout);
                }
                else if (!a.Skip(aw))
                    return false;
            }
        }
        else if ((field == 10 || field == 11) && wire == WireLengthDelimited)
        {
            // SellItemAction (10): player_guid=1, item_guid=2, item_entry=3, count=4.
            // SendPlayerMessageAction (11): player_guid=1, text=2.
            std::string_view actionBytes;
            if (!r.ReadBytes(actionBytes))
                return false;
            out.Kind = field == 10 ? ActionKind::SellItem : ActionKind::SendPlayerMessage;

            Reader a(actionBytes);
            while (!a.AtEnd())
            {
                uint32 af = 0, aw = 0;
                if (!a.ReadTag(af, aw))
                    return false;
                if (af == 1 && aw == WireVarint)
                {
                    if (!a.ReadVarint(out.PlayerGuid))
                        return false;
                }
                else if (field == 10 && af == 2 && aw == WireVarint)
                {
                    if (!a.ReadVarint(out.ItemGuid))
                        return false;
                }
                else if (field == 10 && af == 3 && aw == WireVarint)
                {
                    uint64 entry = 0;
                    if (!a.ReadVarint(entry))
                        return false;
                    out.ItemEntry = static_cast<uint32>(entry);
                }
                else if (field == 10 && af == 4 && aw == WireVarint)
                {
                    uint64 count = 0;
                    if (!a.ReadVarint(count))
                        return false;
                    out.Count = static_cast<uint32>(count);
                }
                else if (field == 11 && af == 2 && aw == WireLengthDelimited)
                {
                    std::string_view text;
                    if (!a.ReadBytes(text))
                        return false;
                    out.Text.assign(text);
                }
                else if (!a.Skip(aw))
                    return false;
            }
        }
        else if (!r.Skip(wire))
            return false;
    }
    return true;
}

bool Decode(std::string_view bytes, ControlRequestMsg& out)
{
    Reader r(bytes);
    while (!r.AtEnd())
    {
        uint32 field = 0, wire = 0;
        if (!r.ReadTag(field, wire))
            return false;

        if (field == 1 && wire == WireVarint)
        {
            if (!r.ReadVarint(out.BotGuid))
                return false;
        }
        else if (field == 2 && wire == WireLengthDelimited)
        {
            // SpawnParams: race=1, class_id=2, level=3, team=4, place=5, map_id=6 (varint), pos_x/y/z=7,8,9 (float).
            std::string_view spawnBytes;
            if (!r.ReadBytes(spawnBytes))
                return false;
            Reader s(spawnBytes);
            while (!s.AtEnd())
            {
                uint32 sf = 0, sw = 0;
                if (!s.ReadTag(sf, sw))
                    return false;
                uint64 value = 0;
                if (sf >= 7 && sf <= 9 && sw == WireFixed32)
                {
                    float v = 0.0f;
                    if (!s.ReadFloat(v))
                        return false;
                    (sf == 7 ? out.SpawnParams.X : sf == 8 ? out.SpawnParams.Y : out.SpawnParams.Z) = v;
                }
                else if ((sf == 1 || sf == 2 || sf == 3 || sf == 4 || sf == 5 || sf == 6) && sw == WireVarint)
                {
                    if (!s.ReadVarint(value))
                        return false;
                    uint32 const v = static_cast<uint32>(value);
                    switch (sf)
                    {
                        case 1: out.SpawnParams.Race = v; break;
                        case 2: out.SpawnParams.ClassId = v; break;
                        case 3: out.SpawnParams.Level = v; break;
                        case 4: out.SpawnParams.Team = v; break;
                        case 5: out.SpawnParams.Place = v != 0; break;
                        case 6: out.SpawnParams.MapId = v; break;
                    }
                }
                else if (sf == 10 && sw == WireLengthDelimited)
                {
                    std::string_view name;
                    if (!s.ReadBytes(name))
                        return false;
                    out.SpawnParams.AltName.assign(name.data(), name.size());
                }
                else if (sf == 11 && sw == WireVarint)
                {
                    if (!s.ReadVarint(out.SpawnParams.OwnerGuid))
                        return false;
                }
                else if (!s.Skip(sw))
                    return false;
            }
        }
        else if (!r.Skip(wire))
            return false;
    }
    return true;
}

bool Decode(std::string_view bytes, ReleaseControlMsg& out)
{
    Reader r(bytes);
    while (!r.AtEnd())
    {
        uint32 field = 0, wire = 0;
        if (!r.ReadTag(field, wire))
            return false;

        if (field == 1 && wire == WireVarint)
        {
            if (!r.ReadVarint(out.BotGuid))
                return false;
        }
        else if (!r.Skip(wire))
            return false;
    }
    return true;
}

std::string Encode(ControlResponseMsg const& msg)
{
    std::string out;
    AppendVarintField(out, 1, msg.Granted ? 1 : 0);
    AppendVarintField(out, 2, msg.BotGuid);
    if (!msg.Reason.empty())
        AppendBytesField(out, 3, msg.Reason);
    if (msg.Race != 0)
        AppendVarintField(out, 4, msg.Race);
    if (msg.ClassId != 0)
        AppendVarintField(out, 5, msg.ClassId);
    return out;
}

std::string Encode(ControlRevokedMsg const& msg)
{
    std::string out;
    AppendVarintField(out, 1, msg.BotGuid);
    if (!msg.Reason.empty())
        AppendBytesField(out, 2, msg.Reason);
    return out;
}

std::string Encode(ActionReplyMsg const& msg)
{
    std::string out;
    AppendVarintField(out, 1, msg.MessageId);
    AppendVarintField(out, 2, msg.Success ? 1 : 0);
    if (!msg.Reason.empty())
        AppendBytesField(out, 3, msg.Reason);
    AppendVarintField(out, 4, msg.CopperGained);
    AppendVarintField(out, 9, msg.CopperSpent);
    if (msg.Found)
        AppendVarintField(out, 5, 1);
    if (!msg.Value.empty())
        AppendBytesField(out, 6, msg.Value);
    for (std::string const& key : msg.Keys)
        AppendBytesField(out, 7, key);
    if (msg.Recipients)
        AppendVarintField(out, 8, msg.Recipients);
    return out;
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
