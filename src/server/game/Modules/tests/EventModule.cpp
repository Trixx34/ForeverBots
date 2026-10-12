/*
 * Test module for Tier 1 event routing, built on forever-module-sdk's ModuleClient.
 *
 * It subscribes to a subset of hooks (PlayerScript.OnLogout is deliberately not one of them) and logs every
 * notification it receives to <endpoint without .sock>.log, so the host test can check what was delivered.
 * Commands:
 *   ping   -> success, "pong"
 *   slow   -> sleeps 1 s, then success "late" (the core's request times out first)
 *   stall  -> sleeps 3 s without reading its socket (the core's queue fills; notifications are dropped)
 *   other  -> success=false with no text ("not mine")
 *
 * Not part of the worldserver build. See ModuleHostTest.cpp for how it is built and run.
 */

#include "forever_module/module_client.h"

#include <chrono>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

using namespace std::chrono_literals;

int main(int argc, char** argv)
{
    if (argc < 2)
        return 2;

    std::string const endpoint = argv[1];
    std::string const logPath = endpoint.substr(0, endpoint.size() - std::string(".sock").size()) + ".log";
    std::mutex logMutex;
    auto log = [&](std::string const& line)
    {
        std::lock_guard<std::mutex> lock(logMutex);
        std::ofstream(logPath, std::ios::app) << line << "\n";
    };

    forever_module::ModuleManifest manifest;
    manifest.module_name = "event-module";
    manifest.module_version = "0.1.0";
    manifest.tier1_hooks = {"PlayerScript.OnLogin", "AccountScript.OnAccountLogin", "WorldScript.OnStartup", "CommandScript"};
    manifest.heartbeat_interval = 1000ms;

    forever_module::ModuleClient client(std::move(manifest));
    if (!client.Connect(endpoint) || !client.Handshake(5000ms))
        return 1;

    client.OnNotify("PlayerScript.OnLogin", [&](forever_module::EventNotify const& n)
    {
        log("notify PlayerScript.OnLogin " + std::to_string(n.player_login().player_guid()));
    });
    client.OnNotify("AccountScript.OnAccountLogin", [&](forever_module::EventNotify const& n)
    {
        log("notify AccountScript.OnAccountLogin " + std::to_string(n.account_login().account_id()));
    });
    client.OnNotify("WorldScript.OnStartup", [&](forever_module::EventNotify const&)
    {
        log("notify WorldScript.OnStartup 0");
    });

    client.OnRequest("CommandScript", [&](forever_module::EventRequest const& request)
    {
        std::string const& text = request.command().command_text();
        log("request " + text + " from " + std::to_string(request.command().issuer_guid()));

        forever_module::EventReply reply;
        auto* command = reply.mutable_command();
        if (text == "ping")
        {
            command->set_success(true);
            command->set_result_text("pong");
        }
        else if (text == "slow")
        {
            std::this_thread::sleep_for(1s);
            command->set_success(true);
            command->set_result_text("late");
        }
        else if (text == "stall")
        {
            std::this_thread::sleep_for(3s); // not reading the socket meanwhile
            command->set_success(false);
        }
        else
        {
            command->set_success(false);
        }
        return reply;
    });

    client.Run();
    return 0;
}
