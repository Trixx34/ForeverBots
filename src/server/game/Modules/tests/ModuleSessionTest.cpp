// ModuleSession over a real socket, with a module-side client written against the SDK's protobuf.
// POSIX only (the test client uses BSD sockets); the session itself is the same code on Windows.
// Build by hand (not part of the CMake build), from the repo root, with the SDK generated sources:
//   g++ -std=c++20 -I src/common -I src/server/shared -I dep/fmt/include -I src/server/game/Modules
//       -I src/server/game/Modules/tests/stubs -I <sdk-build>/generated/forever_module -I <sdk-build>/generated
//       src/server/game/Modules/tests/ModuleSessionTest.cpp src/server/game/Modules/ModuleSession.cpp
//       src/server/game/Modules/ModuleSocket.cpp src/server/game/Modules/ModuleWire.cpp <sdk-build>/generated/forever_module/*.pb.cc -lprotobuf -lpthread

#include "ModuleSession.h"

#include "actions.pb.h"
#include "envelope.pb.h"
#include "events.pb.h"
#include "handshake.pb.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

using namespace std::chrono_literals;

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

std::string SocketPath(char const* tag)
{
    return "/tmp/fm-session-" + std::to_string(::getpid()) + "-" + tag + ".sock";
}

// Module-side client socket with a receive timeout, so a broken session fails the test instead of hanging.
int Connect(std::string const& path)
{
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0)
        {
            timeval tv{2, 0};
            ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            return fd;
        }
        std::this_thread::sleep_for(10ms);
    }
    ::close(fd);
    return -1;
}

bool SendEnvelope(int fd, forever_module::Envelope const& envelope)
{
    std::string body;
    if (!envelope.SerializeToString(&body))
        return false;
    uint32_t const n = static_cast<uint32_t>(body.size());
    unsigned char header[4] = {static_cast<unsigned char>(n), static_cast<unsigned char>(n >> 8),
                               static_cast<unsigned char>(n >> 16), static_cast<unsigned char>(n >> 24)};
    std::string frame(reinterpret_cast<char*>(header), 4);
    frame += body;
    size_t sent = 0;
    while (sent < frame.size())
    {
        ssize_t const w = ::send(fd, frame.data() + sent, frame.size() - sent, MSG_NOSIGNAL);
        if (w <= 0)
            return false;
        sent += static_cast<size_t>(w);
    }
    return true;
}

bool RecvEnvelope(int fd, forever_module::Envelope& envelope)
{
    auto readExact = [fd](void* buffer, size_t length)
    {
        auto* out = static_cast<char*>(buffer);
        while (length > 0)
        {
            ssize_t const r = ::recv(fd, out, length, 0);
            if (r <= 0)
                return false;
            out += r;
            length -= static_cast<size_t>(r);
        }
        return true;
    };
    unsigned char header[4];
    if (!readExact(header, 4))
        return false;
    uint32_t const n = header[0] | (header[1] << 8) | (header[2] << 16) | (static_cast<uint32_t>(header[3]) << 24);
    std::string body(n, '\0');
    if (!readExact(body.data(), n))
        return false;
    return envelope.ParseFromString(body);
}

// Reads envelopes until one of the given kind arrives, skipping heartbeats-from-core and anything else.
bool RecvUntil(int fd, forever_module::Envelope::PayloadCase kind, forever_module::Envelope& out)
{
    while (RecvEnvelope(fd, out))
        if (out.payload_case() == kind)
            return true;
    return false;
}

forever_module::Envelope HandshakeRequest(std::string const& protocol, std::string const& hook)
{
    forever_module::Envelope env;
    env.set_message_id(1);
    auto* req = env.mutable_handshake_request();
    req->set_module_name("session-test");
    req->set_module_version("0.0.1");
    req->set_protocol_version(protocol);
    req->add_tier1_hooks(hook);
    req->set_sdk_package_version("0.1.0");
    return env;
}

ModuleSession::Settings TestSettings(std::string const& path)
{
    ModuleSession::Settings settings;
    settings.Endpoint = path;
    settings.HandshakeTimeout = 1000ms;
    settings.HeartbeatTimeout = 5000ms;
    settings.ExpectedProtocolVersion = "1";
    settings.SupportedHooks = {"PlayerScript.OnLootItem", "CommandScript"};
    settings.Config = {{"greeting", "hello"}};
    return settings;
}

