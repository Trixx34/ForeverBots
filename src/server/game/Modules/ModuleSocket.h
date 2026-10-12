/*
 * Socket layer for the module transport. Unix domain stream sockets on POSIX; on Windows the same
 * AF_UNIX sockets through Winsock (Windows 10 1803 or later). Keeps ModuleSession free of platform calls.
 *
 * Every socket the session uses is non-blocking. Callers wait with Poll() and a deadline, so there is
 * no SO_RCVTIMEO and no MSG_DONTWAIT (Winsock has neither).
 */

#ifndef TRINITYCORE_MODULE_SOCKET_H
#define TRINITYCORE_MODULE_SOCKET_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ModuleSocket
{

#if defined(_WIN32)
using Handle = std::uintptr_t; // SOCKET
inline constexpr Handle kInvalid = ~static_cast<std::uintptr_t>(0); // INVALID_SOCKET
#else
using Handle = int;
inline constexpr Handle kInvalid = -1;
#endif

// Readiness flags used in Watch. Translated to the platform's poll values internally.
inline constexpr short kReadable = 0x1;
inline constexpr short kWritable = 0x2;
// Hang-up or error. Reported in Revents only; a read or write after it reports the failure.
inline constexpr short kError = 0x4;

struct Watch
{
    Handle Fd = kInvalid;
    short Events = 0;  // kReadable and/or kWritable
    short Revents = 0; // filled by Poll()
};

// Starts Winsock on Windows. Returns false with the reason if that fails. A no-op on POSIX.
bool Startup(std::string& error);

// Creates an AF_UNIX stream socket.
Handle CreateUnixSocket(std::string& error);

// Binds to `path` after removing a stale socket file, then listens.
bool BindAndListen(Handle listener, std::string const& path, std::string& error);

// Accepts one connection. Returns kInvalid on failure.
Handle Accept(Handle listener);

// A connected pair of sockets, used to wake a thread blocked in Poll(). Both ends are non-blocking.
bool CreateWakePair(Handle& readEnd, Handle& writeEnd, std::string& error);

// Waits until one of the watches is ready or the timeout expires. Returns the number of ready
// watches (0 on timeout), or -1 on error. Watches with kError set in Revents count as ready.
int Poll(Watch* watches, std::size_t count, std::chrono::milliseconds timeout);

// Non-blocking transfer. Returns bytes moved; 0 from Recv means the peer closed; -1 means error or
// would-block (check WouldBlock() and Interrupted()).
std::ptrdiff_t Send(Handle h, void const* data, std::size_t length);
std::ptrdiff_t Recv(Handle h, void* data, std::size_t length);

bool WouldBlock();   // last call would block; try again after Poll()
bool Interrupted();  // last call was interrupted by a signal; retry immediately
std::string LastError();

void SetNonBlocking(Handle h);
void Shutdown(Handle h); // both directions
void Close(Handle h);
void RemovePath(std::string const& path);

} // namespace ModuleSocket

#endif // TRINITYCORE_MODULE_SOCKET_H
