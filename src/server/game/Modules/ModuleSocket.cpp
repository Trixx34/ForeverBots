/*
 * Socket layer for the module transport. See ModuleSocket.h.
 */

#include "ModuleSocket.h"

#include <algorithm>
#include <cstring>
#include <string>

#if defined(_WIN32)

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

namespace
{

// AF_UNIX and sockaddr_un are not in every Windows SDK header set, so they are declared here.
// Winsock accepts AF_UNIX on Windows 10 1803 and later.
#ifndef AF_UNIX
#define AF_UNIX 1
#endif

struct SockAddrUn
{
    unsigned short sun_family;
    char sun_path[108];
};

short ToNative(short events)
{
    return static_cast<short>(((events & ModuleSocket::kReadable) ? POLLIN : 0) |
                              ((events & ModuleSocket::kWritable) ? POLLOUT : 0));
}

short FromNative(short revents)
{
    short out = 0;
    if (revents & POLLIN)
        out |= ModuleSocket::kReadable;
    if (revents & POLLOUT)
        out |= ModuleSocket::kWritable;
    if (revents & (POLLERR | POLLHUP | POLLNVAL))
        out |= ModuleSocket::kError;
    return out;
}

SOCKET Native(ModuleSocket::Handle h)
{
    return static_cast<SOCKET>(h);
}

} // namespace

namespace ModuleSocket
{

bool Startup(std::string& error)
{
    static bool started = false;
    static int result = 0;
    if (!started)
    {
        WSADATA data{};
        result = WSAStartup(MAKEWORD(2, 2), &data);
        started = true;
    }
    if (result != 0)
    {
        error = "WSAStartup failed with " + std::to_string(result);
        return false;
    }
    return true;
}

Handle CreateUnixSocket(std::string& error)
{
    SOCKET s = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET)
    {
        error = "socket(AF_UNIX) failed: " + LastError();
        return kInvalid;
    }
    return static_cast<Handle>(s);
}

bool BindAndListen(Handle listener, std::string const& path, std::string& error)
{
    SockAddrUn addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path))
    {
        error = "socket path too long (max " + std::to_string(sizeof(addr.sun_path) - 1) + " bytes): " + path;
        return false;
    }
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    RemovePath(path);
    if (::bind(Native(listener), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::listen(Native(listener), 1) != 0)
    {
        error = "bind/listen on " + path + " failed: " + LastError();
        return false;
    }
    return true;
}

Handle Accept(Handle listener)
{
    SOCKET s = ::accept(Native(listener), nullptr, nullptr);
    return s == INVALID_SOCKET ? kInvalid : static_cast<Handle>(s);
}

bool CreateWakePair(Handle& readEnd, Handle& writeEnd, std::string& error)
{
    // Winsock has no socketpair(); a loopback TCP pair does the same job.
    SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET)
    {
        error = "wake socket failed: " + LastError();
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    int addrLen = sizeof(addr);
    SOCKET writer = INVALID_SOCKET;
    SOCKET reader = INVALID_SOCKET;
    if (::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0 &&
        ::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &addrLen) == 0 &&
        ::listen(listener, 1) == 0)
    {
        writer = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (writer != INVALID_SOCKET && ::connect(writer, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0)
            reader = ::accept(listener, nullptr, nullptr);
    }
    ::closesocket(listener);

    if (reader == INVALID_SOCKET)
    {
        error = "wake socket pair failed: " + LastError();
        if (writer != INVALID_SOCKET)
            ::closesocket(writer);
        return false;
    }
    readEnd = static_cast<Handle>(reader);
    writeEnd = static_cast<Handle>(writer);
    SetNonBlocking(readEnd);
    SetNonBlocking(writeEnd);
    return true;
}

int Poll(Watch* watches, std::size_t count, std::chrono::milliseconds timeout)
{
    WSAPOLLFD fds[4]{};
    if (count > 4)
        return -1;
    for (std::size_t i = 0; i < count; ++i)
    {
        fds[i].fd = Native(watches[i].Fd);
        fds[i].events = ToNative(watches[i].Events);
    }
    int const ms = static_cast<int>(std::max<long long>(0, timeout.count()));
    int const rc = ::WSAPoll(fds, static_cast<ULONG>(count), ms);
    if (rc == SOCKET_ERROR)
        return -1;
    for (std::size_t i = 0; i < count; ++i)
        watches[i].Revents = FromNative(fds[i].revents);
    return rc;
}

std::ptrdiff_t Send(Handle h, void const* data, std::size_t length)
{
    return ::send(Native(h), static_cast<char const*>(data), static_cast<int>(length), 0);
}

