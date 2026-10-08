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

#include "BotDungeonPlan.h"
#include <algorithm>
#include <string>
#include <vector>

using namespace BotDungeon;

namespace
{
Candidate C(uint64 guid, uint8 cls, uint8 level = 20, float dist = 100.0f)
{
    Candidate c;
    c.Guid = guid;
    c.ClassId = cls;
    c.Level = level;
    c.DistanceToLeader = dist;
    return c;
}

Role RoleOf(ComposeResult const& r, uint64 guid)
{
    for (Member const& m : r.Members)
        if (m.Guid == guid)
            return m.AsRole;
    FAIL("guid not in group");
    return Role::Dps;
}

bool In(ComposeResult const& r, uint64 guid)
{
    return std::any_of(r.Members.begin(), r.Members.end(), [&](Member const& m) { return m.Guid == guid; });
}

Composition const five;
ComposeConfig const ccfg;
}

TEST_CASE("BotDungeon: class roles", "[BotDungeon]")
{
    CHECK(RolesOfClass(CLS_WARRIOR) & RoleBit(Role::Tank));
    CHECK_FALSE(RolesOfClass(CLS_WARRIOR) & RoleBit(Role::Healer));
    CHECK(RolesOfClass(CLS_PRIEST) & RoleBit(Role::Healer));
    CHECK_FALSE(RolesOfClass(CLS_PRIEST) & RoleBit(Role::Tank));
    CHECK(RolesOfClass(CLS_PALADIN) == (RoleBit(Role::Tank) | RoleBit(Role::Healer) | RoleBit(Role::Dps)));
    CHECK(RolesOfClass(CLS_MAGE) == RoleBit(Role::Dps));
    CHECK(RolesOfClass(0) == 0);
    CHECK(RolesOfClass(6) == 0);   // no death knight in this ruleset
    CHECK(PreferredRole(CLS_WARRIOR) == Role::Tank);
    CHECK(PreferredRole(CLS_PRIEST) == Role::Healer);
    CHECK(PreferredRole(CLS_PALADIN) == Role::Dps);
}

