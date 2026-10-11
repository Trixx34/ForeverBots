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

#ifndef TRINITY_BOT_CONFIG_RELOAD_H
#define TRINITY_BOT_CONFIG_RELOAD_H

// Reload of the Bot.* settings without a restart (".reload config").
//
// Most bot modules read their Bot.* options once into a static struct. BotCfg::Load replaces std::call_once for those structs: the first
// call fills the struct (and remembers how), and every reload runs the fill again, on the world thread inside BotMgr::Update, where no
// map update is running. A struct that is read only after the reload sees the new values; nothing is cached anywhere else.
// Not covered: options read once into an index (the quest index, the pet taming index, the transport index), options read at login or
// startup (BotMgr, bot log database), and settings of the world itself. Those need a restart.

#include "Define.h"
#include <functional>

namespace BotCfg
{
    struct Flag
    {
        uint32 Seen = 0xFFFFFFFFu;   // epoch of the fill that is in the struct
    };

    namespace Detail
    {
        TC_GAME_API void LoadImpl(Flag& flag, std::function<void()> fill);
    }

    // Runs `fill` once per config epoch for this flag (thread safe, like std::call_once, but again after a reload).
    template <typename F>
    void Load(Flag& flag, F&& fill)
    {
        Detail::LoadImpl(flag, std::function<void()>(std::forward<F>(fill)));
    }

    // Asks for a reload: the next BotMgr::Update refills every struct that has been filled before. Called by ".reload config" after the
    // config files were read again.
    TC_GAME_API void RequestReload();

    // World thread, start of BotMgr::Update: applies a pending request. Returns true when a reload ran.
    TC_GAME_API bool ApplyPending();
}

#endif