std::ptrdiff_t Recv(Handle h, void* data, std::size_t length)
{
    return ::recv(Native(h), static_cast<char*>(data), static_cast<int>(length), 0);
}

bool WouldBlock()
{
    return ::WSAGetLastError() == WSAEWOULDBLOCK;
}

bool Interrupted()
{
    return false;
}

std::string LastError()
{
    return "Winsock error " + std::to_string(::WSAGetLastError());
}

void SetNonBlocking(Handle h)
{
    u_long mode = 1;
    ::ioctlsocket(Native(h), FIONBIO, &mode);
}

void Shutdown(Handle h)
{
    ::shutdown(Native(h), SD_BOTH);
}

void Close(Handle h)
{
    ::closesocket(Native(h));
}

void RemovePath(std::string const& path)
{
    ::DeleteFileA(path.c_str());
}

} // namespace ModuleSocket

#else // POSIX

#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace
{

short ToNative(short events)
{
    return static_cast<short>(((events & ModuleSocket::kReadable) ? POLLIN : 0) |
                              ((events & ModuleSocket::kWritable) ? POLLOUT : 0));
}

short FromNative(short revents)
{
    short out = 0;
    if (revents & POLLIN)
        out |= ModuleSocket::kReadable;
    if (revents & POLLOUT)
        out |= ModuleSocket::kWritable;
    if (revents & (POLLERR | POLLHUP | POLLNVAL))
        out |= ModuleSocket::kError;
    return out;
}

void SetCloexec(int fd)
{
    int flags = ::fcntl(fd, F_GETFD);
    if (flags != -1)
        ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

} // namespace

namespace ModuleSocket
{

bool Startup(std::string&)
{
    return true;
}

Handle CreateUnixSocket(std::string& error)
{
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        error = "socket() failed: " + LastError();
        return kInvalid;
    }
    SetCloexec(fd);
    return fd;
}

bool BindAndListen(Handle listener, std::string const& path, std::string& error)
{
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path))
    {
        error = "socket path too long (max " + std::to_string(sizeof(addr.sun_path) - 1) + " bytes): " + path;
        return false;
    }
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    // Remove a stale socket file from a previous run; bind fails with EADDRINUSE otherwise.
    RemovePath(path);
    if (::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(listener, 1) != 0)
    {
        error = "bind/listen on " + path + " failed: " + LastError();
        return false;
    }
    return true;
}

Handle Accept(Handle listener)
{
    int fd = ::accept(listener, nullptr, nullptr);
    if (fd >= 0)
        SetCloexec(fd);
    return fd;
}

bool CreateWakePair(Handle& readEnd, Handle& writeEnd, std::string& error)
{
    int fds[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0)
    {
        error = "socketpair() failed: " + LastError();
        return false;
    }
    for (int fd : fds)
    {
        SetCloexec(fd);
        SetNonBlocking(fd);
    }
    readEnd = fds[0];
    writeEnd = fds[1];
    return true;
}

int Poll(Watch* watches, std::size_t count, std::chrono::milliseconds timeout)
{
    pollfd fds[4]{};
    if (count > 4)
        return -1;
    for (std::size_t i = 0; i < count; ++i)
    {
        fds[i].fd = watches[i].Fd;
        fds[i].events = ToNative(watches[i].Events);
    }
    int const ms = static_cast<int>(std::max<long long>(0, timeout.count()));
    int rc = 0;
    do
    {
        rc = ::poll(fds, static_cast<nfds_t>(count), ms);
    } while (rc < 0 && errno == EINTR);
    if (rc < 0)
        return -1;
    for (std::size_t i = 0; i < count; ++i)
        watches[i].Revents = FromNative(fds[i].revents);
    return rc;
}

std::ptrdiff_t Send(Handle h, void const* data, std::size_t length)
{
    return ::send(h, data, length, MSG_NOSIGNAL);
}

std::ptrdiff_t Recv(Handle h, void* data, std::size_t length)
{
    return ::recv(h, data, length, 0);
}

bool WouldBlock()
{
    return errno == EAGAIN || errno == EWOULDBLOCK;
}

bool Interrupted()
{
    return errno == EINTR;
}

std::string LastError()
{
    return std::strerror(errno);
}

void SetNonBlocking(Handle h)
{
    int flags = ::fcntl(h, F_GETFL);
    if (flags != -1)
        ::fcntl(h, F_SETFL, flags | O_NONBLOCK);
}

void Shutdown(Handle h)
{
    ::shutdown(h, SHUT_RDWR);
}

void Close(Handle h)
{
    ::close(h);
}

void RemovePath(std::string const& path)
{
    ::unlink(path.c_str());
}

} // namespace ModuleSocket

#endif
