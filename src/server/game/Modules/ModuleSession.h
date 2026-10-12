/*
 * One module's Tier 1 connection, owned by the core-side module host (ModuleMgr).
 *
 * Each session binds its own Unix domain socket, accepts exactly one connection from the module
 * process, runs the handshake under a deadline, then delivers events and tracks heartbeats until the
 * connection ends. Lifecycle results are reported through a Listener, called on the session thread.
 *
 * Threading: all socket I/O happens on the session thread. Other threads (the world thread, via
 * ScriptMgr hooks and chat) only queue frames, which never blocks them on the socket. A module that
 * stops reading fills its queue, and further notifications are dropped; the world thread is not held up.
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
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
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
        // Called on the session thread for each ActionRequest the module sends. Must not block: it is
        // expected to queue the request for the world thread and return.
        using ActionHandler = std::function<void(ModuleWire::ActionRequestMsg const&)>;

        struct Settings
        {
            std::string Endpoint;                         // Unix domain socket path
            std::chrono::milliseconds HandshakeTimeout{5000};
            std::chrono::milliseconds HeartbeatTimeout{15000};
            std::string ExpectedProtocolVersion;          // exact match against HandshakeRequest.protocol_version
            std::vector<std::string> SupportedHooks;      // requested Tier 1 hooks must all be in this list
            std::vector<ModuleWire::ConfigEntry> Config;  // delivered in ModuleConfig right after accept
        };

        // Frames queued for a module that is not reading. Beyond this, new events are dropped.
        static constexpr size_t MaxQueuedBytes = 1024 * 1024;

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

        // True once the handshake has been accepted.
        bool IsAccepted() const { return _accepted.load(); }

        // True if the module asked for this hook in its handshake. Only meaningful once accepted.
        bool Subscribes(std::string_view hook) const;

        // Queues an EventNotify for a subscribed hook. Never blocks on the module. Returns false if the
        // event was not queued (not subscribed, not accepted, or the queue is full).
        bool NotifyEvent(std::string_view hook, ModuleWire::NotifyKind kind, uint64 value);

        // Sets the handler for module -> core actions. Call before Start().
        void SetActionHandler(ActionHandler handler);

        // Queues a PlayerItemLooted EventNotify for a subscribed hook. Same rules as NotifyEvent.
        bool NotifyItemLooted(std::string_view hook, ModuleWire::ItemLootedEvent const& event);

        // Queues an ActionReply for the module. Thread-safe: the world thread answers actions after the
        // session thread has handed them off. Returns false if the reply could not be queued.
        bool SendActionReply(ModuleWire::ActionReplyMsg const& reply);

        // Queues an EventRequest carrying a chat command and waits up to `timeout` for the reply.
        // Returns nothing on timeout, disconnect, or a full queue.
        std::optional<ModuleWire::EventReplyMsg> RequestCommand(std::string_view hook, std::string_view commandText,
                                                                uint64 issuerGuid, std::chrono::milliseconds timeout);

    private:
        using Clock = std::chrono::steady_clock;

        void Run();

        // Thread-safe. Queues an already-framed message and wakes the session thread.
        bool Enqueue(std::string frame);
        // Session thread only. Non-blocking: writes as much queued data as the socket accepts.
        bool FlushOutgoing();
        bool HasPendingOutgoing() const;
        // Session thread only. Queues and flushes, waiting for the socket until the deadline or Stop().
        bool SendBlocking(std::string frame, Clock::time_point deadline);
        void Wake();
        void DrainWake();
        void CompleteRequest(ModuleWire::EventReplyMsg reply);
        void FailPendingRequests();

        Settings _settings;
        uint64 _moduleId;
        Listener _listener;
        ActionHandler _actionHandler;

        int _listenFd = -1;
        int _connFd = -1;
        int _wakeRead = -1;
        int _wakeWrite = -1;
        std::thread _thread;
        std::atomic<bool> _stop{false};
        std::atomic<bool> _accepted{false};
        std::atomic<uint64> _heartbeats{0};

        mutable std::mutex _stateMutex;
        std::vector<std::string> _subscriptions;

        mutable std::mutex _outMutex;
        std::string _outBuffer;
        size_t _outOffset = 0;
        bool _outClosed = false;

        std::mutex _pendingMutex;
        std::unordered_map<uint64, std::promise<ModuleWire::EventReplyMsg>> _pending;
        std::atomic<uint64> _nextMessageId{1};
};

#endif // TRINITYCORE_MODULE_SESSION_H
