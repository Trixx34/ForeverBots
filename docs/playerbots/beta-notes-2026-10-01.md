# WoW Forever beta: development notes and known issues (Oct 1, 2026)

Sources (owner-supplied, Blizzard forums): "WoW Forever Beta Development Notes, updated October 1" and "WoW Forever Beta Known Issues, October 1". The text below is a summary produced by a fetch tool, not a transcript: **verify figures against the threads before relying on them.** Paraphrased.

## Content scope (matters for bot scenarios)
- Maximum level is **30** in this beta. Bot level targets and scenarios should stay at or below 30 for now.
- Dungeons: about a dozen were already accessible; the notes add a new **Excavation Site: Wetlands** (levels 26-31) and open Razorfen Downs (25+) and Uldaman (30+). The sim's first dungeon scenarios (Ragefire Chasm, Deadmines) are in the already-open set; the new Wetlands dungeon is new content that VMaNGOS data will not have.
- Dungeon quest XP was halved; respawn rates were raised in several zones (Hillsborough, Ashenvale, Wetlands, Duskwood, Thousand Needles). Escort-quest and multi-drop quest-item bugs were fixed. Skinning is once per creature. These are retail-side changes: the vanilla-converted world data may differ.

## Class and race changes (affects later class AI, not Phase 1)
- Notes list many class changes: Druid (rage on crits, Tiger's Fury removed, new feral talents), Hunter, Mage (Hot Streak renamed Heating Up, Combustion 3 charges), Paladin, Priest, Rogue, Shaman, Warlock, and a large Warrior rework (Fury tree restructure, Berserker Rage at level 30, Enrage no longer required for Flurry, 75% / 100% rage on crits). Races: Gnome Eureka no longer boosts periodic effects; Undead Cannibalize blocked if immune to physical damage.
- Consequence: class AI must be written against this fork's actual spells and talents, not against vanilla 1.12 or the AzerothCore playerbots rotations.
- PvP: honor costs up about 50%, honor cap 25,000. Windshaper (Horde) Skyborne trainer added in Orgrimmar.

## Known issues relevant to a server
- Enemies can parry/block from behind when the player is close; Faerie Fire and Demoralizing Shout do not generate threat; Glancing Blow math wrong for casters and higher-level targets; Eureka does not affect every spell; Dual Wield Specialization hit on both hands (warrior); Retribution Aura uses the target's spell power. Spell chaining lacks precast effects.
- Character friends and guild charter signing are disabled in the beta. Legacy System accessible before 25 causes Lua errors. "World instance transfers during party composition changes" is listed as an issue (watch for it in dungeon scenarios).
- Not mentioned in the summary: the build number. The upstream fork's auth SQL sets gamebuild 70235; the client on the NAS is 70205 until updated. Re-check the build before merging upstream.

## Open
- Haste: nothing in these notes answers whether haste is a stat in this beta (see racials.md).
