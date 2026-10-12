#include "ModuleManifestInfo.h"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <sstream>

bool ParseModuleManifest(std::string const& json, ModuleManifestInfo& out, std::string& error)
{
    boost::property_tree::ptree tree;
    try
    {
        std::istringstream stream(json);
        boost::property_tree::read_json(stream, tree);
    }
    catch (boost::property_tree::json_parser_error const& e)
    {
        error = std::string("malformed module.json: ") + e.message();
        return false;
    }

    out.Name = tree.get<std::string>("name", "");
    out.Version = tree.get<std::string>("version", "");
    out.ProtocolVersion = tree.get<std::string>("protocol_version", "");
    out.SdkPackageVersion = tree.get<std::string>("sdk_package_version", "");

    if (out.Name.empty() || out.Version.empty() || out.ProtocolVersion.empty())
    {
        error = "module.json must set name, version and protocol_version";
        return false;
    }

    out.Tier1Hooks.clear();
    if (auto hooks = tree.get_child_optional("tier1.hooks"))
    {
        for (auto const& hook : *hooks)
            out.Tier1Hooks.push_back(hook.second.get_value<std::string>());
    }

    out.Tier2Participates = tree.get<bool>("tier2.participates", false);
    out.RequestedMaxBots = tree.get<uint32>("tier2.requested_max_bots", 0);
    out.RequestedMaxNearbyUnits = tree.get<uint32>("tier2.requested_max_nearby_units", 0);
    out.RequestedMaxActions = tree.get<uint32>("tier2.requested_max_actions", 0);
    out.RequestedTickInterval = tree.get<uint32>("tier2.requested_tick_interval", 1);
    return true;
}
