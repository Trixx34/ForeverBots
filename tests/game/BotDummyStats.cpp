#include "tc_catch2.h"

#include "BotDummyStats.h"

using namespace BotDummyStats;

TEST_CASE("BotDummyStats: run numbers", "[BotDummyStats]")
{
    Run r;

    SECTION("empty run")
    {
        r.Begin(1000);
        r.End(11000);
        CHECK(r.TotalHits() == 0);
        CHECK(r.Dps() == 0.0);
        CHECK(r.FirstHitMs() == -1);
        CHECK(r.LongestGapMs() == 10000);
    }

    SECTION("damage, dps, first hit and gaps")
    {
        r.Begin(1000);
        r.NoteHit(133, 50, 3000);    // first hit after 2 s
        r.NoteHit(133, 70, 4500);
        r.NoteHit(0, 10, 9500);      // 5 s gap
        r.End(11000);                // 1.5 s tail
        CHECK(r.TotalHits() == 3);
        CHECK(r.TotalDamage() == 130);
        CHECK(r.Dps() == Catch::Approx(13.0));
        CHECK(r.FirstHitMs() == 2000);
        CHECK(r.LongestGapMs() == 5000);
        REQUIRE(r.Rows().size() == 2);
        CHECK(r.Rows()[0].SpellId == 133);
        CHECK(r.Rows()[0].Hits == 2);
        CHECK(r.Rows()[0].Damage == 120);
        CHECK(r.Rows()[1].SpellId == 0);
    }

    SECTION("a late first hit is the longest gap")
    {
        r.Begin(0);
        r.NoteHit(1, 1, 8000);
        r.NoteHit(1, 1, 8500);
        r.End(9000);
        CHECK(r.LongestGapMs() == 8000);
    }

    SECTION("Begin resets a used run")
    {
        r.Begin(0);
        r.NoteHit(1, 100, 10);
        r.End(1000);
        r.Begin(5000);
        CHECK(r.TotalHits() == 0);
        CHECK(r.TotalDamage() == 0);
        CHECK(r.Rows().empty());
    }

    SECTION("percent")
    {
        CHECK(Percent(1, 4) == Catch::Approx(25.0));
        CHECK(Percent(1, 0) == 0.0);
    }
}
