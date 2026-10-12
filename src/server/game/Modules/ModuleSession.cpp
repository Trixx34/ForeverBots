/*
 * Tier 1 session: see ModuleSession.h. Frames are a 4-byte little-endian length plus an Envelope
 * (ModuleWire.h). All socket reads and writes happen on the session thread.
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

#else

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace
{

using Clock = std::chrono::steady_clock;

// Upper bound on each poll slice, so Stop() is noticed promptly.
constexpr std::chrono::milliseconds PollSlice{100};

void SetCloexec(int fd)
{
    int flags = ::fcntl(fd, F_GETFD);
    if (flags != -1)
        ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

bool WaitReadable(int fd, std::chrono::milliseconds timeout)
{
    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;
    int const ms = static_cast<int>(timeout.count());
    int rc = 0;
    do
    {
        rc = ::poll(&pfd, 1, ms < 0 ? 0 : ms);
    } while (rc < 0 && errno == EINTR);
    // POLLHUP/POLLERR also count: the following read reports the EOF or error.
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

bool WriteAll(int fd, std::string const& data)
{
    size_t sent = 0;
    while (sent < data.size())
    {
        // MSG_NOSIGNAL: a closed peer must produce EPIPE, not kill the worldserver with SIGPIPE.
        ssize_t const n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        sent += static_cast<size_t>(n);
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

bool WriteEnvelopeFrame(int fd, ModuleWire::PayloadKind kind, std::string const& payload)
{
    return WriteAll(fd, ModuleWire::Frame(ModuleWire::EncodeEnvelope(kind, payload)));
}

} // namespace

ModuleSession::ModuleSession(Settings settings, uint64 moduleId, Listener listener)
    : _settings(std::move(settings)), _moduleId(moduleId), _listener(std::move(listener))
{
}

ModuleSession::~ModuleSession()
{
    Stop();
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
    return true;
}

void ModuleSession::Start()
{
    _thread = std::thread([this] { Run(); });
}

void ModuleSession::Stop()
{
    _stop = true;

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
        if (WaitReadable(_listenFd, std::min(remaining, PollSlice)))
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

    // Phase 2: handshake. The request must arrive within the same deadline.
    {
        std::string body;
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
            if (WaitReadable(_connFd, std::min(remaining, PollSlice)))
                break;
        }
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
            WriteEnvelopeFrame(_connFd, ModuleWire::PayloadKind::HandshakeResponse, ModuleWire::Encode(response));
            _listener(Event::Rejected, reason);
            goto done;
        }

        ModuleWire::HandshakeResponse response;
        response.Accepted = true;
        response.ModuleId = _moduleId;
        if (!WriteEnvelopeFrame(_connFd, ModuleWire::PayloadKind::HandshakeResponse, ModuleWire::Encode(response)) ||
            !WriteEnvelopeFrame(_connFd, ModuleWire::PayloadKind::ModuleConfig, ModuleWire::Encode(_settings.Config)))
        {
            _listener(Event::Disconnected, "failed to send handshake response");
            goto done;
        }
        _listener(Event::Accepted, request.ModuleName);
    }

    // Phase 3: heartbeats. Each heartbeat resets the liveness deadline.
    {
        auto lastHeartbeat = Clock::now();
        while (!_stop)
        {
            auto const remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                lastHeartbeat + _settings.HeartbeatTimeout - Clock::now());
            if (remaining.count() <= 0)
            {
                _listener(Event::HeartbeatTimeout, "no heartbeat within the heartbeat timeout");
                goto done;
            }
            if (!WaitReadable(_connFd, std::min(remaining, PollSlice)))
                continue;

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
            if (envelope.Kind == ModuleWire::PayloadKind::Heartbeat)
            {
                lastHeartbeat = Clock::now();
                ++_heartbeats;
            }
            // Other payloads (events, control) are handled in later slices.
        }

        // Stop() requested: tell the module to exit cleanly. Best effort; the peer may be gone.
        if (_connFd >= 0)
        {
            ModuleWire::Shutdown shutdown{"server stopping"};
            WriteEnvelopeFrame(_connFd, ModuleWire::PayloadKind::Shutdown, ModuleWire::Encode(shutdown));
        }
    }

done:
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
