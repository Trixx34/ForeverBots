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

#include "BotTownIdlePlan.h"

#include <algorithm>
#include <cmath>

using namespace BotTownIdle;

namespace
{
std::vector<Spot> Spots()
{
    return { { 0.0f, 0.0f, SpotKind::Inn }, { 40.0f, 0.0f, SpotKind::Bank }, { 0.0f, 40.0f, SpotKind::Vendor } };
}

Facts AtSpotFacts()
{
    Facts f;
    f.CurrentSpot = 0;
    f.AtSpot = true;
    f.StillMs = 5000;
    f.SinceActionMs = 5000;
    return f;
}
}

TEST_CASE("town idle: nothing starts with a threat around", "[bot][townidle]")
{
    Facts f = AtSpotFacts();
    f.Threat = true;
    f.StillMs = 600000;
    for (uint32 t = 0; t < 50; ++t)
        CHECK(PlanStep(f, Config(), Spots(), 7, t * 1000).What == Act::Stay);
}

TEST_CASE("town idle: a sitting bot stands up on a threat or after its hold time", "[bot][townidle]")
{
    Config cfg;
    Facts f;
    f.Sitting = true;
    f.SittingMs = 1000;
    CHECK(PlanStep(f, cfg, Spots(), 7, 1000).What == Act::Stay);
    f.Threat = true;
    CHECK(PlanStep(f, cfg, Spots(), 7, 1000).What == Act::StandUp);
    f.Threat = false;
    f.SittingMs = cfg.SitMaxMs;
    CHECK(PlanStep(f, cfg, Spots(), 7, 1000).What == Act::StandUp);
}

TEST_CASE("town idle: too soon after the last action nothing happens", "[bot][townidle]")
{
    Facts f = AtSpotFacts();
    f.SinceActionMs = 500;
    CHECK(PlanStep(f, Config(), Spots(), 7, 5000).What == Act::Stay);
    f = AtSpotFacts();
    f.StillMs = 500;
    CHECK(PlanStep(f, Config(), Spots(), 7, 5000).What == Act::Stay);
}

TEST_CASE("town idle: a bot away from every spot heads for one", "[bot][townidle]")
{
    Facts f;
    f.StillMs = 3000;
    f.SinceActionMs = 3000;
    Plan const p = PlanStep(f, Config(), Spots(), 7, 5000);
    CHECK(p.What == Act::GoSpot);
    CHECK(p.Spot >= 0);
    CHECK(p.Spot < 3);
    CHECK(p.StandOff >= Config().StandOffMin);
    CHECK(p.StandOff <= Config().StandOffMax);
}

TEST_CASE("town idle: with no spots the bot wanders instead", "[bot][townidle]")
{
    Facts f;
    f.StillMs = 600000;
    f.SinceActionMs = 600000;
    Plan const p = PlanStep(f, Config(), {}, 7, 5000);
    CHECK(p.What == Act::Wander);
    CHECK(p.StandOff >= 4.0f);
    CHECK(p.StandOff <= 14.0f);
}

TEST_CASE("town idle: lingering too long forces a move to a different spot", "[bot][townidle]")
{
    Config const cfg;
    for (uint64 key = 1; key <= 40; ++key)
    {
        Facts f = AtSpotFacts();
        f.StillMs = cfg.LingerMaxSec * 1000;
        Plan const p = PlanStep(f, cfg, Spots(), key, 90000);
        CHECK(p.What == Act::GoSpot);
        CHECK(p.Spot != 0);
    }
}

TEST_CASE("town idle: the linger time stays inside its range and differs per bot", "[bot][townidle]")
{
    Config const cfg;
    uint32 lo = ~0u, hi = 0;
    for (uint64 key = 1; key <= 200; ++key)
    {
        uint32 const ms = LingerMs(cfg, key);
        CHECK(ms >= cfg.LingerMinSec * 1000);
        CHECK(ms <= cfg.LingerMaxSec * 1000);
        lo = std::min(lo, ms);
        hi = std::max(hi, ms);
    }
    CHECK(hi - lo > 20000);
}

TEST_CASE("town idle: PickSpot never repeats the current spot and handles small lists", "[bot][townidle]")
{
    CHECK(PickSpot({}, -1, 1, 1000) == -1);
    CHECK(PickSpot({ { 0, 0, SpotKind::Inn } }, 0, 1, 1000) == 0);
    for (uint32 t = 0; t < 200; ++t)
    {
        int32 const s = PickSpot(Spots(), 1, 9, t * 1000);
        CHECK(s >= 0);
        CHECK(s < 3);
        CHECK(s != 1);
    }
}

TEST_CASE("town idle: PickSpot favours the inn over a vendor", "[bot][townidle]")
{
    std::vector<Spot> const spots = { { 0, 0, SpotKind::Vendor }, { 10, 0, SpotKind::Inn }, { 20, 0, SpotKind::Vendor }, { 30, 0, SpotKind::Vendor } };
    uint32 inn = 0, vendor = 0;
    for (uint32 t = 0; t < 400; ++t)
    {
        int32 const s = PickSpot(spots, 0, uint64(t) * 31 + 5, t * 1000);
        (spots[s].Kind == SpotKind::Inn ? inn : vendor)++;
    }
    // weights: inn 4, the two other vendors 1 each -> the inn gets about two thirds
    CHECK(inn > vendor);
}

TEST_CASE("town idle: lingering at a spot yields a mix of looks, emotes and sits", "[bot][townidle]")
{
    Config cfg;
    uint32 look = 0, emote = 0, sit = 0, other = 0;
    bool danced = false;
    for (uint32 t = 0; t < 2000; ++t)
    {
        Facts f = AtSpotFacts();
        f.StillMs = 4000 + (t % 7) * 1000;
        Plan const p = PlanStep(f, cfg, Spots(), 3, t * 1000 + 17);
        switch (p.What)
        {
            case Act::Look: ++look; CHECK(std::fabs(p.TurnRad) >= 0.5f); break;
            case Act::Emote: ++emote; danced = danced || p.Emote == EmoteKind::Dance; break;
            case Act::Sit: ++sit; break;
            case Act::Stay: break;
            default: ++other; break;
        }
    }
    CHECK(look > 0);
    CHECK(emote > 0);
    CHECK(sit > 0);
    CHECK(look > emote);
    CHECK(emote > sit);
    CHECK(danced);
}

TEST_CASE("town idle: hurt bots sit more often", "[bot][townidle]")
{
    Config cfg;
    uint32 well = 0, hurt = 0;
    for (uint32 t = 0; t < 2000; ++t)
    {
        Facts f = AtSpotFacts();
        f.StillMs = 4000 + (t % 7) * 1000;
        if (PlanStep(f, cfg, Spots(), 3, t * 1000 + 17).What == Act::Sit)
            ++well;
        f.HurtOrDrained = true;
        if (PlanStep(f, cfg, Spots(), 3, t * 1000 + 17).What == Act::Sit)
            ++hurt;
    }
    CHECK(hurt > well);
}

TEST_CASE("town idle: the plan is deterministic", "[bot][townidle]")
{
    Facts f = AtSpotFacts();
    Plan const a = PlanStep(f, Config(), Spots(), 11, 123456);
    Plan const b = PlanStep(f, Config(), Spots(), 11, 123456);
    CHECK(a.What == b.What);
    CHECK(a.Spot == b.Spot);
    CHECK(a.TurnRad == b.TurnRad);
}
