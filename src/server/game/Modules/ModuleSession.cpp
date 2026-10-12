/*
 * Tier 1 session: see ModuleSession.h. Frames are a 4-byte little-endian length plus an Envelope
 * (ModuleWire.h). Socket I/O happens only on the session thread; other threads queue frames and wake it.
 * Platform socket calls are in ModuleSocket.h, so this file is the same on POSIX and Windows.
 */

#include "ModuleSession.h"

#include "ModuleSocket.h"

#include <algorithm>

namespace
{

// Upper bound on each poll slice, so Stop() is noticed promptly.
constexpr std::chrono::milliseconds PollSlice{100};

// A peer that stops in the middle of a frame is treated as dead after this long.
constexpr std::chrono::milliseconds FrameReadTimeout{2000};

// How long a Shutdown gets to be written before the connection is closed anyway.
constexpr std::chrono::milliseconds ShutdownWriteTimeout{1000};

using Clock = std::chrono::steady_clock;

bool WaitFor(ModuleSocket::Handle fd, short events, std::chrono::milliseconds timeout)
{
    ModuleSocket::Watch watch;
    watch.Fd = fd;
    watch.Events = events;
    return ModuleSocket::Poll(&watch, 1, timeout) > 0;
}

// Reads exactly `length` bytes, waiting for the socket until the deadline.
bool ReadExact(ModuleSocket::Handle fd, void* buffer, size_t length, Clock::time_point deadline)
{
    auto* out = static_cast<uint8*>(buffer);
    while (length > 0)
    {
        std::ptrdiff_t const n = ModuleSocket::Recv(fd, out, length);
        if (n > 0)
        {
            out += n;
            length -= static_cast<size_t>(n);
            continue;
        }
        if (n == 0)
            return false; // peer closed
        if (ModuleSocket::Interrupted())
            continue;
        if (!ModuleSocket::WouldBlock())
            return false;

        auto const remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        if (remaining.count() <= 0 || !WaitFor(fd, ModuleSocket::kReadable, remaining))
            return false;
    }
    return true;
}

bool ReadFrame(ModuleSocket::Handle fd, std::string& body, Clock::time_point deadline)
{
    uint8 header[4];
    if (!ReadExact(fd, header, sizeof(header), deadline))
        return false;
    uint32 const length = static_cast<uint32>(header[0]) |
                          (static_cast<uint32>(header[1]) << 8) |
                          (static_cast<uint32>(header[2]) << 16) |
                          (static_cast<uint32>(header[3]) << 24);
    if (length == 0 || length > ModuleWire::MaxFrameBytes)
        return false;
    body.assign(length, '\0');
    return ReadExact(fd, body.data(), body.size(), deadline);
}

} // namespace

ModuleSession::ModuleSession(Settings settings, uint64 moduleId, Listener listener)
    : _settings(std::move(settings)), _moduleId(moduleId), _listener(std::move(listener))
{
}

ModuleSession::~ModuleSession()
{
    Stop();
    if (_wakeRead != ModuleSocket::kInvalid)
        ModuleSocket::Close(_wakeRead);
    if (_wakeWrite != ModuleSocket::kInvalid)
        ModuleSocket::Close(_wakeWrite);
}

bool ModuleSession::Listen(std::string& error)
{
    if (!ModuleSocket::Startup(error))
        return false;

    _listenFd = ModuleSocket::CreateUnixSocket(error);
    if (_listenFd == ModuleSocket::kInvalid)
        return false;

    if (!ModuleSocket::BindAndListen(_listenFd, _settings.Endpoint, error))
    {
        ModuleSocket::Close(_listenFd);
        _listenFd = ModuleSocket::kInvalid;
        return false;
    }
    ModuleSocket::SetNonBlocking(_listenFd);

    if (!ModuleSocket::CreateWakePair(_wakeRead, _wakeWrite, error))
    {
        ModuleSocket::Close(_listenFd);
        _listenFd = ModuleSocket::kInvalid;
        return false;
    }
    return true;
}

