/*
 * Tier 1 session: see ModuleSession.h. Frames are a 4-byte little-endian length plus an Envelope
 * (ModuleWire.h). Socket I/O happens only on the session thread; other threads queue frames and wake it.
 */

#include "ModuleSession.h"

#include <algorithm>
#include <cerrno>
#include <cstring>

#if defined(_WIN32)

ModuleSession::ModuleSession(Settings settings, uint64 moduleId, Listener listener)
    : _settings(std::move(settings)), _moduleId(moduleId), _listener(std::move(listener)) { }
ModuleSession::~ModuleSession() = default;
bool ModuleSession::Listen(std::string& error)
{
    error = "module transport is not implemented on Windows yet";
    return false;
}
void ModuleSession::Start() { }
void ModuleSession::Stop() { }
void ModuleSession::Run() { }
bool ModuleSession::Subscribes(std::string_view) const { return false; }
bool ModuleSession::NotifyEvent(std::string_view, ModuleWire::NotifyKind, uint64) { return false; }
std::optional<ModuleWire::EventReplyMsg> ModuleSession::RequestCommand(std::string_view, std::string_view, uint64,
                                                                       std::chrono::milliseconds)
{
    return std::nullopt;
}
bool ModuleSession::Enqueue(std::string) { return false; }
bool ModuleSession::FlushOutgoing() { return false; }
bool ModuleSession::HasPendingOutgoing() const { return false; }
bool ModuleSession::SendBlocking(std::string, Clock::time_point) { return false; }
void ModuleSession::Wake() { }
void ModuleSession::DrainWake() { }
void ModuleSession::CompleteRequest(ModuleWire::EventReplyMsg) { }
void ModuleSession::FailPendingRequests() { }

#else

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