TEST_CASE("BotDungeon: composition", "[BotDungeon]")
{
    SECTION("a classic group fills every place")
    {
        Candidate lead = C(1, CLS_MAGE);
        lead.Leader = true;
        std::vector<Candidate> v = { lead, C(2, CLS_WARRIOR), C(3, CLS_PRIEST), C(4, CLS_ROGUE), C(5, CLS_HUNTER), C(6, CLS_WARLOCK) };
        ComposeResult r = Compose(v, five, 15, 30, ccfg);
        REQUIRE(r.Complete());
        CHECK(r.Members.size() == 5);
        CHECK(In(r, 1));
        CHECK(RoleOf(r, 2) == Role::Tank);
        CHECK(RoleOf(r, 3) == Role::Healer);
        CHECK(RoleOf(r, 1) == Role::Dps);
    }

    SECTION("a pure class is preferred for a scarce role over a hybrid")
    {
        Candidate lead = C(1, CLS_MAGE);
        lead.Leader = true;
        std::vector<Candidate> v = { lead, C(2, CLS_PALADIN, 20, 10), C(3, CLS_WARRIOR, 20, 900), C(4, CLS_PRIEST, 20, 900),
            C(5, CLS_DRUID, 20, 10), C(6, CLS_ROGUE), C(7, CLS_HUNTER) };
        ComposeResult r = Compose(v, five, 15, 30, ccfg);
        REQUIRE(r.Complete());
        CHECK(RoleOf(r, 3) == Role::Tank);     // warrior, although the paladin is nearer
        CHECK(RoleOf(r, 4) == Role::Healer);   // priest over the druid
    }

    SECTION("a hybrid fills the scarce role when no pure class is there")
    {
        Candidate lead = C(1, CLS_MAGE);
        lead.Leader = true;
        std::vector<Candidate> v = { lead, C(2, CLS_PALADIN), C(3, CLS_DRUID), C(4, CLS_ROGUE), C(5, CLS_HUNTER) };
        ComposeResult r = Compose(v, five, 15, 30, ccfg);
        REQUIRE(r.Complete());
        CHECK(RoleOf(r, 2) == Role::Tank);     // first hybrid by distance then guid
        CHECK(RoleOf(r, 3) == Role::Healer);
    }

    SECTION("the leader takes the scarcest role it can fill")
    {
        Candidate lead = C(1, CLS_PALADIN);
        lead.Leader = true;
        std::vector<Candidate> v = { lead, C(2, CLS_PRIEST), C(3, CLS_MAGE), C(4, CLS_ROGUE), C(5, CLS_HUNTER) };
        ComposeResult r = Compose(v, five, 15, 30, ccfg);
        REQUIRE(r.Complete());
        CHECK(RoleOf(r, 1) == Role::Tank);     // the paladin prefers damage but nobody else can tank
        CHECK(RoleOf(r, 2) == Role::Healer);
    }

    SECTION("missing roles are counted")
    {
        Candidate lead = C(1, CLS_MAGE);
        lead.Leader = true;
        std::vector<Candidate> v = { lead, C(2, CLS_ROGUE), C(3, CLS_HUNTER), C(4, CLS_WARLOCK) };
        ComposeResult r = Compose(v, five, 15, 30, ccfg);
        CHECK_FALSE(r.Complete());
        CHECK(r.MissingTanks == 1);
        CHECK(r.MissingHealers == 1);
        CHECK(r.MissingDps == 0);
        CHECK(r.Members.size() == 3);
    }

    SECTION("unavailable, busy, far and out-of-window candidates are not called")
    {
        Candidate lead = C(1, CLS_MAGE, 20);
        lead.Leader = true;
        Candidate off = C(2, CLS_WARRIOR);  off.Available = false;
        Candidate busy = C(3, CLS_PRIEST);  busy.Busy = true;
        Candidate far = C(4, CLS_ROGUE, 20, 5000.0f);
        Candidate low = C(5, CLS_HUNTER, 14);
        Candidate wide = C(6, CLS_WARLOCK, 25);
        std::vector<Candidate> v = { lead, off, busy, far, low, wide };
        ComposeResult r = Compose(v, five, 15, 30, ccfg);
        CHECK(r.Members.size() == 1);
        ComposeConfig busyOk = ccfg;
        busyOk.AllowBusy = true;
        CHECK(In(Compose(v, five, 15, 30, busyOk), 3));
    }

    SECTION("dungeon level window and an unfit leader")
    {
        Candidate lead = C(1, CLS_MAGE, 20);
        lead.Leader = true;
        std::vector<Candidate> v = { lead, C(2, CLS_WARRIOR, 22), C(3, CLS_PRIEST, 18) };
        CHECK(Compose(v, five, 21, 30, ccfg).Members.empty());   // leader below the dungeon
        CHECK(Compose(v, five, 10, 19, ccfg).Members.empty());   // leader above it
        ComposeResult r = Compose(v, five, 15, 30, ccfg);
        CHECK(r.Members.size() == 3);
    }

    SECTION("small groups and zero scarce roles")
    {
        Candidate lead = C(1, CLS_MAGE);
        lead.Leader = true;
        std::vector<Candidate> v = { lead, C(2, CLS_ROGUE), C(3, CLS_HUNTER) };
        Composition three;
        three.Size = 3; three.Tanks = 0; three.Healers = 0;
        ComposeResult r = Compose(v, three, 15, 30, ccfg);
        CHECK(r.Complete());
        CHECK(r.Members.size() == 3);
    }

    SECTION("nobody is placed twice and the result is stable")
    {
        std::vector<Candidate> v = { C(9, CLS_PALADIN), C(7, CLS_PALADIN), C(8, CLS_DRUID), C(3, CLS_PRIEST), C(5, CLS_WARRIOR), C(4, CLS_MAGE) };
        ComposeResult a = Compose(v, five, 15, 30, ccfg);
        std::reverse(v.begin(), v.end());
        ComposeResult b = Compose(v, five, 15, 30, ccfg);
        REQUIRE(a.Members.size() == b.Members.size());
        std::vector<uint64> ga, gb;
        for (Member const& m : a.Members) ga.push_back(m.Guid);
        for (Member const& m : b.Members) gb.push_back(m.Guid);
        std::sort(ga.begin(), ga.end());
        std::sort(gb.begin(), gb.end());
        CHECK(ga == gb);
        CHECK(std::adjacent_find(ga.begin(), ga.end()) == ga.end());
    }
}

