// Test-only definitions for the assertion and debug-info functions that the core's Errors.cpp
// provides. Only what CryptoHash.h needs is defined here. Not part of the worldserver build.
#include "Errors.h"

#include <cstdio>
#include <cstdlib>

std::string GetDebugInfo()
{
    return {};
}

namespace Trinity
{
[[noreturn]] void Assert(char const* file, int line, char const* function, char const* message, std::string) noexcept
{
    std::fprintf(stderr, "ASSERT %s:%d %s: %s\n", file, line, function, message);
    std::abort();
}
}
