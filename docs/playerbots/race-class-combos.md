# Race/class combinations on Forever

Source: a table of available combinations (screenshot, 2026-10-06), transcribed here. `*` marks a combination that is **new for Forever** (not in 1.12 vanilla). Skyborne is a race of its own and exists on both factions, except where noted.

| Class | Allowed races |
|---|---|
| Druid | Night Elf, Tauren, Skyborne* |
| Hunter | Human*, Dwarf, Night Elf, Orc, Tauren, Troll, Skyborne* |
| Mage | Human, Gnome, Orc*, Undead, Troll, Skyborne* (**Alliance Skyborne only**) |
| Paladin | Human, Dwarf, Undead* |
| Priest | Human, Dwarf, Night Elf, Gnome*, Undead, Troll |
| Rogue | Human, Dwarf, Night Elf, Gnome, Orc, Undead, Troll, Skyborne* |
| Shaman | Dwarf*, Orc, Tauren, Troll, Skyborne* (**Horde Skyborne only**) |
| Warlock | Human, Gnome, Orc, Undead, Troll* |
| Warrior | Human, Dwarf, Night Elf, Gnome, Orc, Undead, Tauren, Troll, Skyborne* |

52 combinations in total (Skyborne counted once per class). Skyborne classes: Druid, Hunter, Mage (Alliance), Rogue, Shaman (Horde), Warrior.

Cross-checked against the Wowhead guide (updated 2026-09-25): the six new non-Skyborne combinations announced there (Gnome Priest, Human Hunter, Dwarf Shaman, Orc Mage, Troll Warlock, Undead Paladin) are exactly the starred cells above, and Skyborne Mage is Alliance only while Skyborne Shaman is Horde only. The guide says more combinations are planned, so read the allowed set from the game data and do not hard-code this table. Racials: see [racials.md](racials.md).

## What this means by faction

- **Alliance** races: Human, Dwarf, Night Elf, Gnome. **Horde** races: Orc, Undead, Tauren, Troll. Skyborne: either faction (Mage Alliance only, Shaman Horde only).
- Horde Paladin exists only as Undead; Alliance Shaman only as Dwarf; Alliance Druid only as Night Elf; Horde Priest only as Undead or Troll; Tauren has no Rogue, Mage, Priest, Warlock or Paladin.

## Use by the bots

- The bot factory must only create combinations from this table (invalid ones cannot create a character and could break the client).
- The authoritative source in the game is `playercreateinfo` (and the class/race masks in the DBC), so any difference between this table and the database is a data problem to report, not to paper over in bot code.
- For population targets ("10 bots per class and faction") use a balanced pick across the allowed races, so every allowed race/class gets exercised, including the new combinations.