// Accept, loot notify, sell action round trip, heartbeat, then Stop() must send Shutdown.
void AcceptAndActions()
{
    std::string const path = SocketPath("accept");
    std::mutex eventsMutex;
    std::vector<ModuleSession::Event> events;
    ModuleSession session(TestSettings(path), 7, [&](ModuleSession::Event e, std::string const&)
    {
        std::lock_guard<std::mutex> lock(eventsMutex);
        events.push_back(e);
    });

    std::mutex actionsMutex;
    std::vector<ModuleWire::ActionRequestMsg> actions;
    session.SetActionHandler([&](ModuleWire::ActionRequestMsg const& request)
    {
        std::lock_guard<std::mutex> lock(actionsMutex);
        actions.push_back(request);
    });

    std::string error;
    CHECK(session.Listen(error));
    session.Start();

    int fd = Connect(path);
    CHECK(fd >= 0);
    if (fd < 0)
    {
        session.Stop();
        return;
    }

    CHECK(SendEnvelope(fd, HandshakeRequest("1", "PlayerScript.OnLootItem")));
    forever_module::Envelope in;
    CHECK(RecvEnvelope(fd, in));
    CHECK(in.payload_case() == forever_module::Envelope::kHandshakeResponse);
    CHECK(in.handshake_response().accepted());
    CHECK(in.handshake_response().module_id() == 7);
    CHECK(RecvEnvelope(fd, in));
    CHECK(in.payload_case() == forever_module::Envelope::kModuleConfig);
    CHECK(in.module_config().values_size() == 1 && in.module_config().values(0).value() == "hello");

    for (int i = 0; i < 200 && !session.IsAccepted(); ++i)
        std::this_thread::sleep_for(5ms);
    CHECK(session.IsAccepted());
    CHECK(session.Subscribes("PlayerScript.OnLootItem"));
    CHECK(!session.Subscribes("CommandScript"));

    // Core -> module: a loot notify.
    ModuleWire::ItemLootedEvent loot;
    loot.PlayerGuid = 42;
    loot.ItemGuid = 5001;
    loot.ItemEntry = 2589;
    loot.Count = 3;
    loot.Quality = 0;
    loot.ItemClass = 7;
    loot.SellPrice = 20;
    loot.ItemName = "Tattered Cloth";
    CHECK(session.NotifyItemLooted("PlayerScript.OnLootItem", loot));
    CHECK(RecvUntil(fd, forever_module::Envelope::kEventNotify, in));
    CHECK(in.event_notify().hook_name() == "PlayerScript.OnLootItem");
    CHECK(in.event_notify().player_item_looted().item_guid() == 5001);
    CHECK(in.event_notify().player_item_looted().item_name() == "Tattered Cloth");

    // Module -> core: a sell action; core answers from another thread, as the world thread does.
    forever_module::Envelope request;
    request.set_message_id(31);
    request.mutable_action_request()->set_message_id(31);
    request.mutable_action_request()->mutable_sell_item()->set_player_guid(42);
    request.mutable_action_request()->mutable_sell_item()->set_item_guid(5001);
    request.mutable_action_request()->mutable_sell_item()->set_item_entry(2589);
    request.mutable_action_request()->mutable_sell_item()->set_count(3);
    CHECK(SendEnvelope(fd, request));

    for (int i = 0; i < 200; ++i)
    {
        {
            std::lock_guard<std::mutex> lock(actionsMutex);
            if (!actions.empty())
                break;
        }
        std::this_thread::sleep_for(5ms); // sleep outside the lock: the session thread needs it to deliver
    }
    {
        std::lock_guard<std::mutex> lock(actionsMutex);
        CHECK(actions.size() == 1);
        if (actions.size() == 1)
        {
            CHECK(actions[0].MessageId == 31);
            CHECK(actions[0].Kind == ModuleWire::ActionKind::SellItem);
            CHECK(actions[0].ItemGuid == 5001 && actions[0].Count == 3 && actions[0].ItemEntry == 2589);
        }
    }

    ModuleWire::ActionReplyMsg reply;
    reply.MessageId = 31;
    reply.Success = true;
    reply.CopperGained = 60;
    CHECK(session.SendActionReply(reply));
    CHECK(RecvUntil(fd, forever_module::Envelope::kActionReply, in));
    CHECK(in.message_id() == 31);
    CHECK(in.action_reply().success());
    CHECK(in.action_reply().copper_gained() == 60);

    // Heartbeat from the module is counted.
    forever_module::Envelope heartbeat;
    heartbeat.mutable_heartbeat()->set_sequence(1);
    CHECK(SendEnvelope(fd, heartbeat));
    for (int i = 0; i < 200 && session.HeartbeatCount() == 0; ++i)
        std::this_thread::sleep_for(5ms);
    CHECK(session.HeartbeatCount() == 1);

    // Stop() sends Shutdown to the module.
    session.Stop();
    CHECK(RecvUntil(fd, forever_module::Envelope::kShutdown, in));
    ::close(fd);

    std::lock_guard<std::mutex> lock(eventsMutex);
    CHECK(!events.empty() && events.front() == ModuleSession::Event::Accepted);
}

void RejectsProtocolMismatch()
{
    std::string const path = SocketPath("reject");
    std::atomic<int> rejected{0};
    ModuleSession session(TestSettings(path), 8, [&](ModuleSession::Event e, std::string const&)
    {
        if (e == ModuleSession::Event::Rejected)
            ++rejected;
    });
    std::string error;
    CHECK(session.Listen(error));
    session.Start();

    int fd = Connect(path);
    CHECK(fd >= 0);
    if (fd >= 0)
    {
        CHECK(SendEnvelope(fd, HandshakeRequest("2", "PlayerScript.OnLootItem")));
        forever_module::Envelope in;
        CHECK(RecvEnvelope(fd, in));
        CHECK(in.payload_case() == forever_module::Envelope::kHandshakeResponse);
        CHECK(!in.handshake_response().accepted());
        CHECK(in.handshake_response().reason().find("protocol_version") != std::string::npos);
        ::close(fd);
    }
    for (int i = 0; i < 200 && rejected == 0; ++i)
        std::this_thread::sleep_for(5ms);
    CHECK(rejected == 1);
    session.Stop();
}

