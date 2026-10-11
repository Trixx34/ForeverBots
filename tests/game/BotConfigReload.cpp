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
#include "BotConfigReload.h"

TEST_CASE("BotCfg: Load fills once and again after a reload", "[BotCfg]")
{
    static BotCfg::Flag flag;
    static int fills = 0;
    auto fill = []() { ++fills; };

    BotCfg::Load(flag, fill);
    BotCfg::Load(flag, fill);
    CHECK(fills == 1);

    CHECK_FALSE(BotCfg::ApplyPending());   // nothing asked for
    CHECK(fills == 1);

    BotCfg::RequestReload();
    CHECK(BotCfg::ApplyPending());
    CHECK(fills == 2);
    CHECK_FALSE(BotCfg::ApplyPending());   // one request, one reload

    BotCfg::Load(flag, fill);              // already refilled by the reload
    CHECK(fills == 2);

    static BotCfg::Flag late;              // first use after a reload fills once and joins the next reload
    static int lateFills = 0;
    auto lateFill = []() { ++lateFills; };
    BotCfg::Load(late, lateFill);
    BotCfg::Load(late, lateFill);
    CHECK(lateFills == 1);
    BotCfg::RequestReload();
    BotCfg::ApplyPending();
    CHECK(lateFills == 2);
    CHECK(fills == 3);
}
