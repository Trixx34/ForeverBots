# Group buffs (gap: buffs on other players)

Buffing classes keep their buffs up out of combat, on themselves and on the members of their group in range. Off by default
(`Bot.AI.Buffs.Enabled`).

| Class | Buff | For |
|---|---|---|
| Priest | Power Word: Fortitude | everyone |
| Priest | Divine Spirit | mana users |
| Mage | Arcane Intellect | mana users |
| Druid | Mark of the Wild | everyone |
| Paladin | Blessing of Might | warrior, paladin, hunter, rogue |
| Paladin | Blessing of Wisdom | mana users (one blessing per target from a paladin) |

- Strategy `buffs` (NonCombat), action `buff_cast`. The bot casts its highest known rank. It does nothing in combat, mounted, resting or
  below `MinManaPct` (50), and a missing rank-1 id or a name that differs from the spell data drops the row at the first use (logged once as
  "group buffs validated, dropped:").
- A target counts as buffed when it carries any rank of the buff; blessings only count when they come from the casting paladin.
- The same buff on the same target is tried at most every `RetrySec` (30) whether the cast worked or not (`BotBuffPlan.cpp`, unit tested).
- Logged as `BUFF_CAST` / `BUFF_FAILED` with spell, target class and cast result. Real players in the group are buffed too.

Not done: Thorns (druid, tank only), Blessing of Kings and Sanctuary (talents), shaman weapon buffs, refreshing a lower rank, buffs on players
outside the group.