void ModuleSession::Start()
{
    _thread = std::thread([this] { Run(); });
}

void ModuleSession::Stop()
{
    _stop = true;
    Wake();

    if (_thread.joinable() && _thread.get_id() != std::this_thread::get_id())
        _thread.join();

    // Run() closes its own sockets; this covers a session that was listened on but never started.
    if (_connFd != ModuleSocket::kInvalid)
    {
        ModuleSocket::Close(_connFd);
        _connFd = ModuleSocket::kInvalid;
    }
    if (_listenFd != ModuleSocket::kInvalid)
    {
        ModuleSocket::Close(_listenFd);
        _listenFd = ModuleSocket::kInvalid;
        ModuleSocket::RemovePath(_settings.Endpoint);
    }
}

bool ModuleSession::Subscribes(std::string_view hook) const
{
    if (!_accepted)
        return false;
    std::lock_guard<std::mutex> lock(_stateMutex);
    return std::find(_subscriptions.begin(), _subscriptions.end(), hook) != _subscriptions.end();
}

bool ModuleSession::NotifyEvent(std::string_view hook, ModuleWire::NotifyKind kind, uint64 value)
{
    if (!Subscribes(hook))
        return false;
    return Enqueue(ModuleWire::Frame(ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::EventNotify,
                                                                ModuleWire::EncodeEventNotify(hook, kind, value))));
}

bool ModuleSession::NotifyItemLooted(std::string_view hook, ModuleWire::ItemLootedEvent const& event)
{
    if (!Subscribes(hook))
        return false;
    return Enqueue(ModuleWire::Frame(ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::EventNotify,
                                                                ModuleWire::EncodeEventNotifyItemLooted(hook, event))));
}

bool ModuleSession::SendConfig(std::vector<ModuleWire::ConfigEntry> const& config)
{
    if (!IsAccepted())
        return false;
    return Enqueue(ModuleWire::Frame(ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::ModuleConfig, ModuleWire::Encode(config))));
}

bool ModuleSession::SendActionReply(ModuleWire::ActionReplyMsg const& reply)
{
    return Enqueue(ModuleWire::Frame(ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::ActionReply,
                                                                ModuleWire::Encode(reply), reply.MessageId)));
}

void ModuleSession::SetActionHandler(ActionHandler handler)
{
    _actionHandler = std::move(handler);
}

void ModuleSession::SetControlHandlers(ControlRequestHandler onRequest, ReleaseHandler onRelease)
{
    _controlRequestHandler = std::move(onRequest);
    _releaseHandler = std::move(onRelease);
}

bool ModuleSession::SendControlResponse(uint64 messageId, ModuleWire::ControlResponseMsg const& response)
{
    return Enqueue(ModuleWire::Frame(ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::ControlResponse,
                                                                ModuleWire::Encode(response), messageId)));
}

bool ModuleSession::SendControlRevoked(ModuleWire::ControlRevokedMsg const& revoked)
{
    return Enqueue(ModuleWire::Frame(ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::ControlRevoked,
                                                                ModuleWire::Encode(revoked))));
}

std::optional<ModuleWire::EventReplyMsg> ModuleSession::RequestCommand(std::string_view hook, std::string_view commandText,
                                                                       uint64 issuerGuid, std::chrono::milliseconds timeout)
{
    if (!Subscribes(hook))
        return std::nullopt;

    uint64 const id = _nextMessageId++;
    std::promise<ModuleWire::EventReplyMsg> promise;
    std::future<ModuleWire::EventReplyMsg> future = promise.get_future();
    {
        std::lock_guard<std::mutex> lock(_pendingMutex);
        _pending.emplace(id, std::move(promise));
    }

    auto const cancel = [this, id]
    {
        std::lock_guard<std::mutex> lock(_pendingMutex);
        _pending.erase(id);
    };

    if (!Enqueue(ModuleWire::Frame(ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::EventRequest,
                                                              ModuleWire::EncodeEventRequestCommand(id, hook, commandText, issuerGuid), id))))
    {
        cancel();
        return std::nullopt;
    }

    if (future.wait_for(timeout) != std::future_status::ready)
    {
        cancel();
        return std::nullopt;
    }
    try
    {
        return future.get();
    }
    catch (std::future_error const&)
    {
        return std::nullopt; // session ended before replying
    }
}