namespace
{

// Upper bound on each poll slice, so Stop() is noticed promptly.
constexpr std::chrono::milliseconds PollSlice{100};

// A peer that stops in the middle of a frame is treated as dead after this long.
constexpr std::chrono::milliseconds FrameReadTimeout{2000};

// How long a Shutdown gets to be written before the connection is closed anyway.
constexpr std::chrono::milliseconds ShutdownWriteTimeout{1000};

void SetCloexec(int fd)
{
    int flags = ::fcntl(fd, F_GETFD);
    if (flags != -1)
        ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

void SetNonBlocking(int fd)
{
    int flags = ::fcntl(fd, F_GETFL);
    if (flags != -1)
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// Waits until `fd` is ready for `events` or the timeout expires. POLLHUP/POLLERR count as ready, so the
// next read or write reports the failure.
bool WaitFor(int fd, short events, std::chrono::milliseconds timeout)
{
    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = events;
    int const ms = static_cast<int>(std::max<long long>(0, timeout.count()));
    int rc = 0;
    do
    {
        rc = ::poll(&pfd, 1, ms);
    } while (rc < 0 && errno == EINTR);
    return rc > 0;
}

bool ReadExact(int fd, void* buffer, size_t length)
{
    auto* out = static_cast<uint8*>(buffer);
    while (length > 0)
    {
        ssize_t const n = ::recv(fd, out, length, 0);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        out += n;
        length -= static_cast<size_t>(n);
    }
    return true;
}

bool ReadFrame(int fd, std::string& body)
{
    uint8 header[4];
    if (!ReadExact(fd, header, sizeof(header)))
        return false;
    uint32 const length = static_cast<uint32>(header[0]) |
                          (static_cast<uint32>(header[1]) << 8) |
                          (static_cast<uint32>(header[2]) << 16) |
                          (static_cast<uint32>(header[3]) << 24);
    if (length == 0 || length > ModuleWire::MaxFrameBytes)
        return false;
    body.assign(length, '\0');
    return ReadExact(fd, body.data(), body.size());
}

void SetReceiveTimeout(int fd, std::chrono::milliseconds timeout)
{
    timeval tv{};
    tv.tv_sec = static_cast<time_t>(timeout.count() / 1000);
    tv.tv_usec = static_cast<suseconds_t>((timeout.count() % 1000) * 1000);
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

} // namespace

ModuleSession::ModuleSession(Settings settings, uint64 moduleId, Listener listener)
    : _settings(std::move(settings)), _moduleId(moduleId), _listener(std::move(listener))
{
}

ModuleSession::~ModuleSession()
{
    Stop();
    if (_wakeRead >= 0)
        ::close(_wakeRead);
    if (_wakeWrite >= 0)
        ::close(_wakeWrite);
}

bool ModuleSession::Listen(std::string& error)
{
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (_settings.Endpoint.size() >= sizeof(addr.sun_path))
    {
        error = "socket path too long (max " + std::to_string(sizeof(addr.sun_path) - 1) + " bytes): " + _settings.Endpoint;
        return false;
    }
    std::strncpy(addr.sun_path, _settings.Endpoint.c_str(), sizeof(addr.sun_path) - 1);

    // Remove a stale socket file from a previous run; bind fails with EADDRINUSE otherwise.
    ::unlink(_settings.Endpoint.c_str());

    _listenFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (_listenFd < 0)
    {
        error = std::string("socket() failed: ") + std::strerror(errno);
        return false;
    }
    SetCloexec(_listenFd);

    if (::bind(_listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(_listenFd, 1) != 0)
    {
        error = std::string("bind/listen on ") + _settings.Endpoint + " failed: " + std::strerror(errno);
        ::close(_listenFd);
        _listenFd = -1;
        return false;
    }

    // Self-pipe: other threads write a byte here to wake the session thread from poll().
    int fds[2];
    if (::pipe(fds) != 0)
    {
        error = std::string("pipe() failed: ") + std::strerror(errno);
        ::close(_listenFd);
        _listenFd = -1;
        return false;
    }
    _wakeRead = fds[0];
    _wakeWrite = fds[1];
    for (int fd : fds)
    {
        SetCloexec(fd);
        SetNonBlocking(fd);
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
    if (_connFd >= 0)
    {
        ::close(_connFd);
        _connFd = -1;
    }
    if (_listenFd >= 0)
    {
        ::close(_listenFd);
        _listenFd = -1;
        ::unlink(_settings.Endpoint.c_str());
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
        ssize_t const n = ::send(_connFd, _outBuffer.data() + _outOffset, _outBuffer.size() - _outOffset,
                                 MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return true; // socket full; poll for POLLOUT and retry later
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
        WaitFor(_connFd, POLLOUT, std::min(PollSlice, std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now())));
    }
}

void ModuleSession::Wake()
{
    if (_wakeWrite >= 0)
    {
        char const byte = 1;
        [[maybe_unused]] ssize_t const ignored = ::write(_wakeWrite, &byte, 1); // full pipe means a wake is already pending
    }
}

void ModuleSession::DrainWake()
{
    char buffer[64];
    while (::read(_wakeRead, buffer, sizeof(buffer)) > 0)
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
        if (WaitFor(_listenFd, POLLIN, std::min(remaining, PollSlice)))
        {
            int const fd = ::accept(_listenFd, nullptr, nullptr);
            if (fd >= 0)
            {
                SetCloexec(fd);
                _connFd = fd;
                break;
            }
        }
    }
    if (_stop)
        goto done;

    SetReceiveTimeout(_connFd, FrameReadTimeout);

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
            if (WaitFor(_connFd, POLLIN, std::min(remaining, PollSlice)))
                break;
        }

        std::string body;
        if (!ReadFrame(_connFd, body))
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
        else if (request.Tier2Participates)
            reason = "Tier 2 is not available in this core build yet";
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

        if (!reason.empty())
        {
            ModuleWire::HandshakeResponse response;
            response.Accepted = false;
            response.Reason = reason;
            SendBlocking(ModuleWire::Frame(ModuleWire::EncodeEnvelope(ModuleWire::PayloadKind::HandshakeResponse, ModuleWire::Encode(response))),
                         handshakeDeadline);
            _listener(Event::Rejected, reason);
            goto done;
        }

        ModuleWire::HandshakeResponse response;
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

            pollfd fds[2]{};
            fds[0].fd = _connFd;
            fds[0].events = POLLIN | (HasPendingOutgoing() ? POLLOUT : 0);
            fds[1].fd = _wakeRead;
            fds[1].events = POLLIN;
            int const rc = ::poll(fds, 2, static_cast<int>(std::min(remaining, PollSlice).count()));
            if (rc < 0)
            {
                if (errno == EINTR)
                    continue;
                _listener(Event::Disconnected, "poll failed");
                goto done;
            }

            if (fds[1].revents & POLLIN)
                DrainWake();

            if (fds[0].revents & (POLLIN | POLLHUP | POLLERR))
            {
                std::string body;
                if (!ReadFrame(_connFd, body))
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
                    default:
                        // Other payloads (control) are handled in later slices.
                        break;
                }
            }
        }

        // Stop() requested: tell the module to exit cleanly. Best effort; the peer may be gone.
        if (_connFd >= 0)
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

    if (_connFd >= 0)
    {
        ::shutdown(_connFd, SHUT_RDWR);
        ::close(_connFd);
        _connFd = -1;
    }
    if (_listenFd >= 0)
    {
        ::close(_listenFd);
        _listenFd = -1;
    }
    ::unlink(_settings.Endpoint.c_str());
}

#endif // !_WIN32