TEST_CASE("BotDungeon: readiness", "[BotDungeon]")
{
    ReadyConfig const cfg;
    auto full = []()
    {
        std::vector<MemberState> v(5);
        v[0].AsRole = Role::Tank;
        v[1].AsRole = Role::Healer;
        for (size_t i = 0; i < v.size(); ++i)
            v[i].Guid = i + 1;
        return v;
    };

    std::vector<MemberState> v = full();
    CHECK(CheckReady(v, cfg) == Ready::Go);
    CHECK(CheckReady({}, cfg) == Ready::WaitForMembers);

    SECTION("combat beats everything") { v[2].InCombat = true; v[3].Alive = false; CHECK(CheckReady(v, cfg) == Ready::InCombat); }
    SECTION("dead or absent member") { v[3].Alive = false; CHECK(CheckReady(v, cfg) == Ready::WaitForMembers); v[3].Alive = true; v[4].Present = false; CHECK(CheckReady(v, cfg) == Ready::WaitForMembers); }
    SECTION("low health rests") { v[2].HealthPct = 60; CHECK(CheckReady(v, cfg) == Ready::Rest); }
    SECTION("healer needs more mana than the others")
    {
        v[1].ManaPct = 80;
        CHECK(CheckReady(v, cfg) == Ready::Rest);
        v[1].ManaPct = 85;
        v[2].ManaPct = 75;
        CHECK(CheckReady(v, cfg) == Ready::Go);
    }
    SECTION("repair comes before rest") { v[2].DurabilityPct = 10; v[3].HealthPct = 10; CHECK(CheckReady(v, cfg) == Ready::Repair); }
}

TEST_CASE("BotDungeon: pull choice", "[BotDungeon]")
{
    PullConfig const cfg;
    auto P = [](uint32 id, float dist, uint32 mobs = 2, int32 level = 20)
    {
        Pack p;
        p.Id = id; p.Distance = dist; p.Mobs = mobs; p.MaxMobLevel = level;
        return p;
    };
    Pack boss = P(9, 5.0f, 1, 21);
    boss.Boss = true;

    SECTION("nearest pack first")
    {
        std::vector<Pack> v = { P(1, 80), P(2, 30), P(3, 55) };
        PullChoice c = ChoosePull(v, 20, Ready::Go, 5, 5, cfg);
        CHECK(c.Kind == PullKind::Pull);
        CHECK(c.PackId == 2);
    }
    SECTION("not ready: rest")
    {
        std::vector<Pack> v = { P(1, 80) };
        CHECK(ChoosePull(v, 20, Ready::Rest, 5, 5, cfg).Kind == PullKind::Rest);
        CHECK(ChoosePull(v, 20, Ready::InCombat, 5, 5, cfg).Kind == PullKind::Rest);
    }
    SECTION("done packs are ignored, all done finishes")
    {
        std::vector<Pack> v = { P(1, 10), P(2, 20) };
        v[0].Done = true;
        CHECK(ChoosePull(v, 20, Ready::Go, 5, 5, cfg).PackId == 2);
        v[1].Done = true;
        CHECK(ChoosePull(v, 20, Ready::Go, 5, 5, cfg).Kind == PullKind::Finished);
        CHECK(ChoosePull({}, 20, Ready::Go, 5, 5, cfg).Kind == PullKind::Finished);
    }
    SECTION("too strong, too big and patrolling packs are skipped")
    {
        std::vector<Pack> v = { P(1, 10, 2, 25), P(2, 10, 6), P(3, 10) };
        v[2].Patrols = true;
        PullChoice c = ChoosePull(v, 20, Ready::Go, 5, 5, cfg);
        CHECK(c.Kind == PullKind::SkipAll);
        v[2].Patrols = false;
        CHECK(ChoosePull(v, 20, Ready::Go, 5, 5, cfg).PackId == 3);
    }
    SECTION("the boss waits for the trash and for a full group")
    {
        std::vector<Pack> v = { P(1, 50), boss };
        CHECK(ChoosePull(v, 20, Ready::Go, 5, 5, cfg).PackId == 1);
        v[0].Done = true;
        CHECK(ChoosePull(v, 20, Ready::Go, 5, 5, cfg).PackId == 9);
        CHECK(ChoosePull(v, 20, Ready::Go, 4, 5, cfg).Kind == PullKind::SkipAll);
    }
    SECTION("a boss may go when the trash left is too strong")
    {
        std::vector<Pack> v = { P(1, 50, 2, 30), boss };
        CHECK(ChoosePull(v, 20, Ready::Go, 5, 5, cfg).PackId == 9);
    }
    SECTION("a large pack is fine when it is the boss's")
    {
        Pack big = boss;
        big.Mobs = 7;
        std::vector<Pack> v = { big };
        CHECK(ChoosePull(v, 20, Ready::Go, 5, 5, cfg).PackId == 9);
    }
}