bool ModuleSession::Enqueue(std::string frame)
{
    {
        std::lock_guard<std::mutex> lock(_outMutex);
        if (_outClosed)
            return false;
        size_t const pending = _outBuffer.size() - _outOffset;
        if (pending + frame.size() > MaxQueuedBytes)
            return false;
        if (_outOffset == _outBuffer.size())
        {
            _outBuffer.clear();
            _outOffset = 0;
        }
        _outBuffer += frame;
    }
    Wake();
    return true;
}

bool ModuleSession::FlushOutgoing()
{
    // Holding the lock across a non-blocking send is fine: it returns immediately. Other threads only
    // append under this lock, so the buffer cannot move while we read from it.
    std::lock_guard<std::mutex> lock(_outMutex);
    while (_outOffset < _outBuffer.size())
    {
        std::ptrdiff_t const n = ModuleSocket::Send(_connFd, _outBuffer.data() + _outOffset, _outBuffer.size() - _outOffset);
        if (n < 0)
        {
            if (ModuleSocket::Interrupted())
                continue;
            if (ModuleSocket::WouldBlock())
                return true; // socket full; wait for writability and retry later
            return false;
        }
        if (n == 0)
            return false;
        _outOffset += static_cast<size_t>(n);
    }
    _outBuffer.clear();
    _outOffset = 0;
    return true;
}

bool ModuleSession::HasPendingOutgoing() const
{
    std::lock_guard<std::mutex> lock(_outMutex);
    return _outOffset < _outBuffer.size();
}

bool ModuleSession::SendBlocking(std::string frame, Clock::time_point deadline)
{
    {
        std::lock_guard<std::mutex> lock(_outMutex);
        _outBuffer += frame;
    }
    while (true)
    {
        if (!FlushOutgoing())
            return false;
        if (!HasPendingOutgoing())
            return true;
        if (_stop || Clock::now() >= deadline)
            return false;
        WaitFor(_connFd, ModuleSocket::kWritable,
                std::min(PollSlice, std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now())));
    }
}

void ModuleSession::Wake()
{
    if (_wakeWrite != ModuleSocket::kInvalid)
    {
        char const byte = 1;
        // A full buffer means a wake is already pending, so the result does not matter.
        [[maybe_unused]] std::ptrdiff_t const ignored = ModuleSocket::Send(_wakeWrite, &byte, 1);
    }
}

void ModuleSession::DrainWake()
{
    char buffer[64];
    while (ModuleSocket::Recv(_wakeRead, buffer, sizeof(buffer)) > 0)
    {
    }
}

void ModuleSession::CompleteRequest(ModuleWire::EventReplyMsg reply)
{
    std::lock_guard<std::mutex> lock(_pendingMutex);
    auto it = _pending.find(reply.MessageId);
    if (it == _pending.end())
        return; // the requester already timed out; a late reply is ignored
    it->second.set_value(std::move(reply));
    _pending.erase(it);
}

void ModuleSession::FailPendingRequests()
{
    std::lock_guard<std::mutex> lock(_pendingMutex);
    _pending.clear(); // destroying the promises makes each waiting future throw, which RequestCommand reports as no reply
}

