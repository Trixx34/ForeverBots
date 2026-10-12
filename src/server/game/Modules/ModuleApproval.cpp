#include "ModuleApproval.h"

#include "CryptoHash.h"
#include "ModuleManifestInfo.h"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;
namespace pt = boost::property_tree;

namespace
{

std::string JsonString(std::string const& value)
{
    std::string out = "\"";
    for (char c : value)
    {
        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    std::ostringstream escaped;
                    escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(static_cast<unsigned char>(c));
                    out += escaped.str();
                }
                else
                    out += c;
        }
    }
    out += '"';
    return out;
}

std::string HexEncode(std::array<uint8, 32> const& bytes)
{
    static char const digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (uint8 b : bytes)
    {
        out += digits[b >> 4];
        out += digits[b & 0xF];
    }
    return out;
}

} // namespace

CapabilityDeclaration CapabilitiesOf(ModuleManifestInfo const& manifest)
{
    CapabilityDeclaration c;
    c.Hooks = manifest.Tier1Hooks;
    std::sort(c.Hooks.begin(), c.Hooks.end());
    c.Hooks.erase(std::unique(c.Hooks.begin(), c.Hooks.end()), c.Hooks.end());

    c.Tier2Participates = manifest.Tier2Participates;
    c.RequestedMaxBots = manifest.RequestedMaxBots;
    c.RequestedMaxNearbyUnits = manifest.RequestedMaxNearbyUnits;
    c.RequestedMaxActions = manifest.RequestedMaxActions;
    c.RequestedTickInterval = manifest.RequestedTickInterval;
    return c;
}

std::string CanonicalCapabilityJson(CapabilityDeclaration const& capabilities)
{
    // Hooks are sorted and unique by CapabilitiesOf(); sort again here so a hand-built declaration hashes the same.
    std::vector<std::string> hooks = capabilities.Hooks;
    std::sort(hooks.begin(), hooks.end());
    hooks.erase(std::unique(hooks.begin(), hooks.end()), hooks.end());

    std::string out = "{\"tier1\":{\"hooks\":[";
    for (size_t i = 0; i < hooks.size(); ++i)
    {
        if (i)
            out += ',';
        out += JsonString(hooks[i]);
    }
    out += "]},\"tier2\":{";
    out += "\"participates\":";
    out += capabilities.Tier2Participates ? "true" : "false";
    out += ",\"requested_max_actions\":" + std::to_string(capabilities.RequestedMaxActions);
    out += ",\"requested_max_bots\":" + std::to_string(capabilities.RequestedMaxBots);
    out += ",\"requested_max_nearby_units\":" + std::to_string(capabilities.RequestedMaxNearbyUnits);
    out += ",\"requested_tick_interval\":" + std::to_string(capabilities.RequestedTickInterval);
    out += "}}";
    return out;
}

std::string CapabilityHash(CapabilityDeclaration const& capabilities)
{
    std::string const json = CanonicalCapabilityJson(capabilities);
    return HexEncode(Trinity::Crypto::SHA256::GetDigestOf(json));
}

std::vector<std::string> DescribeCapabilityChanges(CapabilityDeclaration const& approved, CapabilityDeclaration const& current)
{
    std::vector<std::string> changes;

    for (std::string const& hook : current.Hooks)
        if (std::find(approved.Hooks.begin(), approved.Hooks.end(), hook) == approved.Hooks.end())
            changes.push_back("adds Tier 1 hook " + hook);
    for (std::string const& hook : approved.Hooks)
        if (std::find(current.Hooks.begin(), current.Hooks.end(), hook) == current.Hooks.end())
            changes.push_back("removes Tier 1 hook " + hook);

    if (approved.Tier2Participates != current.Tier2Participates)
        changes.push_back(current.Tier2Participates ? "enables Tier 2 participation" : "disables Tier 2 participation");

    auto const compare = [&changes](char const* name, uint32 from, uint32 to)
    {
        if (from != to)
            changes.push_back(std::string("changes ") + name + " from " + std::to_string(from) + " to " + std::to_string(to));
    };
    compare("requested_max_bots", approved.RequestedMaxBots, current.RequestedMaxBots);
    compare("requested_max_nearby_units", approved.RequestedMaxNearbyUnits, current.RequestedMaxNearbyUnits);
    compare("requested_max_actions", approved.RequestedMaxActions, current.RequestedMaxActions);
    compare("requested_tick_interval", approved.RequestedTickInterval, current.RequestedTickInterval);

    if (changes.empty())
        changes.push_back("capability declaration changed");
    return changes;
}

