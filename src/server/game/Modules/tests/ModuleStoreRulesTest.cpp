// Module store rules: key validation and the prefix pattern. Also checks the request decoder for the store actions,
// on hand-encoded bytes (actions.proto field numbers). Built by hand with the core include paths.

#include "ModuleStoreRules.h"
#include "ModuleWire.h"

#include <cstdio>
#include <string>

namespace
{

int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            ++g_failures;                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                 \
    } while (0)

// Field tag: field number and wire type. Lengths here are below 128, so one byte each.
std::string Tag(unsigned field, unsigned wire) { return std::string(1, char((field << 3) | wire)); }
std::string Bytes(unsigned field, std::string const& value) { return Tag(field, 2) + char(value.size()) + value; }

} // namespace

int main()
{
    std::string reason;
    CHECK(ModuleStore::ValidKey("bot/42", reason));
    CHECK(!ModuleStore::ValidKey("", reason));
    CHECK(!ModuleStore::ValidKey("has space", reason));
    CHECK(!ModuleStore::ValidKey("tab\t", reason));
    CHECK(!ModuleStore::ValidKey(std::string(ModuleStore::MaxKeyBytes + 1, 'k'), reason));
    CHECK(ModuleStore::ValidKey(std::string(ModuleStore::MaxKeyBytes, 'k'), reason));

    CHECK(ModuleStore::PrefixPattern("bot/") == "bot/%");
    CHECK(ModuleStore::PrefixPattern("") == "%");
    CHECK(ModuleStore::PrefixPattern("a_b%c\\") == "a\\_b\\%c\\\\%");

    // StoreSet (field 13): key=1, value=2. The message id is field 1.
    std::string setRequest = Tag(1, 0) + char(7) + Bytes(13, Bytes(1, "bot/42") + Bytes(2, "1"));
    ModuleWire::ActionRequestMsg set;
    CHECK(ModuleWire::Decode(setRequest, set));
    CHECK(set.MessageId == 7);
    CHECK(set.Kind == ModuleWire::ActionKind::StoreSet);
    CHECK(set.Key == "bot/42");
    CHECK(set.Value == "1");

    // StoreGet (12), StoreDelete (14) and StoreList (15, prefix in Key).
    ModuleWire::ActionRequestMsg get;
    CHECK(ModuleWire::Decode(Tag(1, 0) + char(8) + Bytes(12, Bytes(1, "bot/42")), get));
    CHECK(get.Kind == ModuleWire::ActionKind::StoreGet && get.Key == "bot/42");

    ModuleWire::ActionRequestMsg del;
    CHECK(ModuleWire::Decode(Tag(1, 0) + char(9) + Bytes(14, Bytes(1, "bot/42")), del));
    CHECK(del.Kind == ModuleWire::ActionKind::StoreDelete && del.Key == "bot/42");

    ModuleWire::ActionRequestMsg list;
    CHECK(ModuleWire::Decode(Tag(1, 0) + char(10) + Bytes(15, Bytes(1, "bot/")), list));
    CHECK(list.Kind == ModuleWire::ActionKind::StoreList && list.Key == "bot/");

    // A reply carries found, value and keys; an empty value is not written.
    ModuleWire::ActionReplyMsg reply;
    reply.MessageId = 3;
    reply.Success = true;
    reply.Found = true;
    reply.Keys = { "bot/1", "bot/2" };
    std::string encoded = ModuleWire::Encode(reply);
    CHECK(encoded.find("bot/1") != std::string::npos && encoded.find("bot/2") != std::string::npos);
    CHECK(encoded.find(Tag(5, 0) + char(1)) != std::string::npos);

    // With no keys and an empty value, field 6 (value) and field 7 (keys) are absent. Checked on a reply that has no
    // key text, since the tag byte 0x32 is also the character '2'.
    ModuleWire::ActionReplyMsg empty;
    empty.MessageId = 4;
    empty.Success = true;
    std::string emptyEncoded = ModuleWire::Encode(empty);
    CHECK(emptyEncoded.find(Tag(6, 2)) == std::string::npos);
    CHECK(emptyEncoded.find(Tag(7, 2)) == std::string::npos);

    if (g_failures == 0)
    {
        std::printf("PASS  module store rules and store request decoding\n");
        return 0;
    }
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
}