TEST_CASE("BotDungeon: run phases", "[BotDungeon]")
{
    RunConfig const cfg;
    RunFacts f;
    f.NowMs = 1000000;
    f.StartedMs = 900000;
    f.PhaseSinceMs = 900000;

    SECTION("gather waits for everybody, then travels")
    {
        f.Current = Phase::Gather;
        f.Present = 4;
        CHECK(AdvanceRun(f, cfg).Next == Phase::Gather);
        f.Present = 5;
        CHECK(AdvanceRun(f, cfg).Next == Phase::Travel);
        f.Present = 4;
        f.NowMs = f.PhaseSinceMs + cfg.GatherTimeoutMs + 1;
        f.StartedMs = f.NowMs - 1000;
        CHECK(AdvanceRun(f, cfg).Next == Phase::Aborted);
    }
    SECTION("travel enters, times out")
    {
        f.Current = Phase::Travel;
        CHECK(AdvanceRun(f, cfg).Next == Phase::Travel);
        f.Inside = true;
        CHECK(AdvanceRun(f, cfg).Next == Phase::Clear);
        f.Inside = false;
        f.NowMs = f.PhaseSinceMs + cfg.TravelTimeoutMs + 1;
        f.StartedMs = f.NowMs - 1000;
        CHECK(AdvanceRun(f, cfg).Next == Phase::Aborted);
    }
    SECTION("clear ends with the last boss, loot first when pending")
    {
        f.Current = Phase::Clear;
        f.Inside = true;
        CHECK(AdvanceRun(f, cfg).Next == Phase::Clear);
        f.BossKilled = true;
        f.LootPending = true;
        CHECK(AdvanceRun(f, cfg).Next == Phase::Loot);
        f.LootPending = false;
        CHECK(AdvanceRun(f, cfg).Next == Phase::Done);
        f.BossKilled = false;
        f.PacksLeft = false;
        CHECK(AdvanceRun(f, cfg).Next == Phase::Done);
    }
    SECTION("loot ends when the corpses are empty or after a minute")
    {
        f.Current = Phase::Loot;
        f.Inside = true;
        f.LootPending = true;
        f.PhaseSinceMs = f.NowMs - 1000;
        CHECK(AdvanceRun(f, cfg).Next == Phase::Loot);
        f.PhaseSinceMs = f.NowMs - 61000;
        CHECK(AdvanceRun(f, cfg).Next == Phase::Done);
    }
    SECTION("a wipe recovers, then regroups; too many wipes abort")
    {
        f.Current = Phase::Clear;
        f.Inside = true;
        f.Alive = 0;
        CHECK(AdvanceRun(f, cfg).Next == Phase::Recover);
        f.Wipes = cfg.MaxWipes;
        CHECK(AdvanceRun(f, cfg).Next == Phase::Aborted);
        f.Wipes = 1;
        f.Current = Phase::Recover;
        CHECK(AdvanceRun(f, cfg).Next == Phase::Recover);
        f.Alive = 5;
        f.Present = 5;
        CHECK(AdvanceRun(f, cfg).Next == Phase::Clear);
        f.Inside = false;
        CHECK(AdvanceRun(f, cfg).Next == Phase::Travel);
    }
    SECTION("whole-run timeout does not apply to a player-led run")
    {
        f.Current = Phase::Clear;
        f.Inside = true;
        f.StartedMs = f.NowMs - cfg.RunTimeoutMs - 1;
        CHECK(AdvanceRun(f, cfg).Next == Phase::Aborted);
        f.LeaderIsPlayer = true;
        CHECK(AdvanceRun(f, cfg).Next == Phase::Clear);
    }
    SECTION("finished runs stay finished")
    {
        f.Current = Phase::Done;
        CHECK(AdvanceRun(f, cfg).Next == Phase::Done);
        f.Current = Phase::Aborted;
        CHECK(AdvanceRun(f, cfg).Next == Phase::Aborted);
    }
    SECTION("names")
    {
        CHECK(std::string(PhaseName(Phase::Recover)) == "recover");
        CHECK(std::string(RoleName(Role::Healer)) == "healer");
    }
}
