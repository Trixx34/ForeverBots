#include "ModuleStoreRules.h"

namespace ModuleStore
{

bool ValidKey(std::string const& key, std::string& reason)
{
    if (key.empty() || key.size() > MaxKeyBytes)
    {
        reason = "key must be 1 to " + std::to_string(MaxKeyBytes) + " characters";
        return false;
    }
    for (unsigned char c : key)
    {
        if (c <= 0x20 || c >= 0x7F)
        {
            reason = "key must be printable ASCII without spaces";
            return false;
        }
    }
    return true;
}

std::string PrefixPattern(std::string const& prefix)
{
    std::string pattern;
    for (char c : prefix)
    {
        if (c == '%' || c == '_' || c == '\\')
            pattern += '\\';
        pattern += c;
    }
    return pattern + "%";
}

} // namespace ModuleStore
