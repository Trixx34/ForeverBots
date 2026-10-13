#include "ModuleStore.h"
#include "DatabaseEnv.h"

namespace ModuleStore
{

namespace
{

// Escapes a string for use inside '...' in a query.
std::string Escaped(std::string value)
{
    WorldDatabase.EscapeString(value);
    return value;
}

} // namespace

std::string Get(std::string const& module, std::string const& key, bool& found, std::string& value)
{
    found = false;
    value.clear();

    std::string reason;
    if (!ValidKey(key, reason))
        return reason;

    QueryResult result = WorldDatabase.PQuery("SELECT `value` FROM `module_store` WHERE `module` = '{}' AND `key` = '{}'",
                                              Escaped(module), Escaped(key));
    if (result)
    {
        found = true;
        value = result->Fetch()[0].GetString();
    }
    return "";
}

std::string Set(std::string const& module, std::string const& key, std::string const& value)
{
    std::string reason;
    if (!ValidKey(key, reason))
        return reason;
    if (value.size() > MaxValueBytes)
        return "value must be at most " + std::to_string(MaxValueBytes) + " bytes";

    // A new key counts against the module's row limit; replacing an existing key does not.
    bool found = false;
    std::string current;
    if (std::string const error = Get(module, key, found, current); !error.empty())
        return error;

    if (!found)
    {
        QueryResult count = WorldDatabase.PQuery("SELECT COUNT(*) FROM `module_store` WHERE `module` = '{}'", Escaped(module));
        if (count && count->Fetch()[0].GetUInt64() >= MaxRowsPerModule)
            return "module already holds " + std::to_string(MaxRowsPerModule) + " rows";
    }

    WorldDatabase.DirectPExecute("REPLACE INTO `module_store` (`module`, `key`, `value`) VALUES ('{}', '{}', '{}')",
                                 Escaped(module), Escaped(key), Escaped(value));

    // The database layer logs failed writes but does not return them, so read the row back.
    bool storedFound = false;
    std::string stored;
    if (std::string const error = Get(module, key, storedFound, stored); !error.empty())
        return error;
    if (!storedFound || stored != value)
        return "the row could not be written";
    return "";
}

std::string Delete(std::string const& module, std::string const& key, bool& existed)
{
    existed = false;
    std::string value;
    if (std::string const error = Get(module, key, existed, value); !error.empty())
        return error;
    if (existed)
        WorldDatabase.DirectPExecute("DELETE FROM `module_store` WHERE `module` = '{}' AND `key` = '{}'", Escaped(module), Escaped(key));
    return "";
}

std::string List(std::string const& module, std::string const& prefix, std::vector<std::string>& keys)
{
    keys.clear();
    if (prefix.size() > MaxKeyBytes)
        return "prefix must be at most " + std::to_string(MaxKeyBytes) + " characters";

    QueryResult result = WorldDatabase.PQuery(
        "SELECT `key` FROM `module_store` WHERE `module` = '{}' AND `key` LIKE '{}' ORDER BY `key` LIMIT {}",
        Escaped(module), Escaped(PrefixPattern(prefix)), MaxListed);
    if (result)
    {
        do
        {
            keys.push_back(result->Fetch()[0].GetString());
        } while (result->NextRow());
    }
    return "";
}

} // namespace ModuleStore
