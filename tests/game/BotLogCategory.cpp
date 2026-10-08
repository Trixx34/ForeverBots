/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */


#include "tc_catch2.h"

#include "BotLogCategory.h"

using namespace BotLogCat;

TEST_CASE("Reason prefixes pick the category", "[BotLogCategory]")
{
    CHECK(Classify("decision", "DUNGEON_NO_ENTRANCE") == Dungeon);
    CHECK(Classify("decision", "TRAVEL_LEG_START") == Travel);
    CHECK(Classify("decision", "PET_TAME_OK") == Pets);
    CHECK(Classify("decision", "AH_POSTED") == Economy);
    CHECK(Classify("decision", "BANK_DEPOSIT") == Economy);
    CHECK(Classify("decision", "MAIL_SENT") == Economy);
    CHECK(Classify("decision", "PARTY_FORMED") == Social);
    CHECK(Classify("decision", "TOWN_IDLE_GO") == Movement);
    CHECK(Classify("decision", "DUMMY_SUMMARY") == Dummy);
    CHECK(Classify("decision", "CORPSE_RUN_START") == Recovery);
    CHECK(Classify("quest_blocked", "NO_PATH") == Movement);
    CHECK(Classify("decision", "QUEST_WALK_START") == Quest);
}

TEST_CASE("Event type is the fallback, other when nothing matches", "[BotLogCategory]")
{
    CHECK(Classify("cast", "SOMETHING_NEW") == Combat);
    CHECK(Classify("quest_blocked", "ITEM_NOT_DROPPING") == Quest);
    CHECK(Classify("xp", "") == Progress);
    CHECK(Classify("alt_command", "REFUSED") == Social);
    CHECK(Classify("decision", "BRAND_NEW_TAG") == Other);
    CHECK(Classify("", "") == Other);
}

TEST_CASE("Every category has a name that parses back", "[BotLogCategory]")
{
    for (uint8 i = 0; i < Count; ++i)
    {
        Category c;
        REQUIRE(FindCategory(kNames[i], c));
        CHECK(c == Category(i));
    }
    Category c;
    CHECK(FindCategory("COMBAT", c));
    CHECK_FALSE(FindCategory("nope", c));
}

TEST_CASE("Categories list: default all, exclusions, inclusions", "[BotLogCategory]")
{
    std::vector<std::string> unknown;

    Config all;
    ParseCategories("all", all, unknown);
    CHECK(all.Allows(Combat, 1));

    Config minus;
    ParseCategories("-combat, -Movement", minus, unknown);
    CHECK_FALSE(minus.Allows(Combat, 3));
    CHECK_FALSE(minus.Allows(Movement, 1));
    CHECK(minus.Allows(Quest, 1));

    Config plus;
    ParseCategories("quest,dungeon", plus, unknown);
    CHECK(plus.Allows(Quest, 1));
    CHECK(plus.Allows(Dungeon, 1));
    CHECK_FALSE(plus.Allows(Combat, 3));

    Config mixed;
    ParseCategories("none,combat,all,-pets", mixed, unknown);
    CHECK(mixed.Allows(Combat, 1));
    CHECK_FALSE(mixed.Allows(Pets, 1));

    Config none;
    ParseCategories("none", none, unknown);
    CHECK_FALSE(none.Allows(Lifecycle, 3));

    CHECK(unknown.empty());
}

TEST_CASE("Empty list keeps everything on, unknown names are reported", "[BotLogCategory]")
{
    std::vector<std::string> unknown;
    Config cfg;
    ParseCategories("", cfg, unknown);
    CHECK(cfg.Allows(Other, 0));

    ParseCategories("-combat,-bogus,fights", cfg, unknown);
    REQUIRE(unknown.size() == 2);
    CHECK(unknown[0] == "-bogus");
    CHECK(unknown[1] == "fights");
    CHECK_FALSE(cfg.Allows(Combat, 1));
}

TEST_CASE("Severity floors apply per category", "[BotLogCategory]")
{
    std::vector<std::string> unknown;
    Config cfg;
    ParseSeverities("combat:2, movement:3", cfg, unknown);
    CHECK(unknown.empty());
    CHECK_FALSE(cfg.Allows(Combat, 1));
    CHECK(cfg.Allows(Combat, 2));
    CHECK_FALSE(cfg.Allows(Movement, 2));
    CHECK(cfg.Allows(Movement, 3));
    CHECK(cfg.Allows(Quest, 0));

    ParseSeverities("combat:9,nope:1,quest", cfg, unknown);
    CHECK(unknown.size() == 3);
    CHECK(cfg.MinSeverity[Combat] == 2);
}
