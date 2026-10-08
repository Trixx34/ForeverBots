# Bot training dummy mode (2026-10-08)

Stacked on PR #16 (`feature/bot-class-rotations`). Switch: `Bot.AI.Dummy.Enabled` (default **off**). Lets one bot fight an immortal, harmless dummy for a fixed time so a class rotation can be measured without a sim run.

## Use

1. Run `sql/custom/world/2026_10_08_04_world_forever_bot_dummy.sql` on the world DB (creates creature 9900001 as a copy of the Theramore Combat Dummy 4952, faction 14), set `Bot.AI.Dummy.Enabled = 1`, restart.
2. In party chat: `bot dummy` (60 s), `bot dummy 90`, `bot dummy off`. The usual selectors apply (`mage dummy 60` style, same as the other verbs).
3. Read the log: `DUMMY_START` and `DUMMY_SUMMARY` (decision events). Summary details: `end` (done, stopped, bot_died, dummy_gone), `duration_ms`, `damage`, `hits`, `dps`, `first_hit_ms`, `longest_gap_ms`, `reengaged`, `rotation` (is `Bot.AI.Rotation.Enabled` on), and `spells`: per spell id and name `hits`, `damage`, `pct` (spell id 0 is melee swings), highest damage first.

Compare rotation on and off with the same class and level, and check the existing `CAST_FAILED` rows of the same fight for the reasons a spell did not fire.

## How it works

* `BotDummy.cpp`: the chat verb summons the dummy 6 yards in front of the bot at the bot's level and puts both in combat. From there the normal combat strategy fights it (target choice, approach, casts), so the numbers are those of real fights.
* Strategy `dummy` (NonCombat and Combat engines) ticks once a second: refills health, mana and energy (`Bot.AI.Dummy.FullPower`), engages again if the dummy script ended the combat (it does after 5 s without damage), and ends the run on time, on death, or when the dummy is gone. The dummy is despawned at the end and at logout.
* Damage comes from the dummy script: `npc_training_dummy::DamageTaken` (`scripts/World/npcs_special.cpp`) calls `BotDummy::NoteDamage` before it zeroes the damage. Pet damage counts for the owner. Anyone else hitting a dummy is ignored.
* `BotDummyStats.h/.cpp` is the pure arithmetic (unit tested: `tests/game/BotDummyStats.cpp`, tag `[BotDummyStats]`).

## Limits

* Dps is over the whole run, idle time included; `longest_gap_ms` shows the idle part. Healers are not measured (no damage rows for heals).
* Rage comes from the hits only (not refilled), like a real fight.
* Not run on a server. `BotDummy.cpp`, `BotChat.cpp`, `BotAI.cpp`, `BotStrategies.cpp`, `npcs_special.cpp` and the test compile; the SQL is untested against the live schema (it copies the rows of entry 4952 into 9900001 table by table).
* The conf.dist description of `Bot.AI.Rotation.Enabled` from PR #16 has no value line under the keys list; not touched here.

## Also in this branch: warrior Battle Stance and per-spell buff timers

* New first warrior row `Battle Stance` (2457, self buff): Charge, Rend and Overpower need it, and the bots use no other stance, so a warrior in another stance switches back. Defensive and Berserker are not used yet.
* The self-buff recast timer was one value for the whole fight (a buff just cast blocked every other buff for 25 s). It is now per spell id, so a stance, Battle Shout and a seal no longer starve each other. The 3 s / 25 s recast times are unchanged.
* Not run on a server. Check `DUMMY_SUMMARY` of a warrior and the "class spells validated, dropped:" line for `Battle Stance`.
