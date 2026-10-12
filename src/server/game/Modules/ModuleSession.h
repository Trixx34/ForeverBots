/*
 * One module's Tier 1 connection, owned by the core-side module host (ModuleMgr).
 *
 * Each session binds its own Unix domain socket, accepts exactly one connection from the module
 * process, runs the handshake under a deadline, then tracks heartbeats until the connection ends.
 * Results are reported through a Listener, called on the session thread.
 *
 * No TrinityCore dependencies, so it can be unit-tested with the stubs in tests/.
 * POSIX only for now; on Windows Listen() fails with a clear error until the named-pipe transport
 * is written.
 */

#ifndef TRINITYCORE_MODULE_SESSION_H
#define TRINITYCORE_MODULE_SESSION_H

#include "Define.h"
#include "ModuleWire.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <thread>
#include <vector>

class ModuleSession
{
    public:
        enum class Event
        {
            Accepted,         // handshake succeeded; detail = module name
            Rejected,         // core refused the handshake; detail = reason sent to the module
            HandshakeTimeout, // no connection, or no handshake, within HandshakeTimeout
            HeartbeatTimeout, // no heartbeat within HeartbeatTimeout after acceptance
            Disconnected      // peer closed the connection or sent a malformed frame
        };

        using Listener = std::function<void(Event, std::string const& detail)>;

        struct Settings
        {
            std::string Endpoint;                         // Unix domain socket path
            std::chrono::milliseconds HandshakeTimeout{5000};
            std::chrono::milliseconds HeartbeatTimeout{15000};
            std::string ExpectedProtocolVersion;          // exact match against HandshakeRequest.protocol_version
            std::vector<std::string> SupportedHooks;      // requested Tier 1 hooks must all be in this list
            std::vector<ModuleWire::ConfigEntry> Config;  // delivered in ModuleConfig right after accept
        };

        ModuleSession(Settings settings, uint64 moduleId, Listener listener);
        ~ModuleSession();

        ModuleSession(ModuleSession const&) = delete;
        ModuleSession& operator=(ModuleSession const&) = delete;

        // Binds and listens on the endpoint. Call before spawning the module process, so the
        // module can connect as soon as it starts. Returns false with a reason in error.
        bool Listen(std::string& error);

        // Starts the session thread. Requires a successful Listen().
        void Start();

        // Sends Shutdown to a connected module, closes the sockets, and joins the session thread.
        // Idempotent. Must not be called from the Listener.
        void Stop();

        // Heartbeats received since the handshake was accepted.
        uint64 HeartbeatCount() const { return _heartbeats.load(); }

    private:
        void Run();

        Settings _settings;
        uint64 _moduleId;
        Listener _listener;

        int _listenFd = -1;
        int _connFd = -1;
        std::thread _thread;
        std::atomic<bool> _stop{false};
        std::atomic<uint64> _heartbeats{0};
};

#endif // TRINITYCORE_MODULE_SESSION_H
