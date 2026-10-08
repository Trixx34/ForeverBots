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

## Also in this branch: druid Bear Form (`Bot.AI.Rotation.DruidForms`, default off)

* Needs `Bot.AI.Rotation.Enabled`. New spell kind `Shift` (Bear Form 5487, first druid row) and a `Form` field on every table row: any form (default, all old rows), not shifted (Thorns, Moonfire, Wrath, Healing Touch, Entangling Roots, Faerie Fire) or Bear Form (Maul 6807, Swipe 779 with 2+ enemies, Demoralizing Roar 99 with 2+ enemies). A row whose form does not match the bot's current form is skipped, so a bear never fails casts it cannot make.
* A druid in Bear or Cat Form uses the melee chase (no standing at range). A bear that needs a heal leaves the form for Healing Touch (`HealAction`), and Bear Form is cast again after its 25 s recast time.
* Cat Form (768, level 20) is the first druid row and wins over Bear Form once known; a shifted bot never shifts again (so no flip between forms). Cat rows: Rip (3 combo points, target above 50%), Ferocious Bite (4), Rake (target above 40%), Claw. Finishers skip while their aura runs on the target (Rip) as well as on the bot (Slice and Dice). Stealth, Shred, Pounce and Tiger's Fury are not used. Dire Bear Form (level 40) is not in the table.
* With the switch off nothing changes: Bear Form is skipped and the unshifted rows run as before.
* Compiles; not run on a server. Spell ids are validated by name at startup like the other rows.

## Also in this branch: shaman totems, pre-pull buffs, rogue stealth openers

* **Totems** (new kind `Totem`, treated as a self buff without an aura check, recast no earlier than 50 s): Stoneskin Totem (first 10 s of a fight), Searing Totem (target above 60%), Healing Stream Totem (health under 70%). Classic totems need a totem tool in the bags; a bot without it fails the cast, which is logged once per fight as `CAST_FAILED`. Giving the bots the tools is not done.
* **Pre-pull buffs** (strategy `prebuff`, NonCombat, added to every bot while `Bot.AI.Rotation.Enabled` is on; action `combat_prebuff`): out of combat, standing, not shifted, with mana above `Bot.AI.Combat.PrePullManaPct`, a bot keeps up its own long buffs: Power Word: Fortitude, Inner Fire, Arcane Intellect, Frost Armor, Mark of the Wild, Blessing of Might, Demon Skin, Aspect of the Hawk, Lightning Shield. One cast per 30 s per buff, highest known rank; the root id must match the spell name or the entry is ignored. Buffs on other players are not done.
* **Rogue**: Cheap Shot and Ambush as the first rogue rows, only when the rogue carries Stealth (aura 1784). Nothing makes a rogue stealth yet (stealthed travel is slow and would change the quest walking), so these rows only fire when the rogue is stealthed by other means. Ambush needs a dagger and a position behind the target and will often fail with `NOT_BEHIND`.
* Compiles (BotCombat.cpp, BotAI.cpp); not run on a server.