void TimesOutWithoutConnection()
{
    std::string const path = SocketPath("timeout");
    std::atomic<int> timeouts{0};
    auto settings = TestSettings(path);
    settings.HandshakeTimeout = 150ms;
    ModuleSession session(settings, 9, [&](ModuleSession::Event e, std::string const&)
    {
        if (e == ModuleSession::Event::HandshakeTimeout)
            ++timeouts;
    });
    std::string error;
    CHECK(session.Listen(error));
    session.Start();
    for (int i = 0; i < 400 && timeouts == 0; ++i)
        std::this_thread::sleep_for(5ms);
    CHECK(timeouts == 1);
    session.Stop();
}

// Tier 2 requested: the negotiator's grant comes back in the handshake response; a refusal is a rejection.
void Tier2Handshake()
{
    std::string const path = SocketPath("tier2");
    std::atomic<int> rejected{0};
    auto settings = TestSettings(path);
    settings.NegotiateTier2 = [&](ModuleWire::HandshakeRequest const& request, ModuleWire::HandshakeResponse& response,
                                  std::string& reason)
    {
        if (request.RequestedMaxBots > 64)
        {
            reason = "too many bots requested";
            return false;
        }
        response.Tier2RegionName = "trinity_module_test_region";
        response.NegotiatedMaxBots = std::min<uint32_t>(request.RequestedMaxBots, 16);
        response.NegotiatedMaxNearbyUnits = 4;
        response.NegotiatedMaxActions = 32;
        response.NegotiatedTickInterval = request.RequestedTickInterval;
        return true;
    };
    ModuleSession session(settings, 11, [&](ModuleSession::Event e, std::string const&)
    {
        if (e == ModuleSession::Event::Rejected)
            ++rejected;
    });
    std::string error;
    CHECK(session.Listen(error));
    session.Start();

    int fd = Connect(path);
    CHECK(fd >= 0);
    if (fd >= 0)
    {
        forever_module::Envelope env = HandshakeRequest("1", "PlayerScript.OnLootItem");
        env.mutable_handshake_request()->set_tier2_participates(true);
        env.mutable_handshake_request()->set_requested_max_bots(40);
        env.mutable_handshake_request()->set_requested_tick_interval(3);
        CHECK(SendEnvelope(fd, env));
        forever_module::Envelope in;
        CHECK(RecvEnvelope(fd, in));
        CHECK(in.payload_case() == forever_module::Envelope::kHandshakeResponse);
        CHECK(in.handshake_response().accepted());
        CHECK(in.handshake_response().tier2_region_name() == "trinity_module_test_region");
        CHECK(in.handshake_response().negotiated_max_bots() == 16);
        CHECK(in.handshake_response().negotiated_max_nearby_units() == 4);
        CHECK(in.handshake_response().negotiated_max_actions() == 32);
        CHECK(in.handshake_response().negotiated_tick_interval() == 3);
        ::close(fd);
    }
    session.Stop();

    // Refused negotiation: a rejection that names the reason.
    std::string const refusedPath = SocketPath("tier2refused");
    auto refusedSettings = TestSettings(refusedPath);
    refusedSettings.NegotiateTier2 = [](ModuleWire::HandshakeRequest const&, ModuleWire::HandshakeResponse&, std::string& reason)
    {
        reason = "too many bots requested";
        return false;
    };
    ModuleSession refused(refusedSettings, 12, [&](ModuleSession::Event e, std::string const&)
    {
        if (e == ModuleSession::Event::Rejected)
            ++rejected;
    });
    CHECK(refused.Listen(error));
    refused.Start();
    int fd2 = Connect(refusedPath);
    CHECK(fd2 >= 0);
    if (fd2 >= 0)
    {
        forever_module::Envelope env = HandshakeRequest("1", "PlayerScript.OnLootItem");
        env.mutable_handshake_request()->set_tier2_participates(true);
        CHECK(SendEnvelope(fd2, env));
        forever_module::Envelope in;
        CHECK(RecvEnvelope(fd2, in));
        CHECK(!in.handshake_response().accepted());
        CHECK(in.handshake_response().reason() == "too many bots requested");
        ::close(fd2);
    }
    refused.Stop();
    for (int i = 0; i < 200 && rejected < 1; ++i)
        std::this_thread::sleep_for(5ms);
    CHECK(rejected == 1);
}

} // namespace

int main()
{
    AcceptAndActions();
    RejectsProtocolMismatch();
    TimesOutWithoutConnection();
    Tier2Handshake();

    if (g_failures == 0)
    {
        std::printf("PASS  ModuleSession over a socket\n");
        return 0;
    }
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
}
