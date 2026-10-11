# Reload of Bot.* settings without a restart

`.reload config` re-reads `worldserver.conf` and `botserver.conf` (the bot file is kept on reload) and now also refills the bot option structs. No restart is needed for those options.

How it works: the modules used `std::call_once` to read their `Bot.*` options into a static struct. They now call `BotCfg::Load(flag, fill)` (`BotConfigReload.h`), which fills once per config epoch and remembers `fill`. `.reload config` calls `BotCfg::RequestReload()`. The next `BotMgr::Update` (world thread, outside the map updates, so no map thread reads a struct while it is refilled) bumps the epoch and refills every struct that was filled before. A struct first used after the reload is filled from the new values.

Covered: the combat, behavior (natural movement, town idle), AI, quest (quest, loot, safety, ammo), pet, travel, dungeon, population, watchdog, consumables, dummy and loot roll option groups.

Not covered, still needs a restart: options read once into an index or at login or startup (the quest index, the pet taming index, the transport index, `BotMgr` settings, the bot log database) and options read in code that does not use `BotCfg::Load` (chat orders, bank, auction, party, player-led runs, social, trade links). Values a bot already holds in its own state (a running trip, a claimed spawn) change on its next decision.

Check: change a value in `botserver.conf`, run `.reload config`, and look for "Bot settings reloaded (N option groups)" in the server log.
