# Design: group and dungeon behavior toward the Deadmines milestone (2026-10-08, design only)

Builds on next-plan "Target: Deadmines with 5 boosted bots" and class-role-design.md. Nothing here is implemented.

## Stages (each needs a sim run before the next)
1. **Roles in a group.** Add Tank and Healer to the Role enum. Role from class plus talent counts (warrior/paladin/druid tank-capable, priest/paladin/shaman/druid healer), overridable by `bot role`. Group context value in the AI (empty when solo): members, leader, roles, health of each.
2. **Follow and assist.** Bots in a bot-led or player-led group follow the leader at a role-based offset (tank front, healer rear), assist the leader's/tank's target, loot rules per group setting. Reuse the existing follow strategy and `bot group form|move|list`.
3. **Tank and healer logic.** Tank: pick target by threat (taunt when an ally is hit, mark/focus next add). Healer: triage by missing health fraction, heal the tank first, drink at 40% mana. DPS: no pull before tank threat (delay N s), stop on crowd-control targets. Needs combat spell tables with Taunt, Heal kinds.
4. **Open-world group quest.** The 5 bots do a group quest together (lifts the NEEDS_GROUP block for them): shared quest pick by the leader, others take the same quest, wait for stragglers, shared kill credit.
5. **Dungeon strategy.** Enter Deadmines (instance entry via area trigger / portal), pull in packs (leader pulls with tank, others wait at a regroup point), CC and add handling, loot rules, rest between packs when mana/health below thresholds, run back after a wipe (corpse run in instance, re-enter, resume at the last cleared pack). Boss list for Deadmines with per-boss notes (Rhahk'Zor, Sneed, Gilnid, Mr. Smite, Cookie, Edwin VanCleef).

## Logging and codes
Decisions: `GROUP_FORMED`, `GROUP_ROLE`, `DUNGEON_ENTER`, `PACK_PULL`, `BOSS_START`, `BOSS_KILL`, `DUNGEON_WIPE`, `DUNGEON_DONE`. Blocked: `GROUP_WAIT` (straggler), `NO_HEALER`, `NO_TANK`, `INSTANCE_LOCKED`. Per run: outcome (clear/wipe/timeout), duration, deaths per bot, mana-out events.

## Preconditions and risks
Gearing and trained spells for boosted bots (factory step D), the wider combat spell table, instance map data/nav for Deadmines in the test world, the group-leader chat commands verified (A2). Highest risk: threat handling without a real threat table API for bots, and pathing inside the instance. Keep every piece behind `Bot.Group.*` / `Bot.Dungeon.*` config, default off.

## Done when
5 boosted bots clear Deadmines unattended in a sim run, with outcome, duration and deaths logged.
