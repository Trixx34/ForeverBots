# Forever Bots Window

A client addon. `/botwin` opens one window to:

* **Add bots**: invite a bot by name (bots accept), kick it, or start the `lfg` search (I tank / I heal / I deal damage, status, stop) by naming any bot.
* **Choose who is addressed**: the whole group, tanks, healers, damage dealers, subgroup 1 or 2, or only your targeted bot (whispered).
* **Send orders**: follow, stay, stop, pull, heal, revive, mount, dismount, summon, release, rest, "what are you doing?", stance (aggressive / defensive / passive), role (tank / healer / dps), follow distance, focus.

Every button sends the same chat line you could type, so the server needs no change. Server switches that must be on (see `botserver.conf.dist`): `Bot.Chat.Orders.Enabled` for stop, pull, heal, mount, dismount, summon, revive; `Bot.Chat.Control.Enabled` for stance, role, distance, focus; `Bot.LFG.Enabled` for the search; `Bot.Invite.Enabled` for invites. Only the group leader's orders are obeyed.

Install: copy the folder into `Interface/AddOns/`. If the client rejects it as out of date, change the `## Interface:` number in the `.toc` to the client's.
