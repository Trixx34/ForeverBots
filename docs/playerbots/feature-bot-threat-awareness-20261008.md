# Threat awareness (gap: threat table)

The group roles of the bots (`Bot.AI.Roles.Enabled`) make the tank open and the others wait 3 s (`HoldFire`). After that nothing watched the
threat table, so a strong damage dealer could pull the mob off the tank. `Bot.AI.Roles.Threat` (default 1, needs `Roles.Enabled`) adds that.

- Every cast tick a non-tank bot in a group with a warrior reads its own threat and the tank's on the fight target
  (`ThreatManager::GetThreat`). The pull limit is 110 percent of the tank's threat in melee range and 130 percent at range; the bot acts at
  90 percent of that (`BotGroupRoles::CheckThreat`, pure and unit tested).
- With a ready, affordable reducer it casts it (Fade for priests, self; Feint for rogues, on the mob): `THREAT_REDUCE`. Without one it stops
  attacking for 2.5 s: `THREAT_PAUSE`. The engage action does not re-attack during the pause.
- At most one action per 8 s. Not while the bot is the tank, when no tank is in range and fighting the mob, or while the tank has built no
  threat yet (the opening is `HoldFire`'s job).

Not done: Soulshatter (not in the data), Feign Death and Vanish (they end the fight for the bot), mage and warlock reducers, and healers' threat
from heals (heal threat is split over every mob, so it rarely pulls).
