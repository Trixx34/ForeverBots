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

#include "BotConfigReload.h"
#include "Log.h"
#include <atomic>
#include <mutex>
#include <vector>

namespace BotCfg
{
namespace
{
    struct Site
    {
        Flag* flag;
        std::function<void()> fill;
    };

    std::mutex s_mx;
    std::vector<Site> s_sites;
    std::atomic<uint32> s_epoch{ 0 };
    std::atomic<bool> s_pending{ false };
}

void Detail::LoadImpl(Flag& flag, std::function<void()> fill)
{
    if (flag.Seen == s_epoch.load(std::memory_order_acquire))
        return;
    std::lock_guard<std::mutex> lk(s_mx);
    uint32 const epoch = s_epoch.load(std::memory_order_relaxed);
    if (flag.Seen == epoch)
        return;
    bool known = false;
    for (Site const& s : s_sites)
        if (s.flag == &flag)
            known = true;
    fill();
    if (!known)
        s_sites.push_back({ &flag, std::move(fill) });
    flag.Seen = epoch;
}

void RequestReload()
{
    s_pending.store(true, std::memory_order_release);
}

bool ApplyPending()
{
    if (!s_pending.exchange(false, std::memory_order_acq_rel))
        return false;
    std::lock_guard<std::mutex> lk(s_mx);
    uint32 const epoch = s_epoch.fetch_add(1, std::memory_order_acq_rel) + 1;
    for (Site& s : s_sites)
    {
        s.fill();
        s.flag->Seen = epoch;
    }
    TC_LOG_INFO("server.loading", "Bot settings reloaded ({} option groups)", s_sites.size());
    return true;
}
}
