# Bots forming their own parties (2026-10-08)

Branch `claude/project-thread-nqkq6g`, started from `forever`. Until now a bot only grouped when a player invited it or when the dungeon run (`BotDungeonRun`) built a five-man group. With `Bot.AI.Party.Enabled = 1` free bots out in the world team up on their own. Default **off**, options documented in `worldserver.conf.dist`.

## Rules (`BotPartyPlan.h/.cpp`, pure, tag `[BotParty]`)

* **Who looks.** At every check (`FormEverySec`) each free bot looks for company with `FormChancePct`; the roll is a hash of (bot, check counter), so tests are reproducible and two bots never share a roll.
* **Who joins.** Same map, within `JoinRadius` yards, at least `MinSharedQuests` unfinished quests in common, and the level spread of the whole party within `MaxLevelSpread`. Joiners with the most shared quests, then the nearest, then the lower guid, fill the party up to `MaxSize`. Fewer than `MinSize` means no party. A bot is in at most one party.
* **Leader.** The highest level of the party (lower guid on a tie). It keeps its own quest work; the others follow it.
* **Why a shared quest.** A kill credits every group member standing near it, so followers progress the quest the leader is working on. A party ends when that stops being true.
* **Breaking up (`Evaluate`).** Party ends: `lifetime` (50-100 percent of `LifetimeMin`, fixed per party), `leader_gone` (dead, away or logged out for `LeaderGoneSec`), `too_small`. A member is dropped: `gone`, `dead`, `quest_done` (no unfinished quest in common any more), `lost` (beyond `LeashYards` for `LeashSec`, or on another map), `level` (more than `MaxLevelSpread + 2` away from the leader).

## Glue (`BotParty.h/.cpp`, world thread, called from `BotMgr::Update`)

* Candidates: online bots that are alive, out of combat, not in flight or teleporting, ungrouped, not on an instanced map, not active alts, not held by a dungeon run, a trip or a taming run, with at least one unfinished quest.
* A real `Group` is created for the leader (same calls as `BotDungeonRun`), the members join and `Motion().SetFollow(leader)`. Followers are marked busy (`BotParty::Busy`), so the quest AI stands back (`why = party`) and the watchdog does not call them stalled. The marker is removed when a member is dropped or the party ends.
* A dead member is dropped at once, which gives the corpse run back to the quest AI.
* Events (decision rows): `PARTY_FORMED`, `PARTY_MEMBER_DROPPED`, `PARTY_DISBANDED`, each with the reason.

## Limits

* Bots in a party refuse a player's invite (`INVITE_REFUSED_ALREADY_GROUPED`), and the travel and dungeon logic skip grouped bots, so they are free again only after the party ends (at most `LifetimeMin`).
* The leader does not wait for stragglers; a member that falls behind is dropped after `LeashSec`.
* Followers do not work their own quest targets while grouped.
* Nothing here has run on a sim server. Watch `PARTY_*` rows for parties that end at once (`too_small`, `quest_done`) or members dropped as `lost`, which would mean the follow movement is too slow or the leashes too tight.

## Tests

`bin/tests "[BotParty]"`: shared counts, forming (off, leader choice, filters, size cap, one party per bot, level spread of the whole party, chance, determinism), lifetime, break-up reasons.