void ModuleSession::Run()
{
    // Phase 1: wait for the module to connect, within the handshake timeout.
    auto const handshakeDeadline = Clock::now() + _settings.HandshakeTimeout;
    while (!_stop)
    {
        auto const remaining = std::chrono::duration_cast<std::chrono::milliseconds>(handshakeDeadline - Clock::now());
        if (remaining.count() <= 0)
        {
            _listener(Event::HandshakeTimeout, "module did not connect within the handshake timeout");
            goto done;
        }
        if (WaitFor(_listenFd, ModuleSocket::kReadable, std::min(remaining, PollSlice)))
        {
            ModuleSocket::Handle const fd = ModuleSocket::Accept(_listenFd);
            if (fd != ModuleSocket::kInvalid)
            {
                ModuleSocket::SetNonBlocking(fd);
                _connFd = fd;
                break;
            }
        }
    }
    if (_stop)
        goto done;

    // Phase 2: handshake. The request must arrive within the same deadline.
    {
        while (true)
        {
            auto const remaining = std::chrono::duration_cast<std::chrono::milliseconds>(handshakeDeadline - Clock::now());
            if (remaining.count() <= 0)
            {
                _listener(Event::HandshakeTimeout, "no HandshakeRequest within the handshake timeout");
                goto done;
            }
            if (_stop)
                goto done;
            if (WaitFor(_connFd, ModuleSocket::kReadable, std::min(remaining, PollSlice)))
                break;
        }

        std::string body;
        if (!ReadFrame(_connFd, body, Clock::now() + FrameReadTimeout))
        {
            _listener(Event::Disconnected, "module closed the connection before handshaking");
            goto done;
        }

        ModuleWire::Envelope envelope;
        ModuleWire::HandshakeRequest request;
        if (!ModuleWire::DecodeEnvelope(body, envelope) ||
            envelope.Kind != ModuleWire::PayloadKind::HandshakeRequest ||
            !ModuleWire::Decode(envelope.Payload, request))
        {
            _listener(Event::Rejected, "malformed HandshakeRequest");
            goto done;
        }

        std::string reason;
        if (request.ProtocolVersion != _settings.ExpectedProtocolVersion)
            reason = "protocol_version mismatch: core speaks \"" + _settings.ExpectedProtocolVersion +
                     "\", module declared \"" + request.ProtocolVersion + "\"";
        else if (request.Tier2Participates && !_settings.NegotiateTier2)
            reason = "Tier 2 is not available on this core";
        else
        {
            for (std::string const& hook : request.Tier1Hooks)
            {
                if (std::find(_settings.SupportedHooks.begin(), _settings.SupportedHooks.end(), hook) == _settings.SupportedHooks.end())
                {
                    reason = "unsupported Tier 1 hook: " + hook;
                    break;
                }
            }
        }

        ModuleWire::HandshakeResponse response;
        if (reason.empty() && request.Tier2Participates && !_settings.NegotiateTier2(request, response, reason))
            reason = reason.empty() ? "Tier 2 request refused" : reason;

        if (!reason.empty())
        {
            response.Accepted = false;
            response.Reason = reason;
            SendBlocking(ModuleWire::Frame(ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::HandshakeResponse, ModuleWire::Encode(response))),
                         handshakeDeadline);
            _listener(Event::Rejected, reason);
            goto done;
        }

        response.Accepted = true;
        response.ModuleId = _moduleId;
        if (!SendBlocking(ModuleWire::Frame(ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::HandshakeResponse, ModuleWire::Encode(response))),
                          handshakeDeadline) ||
            !SendBlocking(ModuleWire::Frame(ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::ModuleConfig, ModuleWire::Encode(_settings.Config))),
                          handshakeDeadline))
        {
            _listener(Event::Disconnected, "failed to send handshake response");
            goto done;
        }

        // Subscriptions must be visible before the session counts as accepted.
        {
            std::lock_guard<std::mutex> lock(_stateMutex);
            _subscriptions = request.Tier1Hooks;
        }
        _accepted = true;
        _listener(Event::Accepted, request.ModuleName);
    }

    // Phase 3: serve. Flush queued events, read heartbeats and replies, and enforce the heartbeat deadline.
    {
        auto lastHeartbeat = Clock::now();
        while (!_stop)
        {
            if (!FlushOutgoing())
            {
                _listener(Event::Disconnected, "connection closed while sending");
                goto done;
            }

            auto const remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                lastHeartbeat + _settings.HeartbeatTimeout - Clock::now());
            if (remaining.count() <= 0)
            {
                _listener(Event::HeartbeatTimeout, "no heartbeat within the heartbeat timeout");
                goto done;
            }

            ModuleSocket::Watch watches[2];
            watches[0].Fd = _connFd;
            watches[0].Events = ModuleSocket::kReadable | (HasPendingOutgoing() ? ModuleSocket::kWritable : 0);
            watches[1].Fd = _wakeRead;
            watches[1].Events = ModuleSocket::kReadable;
            int const rc = ModuleSocket::Poll(watches, 2, std::min(remaining, PollSlice));
            if (rc < 0)
            {
                _listener(Event::Disconnected, "poll failed: " + ModuleSocket::LastError());
                goto done;
            }

            if (watches[1].Revents & ModuleSocket::kReadable)
                DrainWake();

            if (watches[0].Revents & (ModuleSocket::kReadable | ModuleSocket::kError))
            {
                std::string body;
                if (!ReadFrame(_connFd, body, Clock::now() + FrameReadTimeout))
                {
                    _listener(Event::Disconnected, "connection closed");
                    goto done;
                }

                ModuleWire::Envelope envelope;
                if (!ModuleWire::DecodeEnvelope(body, envelope))
                {
                    _listener(Event::Disconnected, "malformed frame");
                    goto done;
                }

                switch (envelope.Kind)
                {
                    case ModuleWire::PayloadKind::Heartbeat:
                        lastHeartbeat = Clock::now();
                        ++_heartbeats;
                        break;
                    case ModuleWire::PayloadKind::EventReply:
                    {
                        ModuleWire::EventReplyMsg reply;
                        if (ModuleWire::Decode(envelope.Payload, reply))
                            CompleteRequest(std::move(reply));
                        break;
                    }
                    case ModuleWire::PayloadKind::ActionRequest:
                    {
                        ModuleWire::ActionRequestMsg request;
                        if (_actionHandler && ModuleWire::Decode(envelope.Payload, request))
                            _actionHandler(request);
                        break;
                    }
                    case ModuleWire::PayloadKind::ControlRequest:
                    {
                        ModuleWire::ControlRequestMsg request;
                        if (_controlRequestHandler && ModuleWire::Decode(envelope.Payload, request))
                            _controlRequestHandler(envelope.MessageId, request);
                        break;
                    }
                    case ModuleWire::PayloadKind::ReleaseControl:
                    {
                        ModuleWire::ReleaseControlMsg release;
                        if (_releaseHandler && ModuleWire::Decode(envelope.Payload, release))
                            _releaseHandler(release);
                        break;
                    }
                    default:
                        // Other payloads (control) are handled in later slices.
                        break;
                }
            }
        }

        // Stop() requested: tell the module to exit cleanly. Best effort; the peer may be gone.
        if (_connFd != ModuleSocket::kInvalid)
        {
            ModuleWire::Shutdown shutdown{"server stopping"};
            SendBlocking(ModuleWire::Frame(ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::Shutdown, ModuleWire::Encode(shutdown))),
                         Clock::now() + ShutdownWriteTimeout);
        }
    }

done:
    {
        // No new frames once the session ends. Callers' Enqueue() now fails instead of queueing forever.
        std::lock_guard<std::mutex> lock(_outMutex);
        _outClosed = true;
        _outBuffer.clear();
        _outOffset = 0;
    }
    _accepted = false;
    FailPendingRequests();

    if (_connFd != ModuleSocket::kInvalid)
    {
        ModuleSocket::Shutdown(_connFd);
        ModuleSocket::Close(_connFd);
        _connFd = ModuleSocket::kInvalid;
    }
    if (_listenFd != ModuleSocket::kInvalid)
    {
        ModuleSocket::Close(_listenFd);
        _listenFd = ModuleSocket::kInvalid;
    }
    ModuleSocket::RemovePath(_settings.Endpoint);
}