bool ApprovalStore::Load(std::string const& path, std::string& error)
{
    _records.clear();

    std::error_code ec;
    if (!fs::exists(path, ec))
        return true;

    try
    {
        pt::ptree tree;
        pt::read_json(path, tree);

        for (auto const& [name, body] : tree)
        {
            ApprovalRecord record;
            record.ApprovedVersion = body.get<std::string>("approved_version");
            record.ApprovedAt = body.get<std::string>("approved_at");
            record.CapabilityHash = body.get<std::string>("capability_hash");

            CapabilityDeclaration& c = record.Capabilities;
            for (auto const& [ignored, hook] : body.get_child("capabilities.hooks"))
                c.Hooks.push_back(hook.get_value<std::string>());
            c.Tier2Participates = body.get<int>("capabilities.tier2_participates") != 0;
            c.RequestedMaxBots = body.get<uint32>("capabilities.requested_max_bots");
            c.RequestedMaxNearbyUnits = body.get<uint32>("capabilities.requested_max_nearby_units");
            c.RequestedMaxActions = body.get<uint32>("capabilities.requested_max_actions");
            c.RequestedTickInterval = body.get<uint32>("capabilities.requested_tick_interval");

            _records[name] = std::move(record);
        }
    }
    catch (std::exception const& e)
    {
        _records.clear();
        error = path + " is unreadable or malformed: " + e.what();
        return false;
    }
    return true;
}

bool ApprovalStore::Save(std::string const& path, std::string& error) const
{
    pt::ptree tree;
    for (auto const& [name, record] : _records)
    {
        pt::ptree body;
        body.put("approved_version", record.ApprovedVersion);
        body.put("approved_at", record.ApprovedAt);
        body.put("capability_hash", record.CapabilityHash);

        pt::ptree hooks;
        for (std::string const& hook : record.Capabilities.Hooks)
            hooks.push_back({"", pt::ptree(hook)});
        pt::ptree capabilities;
        capabilities.add_child("hooks", hooks);
        capabilities.put("tier2_participates", record.Capabilities.Tier2Participates ? 1 : 0);
        capabilities.put("requested_max_bots", record.Capabilities.RequestedMaxBots);
        capabilities.put("requested_max_nearby_units", record.Capabilities.RequestedMaxNearbyUnits);
        capabilities.put("requested_max_actions", record.Capabilities.RequestedMaxActions);
        capabilities.put("requested_tick_interval", record.Capabilities.RequestedTickInterval);
        body.add_child("capabilities", capabilities);

        tree.add_child(pt::ptree::path_type(name, '/'), body);
    }

    // Write beside the target and rename over it, so a crash mid-write cannot leave a truncated store.
    std::string const temp = path + ".tmp";
    try
    {
        pt::write_json(temp, tree);
    }
    catch (std::exception const& e)
    {
        error = "cannot write " + temp + ": " + e.what();
        return false;
    }

    std::error_code ec;
    fs::rename(temp, path, ec);
    if (ec)
    {
        error = "cannot replace " + path + ": " + ec.message();
        return false;
    }
    return true;
}

ApprovalRecord const* ApprovalStore::Find(std::string const& name) const
{
    auto it = _records.find(name);
    return it == _records.end() ? nullptr : &it->second;
}

void ApprovalStore::Set(std::string const& name, ApprovalRecord record)
{
    _records[name] = std::move(record);
}

bool IsValidModuleName(std::string const& name)
{
    if (name.empty() || name.size() > 64)
        return false;
    return std::all_of(name.begin(), name.end(), [](char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}

std::string UtcTimestampNow()
{
    std::time_t const now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
    gmtime_r(&now, &utc);
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}
