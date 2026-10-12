// Test-only stand-in for the worldserver's logger. Prints fmt-style messages to stderr. Not part of
// the worldserver build.
#ifndef TRINITYCORE_TEST_STUB_LOG_H
#define TRINITYCORE_TEST_STUB_LOG_H

#include <cstdio>
#include <fmt/format.h>

#define TC_LOG_INFO(filter, ...)  std::fprintf(stderr, "[info  %s] %s\n", filter, fmt::format(__VA_ARGS__).c_str())
#define TC_LOG_WARN(filter, ...)  std::fprintf(stderr, "[warn  %s] %s\n", filter, fmt::format(__VA_ARGS__).c_str())
#define TC_LOG_ERROR(filter, ...) std::fprintf(stderr, "[error %s] %s\n", filter, fmt::format(__VA_ARGS__).c_str())

#endif // TRINITYCORE_TEST_STUB_LOG_H
