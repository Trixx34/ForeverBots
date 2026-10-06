# Racials on Forever (reference for bot behavior)

Source: the owner pasted the Wowhead guide "All Racials and Available Class-Race Combinations in World of Warcraft: Forever" (by Serenal, updated 2026-09-25, written before the game's release), plus reader comments. **Treat this as a secondary source:** the guide itself says it may be incomplete, several racials were still being corrected, and comment claims are unverified. The authoritative data is this fork's spell/racial data (DBC/hotfix tables and `playercreateinfo*`); check there before coding anything. Paraphrased, not copied.

## Combinations
- The six new combinations announced so far are Gnome Priest, Human Hunter, Dwarf Shaman, Orc Mage, Troll Warlock and Undead Paladin, "with more planned". They match the starred cells in [race-class-combos.md](race-class-combos.md). More combinations may be added later, so the bot factory must read the allowed set from the game data, not hard-code the table.
- Skyborne is a new race with two faction variants: Mage only on the Alliance side, Shaman only on the Horde side. Dwarf Mage is not an option.

## Skyborne (both variants share Walk on Air, Wind Blessed, Elemental Insight)
| Racial | Effect |
|---|---|
| Walk on Air | glide downward through the air for 10 s |
| Wind Blessed (passive) | +1% haste (melee, ranged, spell) |
| Elemental Insight (passive) | +5% damage to Elementals |
| Skysight (Windshaper, Horde only) | active, +10% run speed |
| Read Ley Line (High Order, Alliance only) | active, +100% health and mana regeneration |

Readers noted the Horde variant looks weaker; that is a balance question, not a bot issue.

## Horde
- **Undead:** Cannibalize (regenerates health, and per a reader comment now mana too, on humanoid/undead corpses within 5 yd; any action or damage cancels), Touch of the Grave (5% proc, drains health up to 5% of max), Underwater Breathing (+300%), Will of the Forsaken (removes charm, fear, sleep).
- **Tauren:** Cultivation (grows herbs, no Herbalism needed), Endurance (+5% health, +1% hit), Plainsrunning (speed builds the longer you keep moving), War Stomp (2 s stun around you).
- **Orc:** Axe Specialization (axes add crit to spells/abilities), Blood Fury (+10% attack power and spell power for 15 s), Shatter Curse (curse/bane immunity and less magic damage for 8 s), Hardiness (-20% stun duration). A reader asks whether Command is gone.
- **Troll:** Beast Slaying (+5% vs beasts), Berserking (+10% cast and attack speed for 10 s), Regeneration (10% health regen continues in combat), Rapid Regeneration (50% of max health over time).

## Alliance
- **Dwarf:** Find Treasure, Big Game Hunter (+5% vs beasts), Mace Specialization (+1% crit with spells and attacks while a mace is equipped), Stoneform (removes and grants immunity to bleeds, poisons, diseases; -10% physical damage for 8 s).
- **Gnome:** Eureka! (next 3 spells/abilities cheaper and +10% damage or healing), Expansive Mind (more maximum resource), Engineering Specialization, Escape Artist (brief root/snare immunity).
- **Human:** Will to Survive (removes stuns; full tooltip not yet known), Perception (detect stealth for 20 s), Sword Specialization (+2% crit with swords), The Human Spirit (+5% spirit).
- **Night Elf:** Elune's Light (+10% crit for 15 s), Wisp Spirit (+75% speed as a wisp after death), Quickness (+1% dodge, +2% run speed), Shadowmeld (stealth while stationary; the exact behavior was still unconfirmed).

## Open questions that affect the bot code
- Whether **haste** exists as a stat in this fork's combat code (a reader asked whether 1% haste does anything). Check the stat code before using haste in any bot decision.
- Priest-specific racials per race (reader comments mention a Gnome Priest area stun and a resist-death ability): unverified, needs the real spell data.
- Active racials with a defensive or utility role (Stoneform, Will of the Forsaken, Escape Artist, War Stomp, Berserking, Blood Fury) are candidates for the class-ai racial usage rules in phases 4 to 6. Not needed for Phase 1.
