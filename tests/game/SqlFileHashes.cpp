#include "tc_catch2.h"
#include "SqlFileHashes.h"

TEST_CASE("CRLF becomes LF, nothing else changes", "[SqlFileHashes]")
{
    CHECK(NormalizeSqlLineEndings("a\r\nb\r\n") == "a\nb\n");
    CHECK(NormalizeSqlLineEndings("a\nb\n") == "a\nb\n");
    CHECK(NormalizeSqlLineEndings("a\rb") == "a\rb");          // a lone CR stays
    CHECK(NormalizeSqlLineEndings("a\r\r\nb") == "a\r\nb");     // only the CR right before the LF goes
    CHECK(NormalizeSqlLineEndings("end\r") == "end\r");         // a trailing lone CR stays
    CHECK(NormalizeSqlLineEndings("") == "");
}

TEST_CASE("A stored hash matches when it is either of the two hashes", "[SqlFileHashes]")
{
    SqlFileHashes const hashes{ "lf-hash", "raw-hash" };
    CHECK(hashes.Matches("lf-hash"));       // stored by Windows, or by any build since 2026-10-14
    CHECK(hashes.Matches("raw-hash"));      // stored by Linux before that
    CHECK_FALSE(hashes.Matches("other"));
    CHECK_FALSE(hashes.Matches(""));
}
