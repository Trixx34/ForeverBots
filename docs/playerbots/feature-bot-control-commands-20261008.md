# Bot control panel: role, stance, focus, follow distance, status (2026-10-08)

Requested by Trixx34. Extends the chat commands (`BotChat.cpp`, header of `BotChat.h`). Switch: `Bot.Chat.Control.Enabled` (default **off**; with it off these words are ordinary chat). Syntax and authorization are the existing ones: `[selector] verb [args]` from the group leader in party chat, raid chat or a whisper to one bot. The pure parts are in `BotControl.h/.cpp` (tests: `tests/game/BotControl.cpp`).

| Command | Effect | Refusal codes |
|---|---|---|
| `role tank\|healer\|dps\|auto` | Overrides the role the class gives. `tank`: the bot is the group tank (the others assist its target and the healers watch it first); it wins over an Auto warrior (lowest guid among several). `healer` / `dps`: a warrior with that override is no longer a tank candidate. `dps` also keeps a hybrid from healing the group (it still heals itself). `auto`: back to the class. Needs `Bot.AI.Roles.Enabled` to have a visible effect in combat. | `BAD_ARGS` |
| `stance aggressive\|defensive\|passive` | `aggressive` (default): fights what the group is in combat with. `defensive`: only mobs attacking the bot or a member of its group. `passive`: only mobs attacking the bot (same as the `passive` order, which is now an alias of this setting; `aggressive` order = `stance aggressive`). | `BAD_ARGS` |
| `focus` / `focus off` | Takes the leader's selected hostile creature as the focus target; in a fight the bot attacks it before anything else when it is within `Bot.Chat.Control.FocusRangeYards` and not too strong. `focus off` clears it. Does not start a fight by itself (use `pull`). | `NO_TARGET`, `NOT_HOSTILE`, `DIFFERENT_MAP`, `BAD_ARGS` |
| `distance <yards>` / `distance default` | Follow distance: the bot starts walking when the leader is farther than that and stops within half of it (never below 2 yards). Default is 10 / 5. Range `Bot.Chat.Control.MinFollowYards`..`MaxFollowYards` (3..40). | `BAD_ARGS` |
| `what are you doing?` (also `report`, `what's up`) | One line per bot (up to `Bot.Chat.MaxReplyLines` bots, else the existing aggregate `status`): what it does right now plus the settings that are not at their defaults, e.g. `Thrall: fighting Defias Thug; role tank, stance defensive, follow 15y, focus Defias Thug; hp 80%, mana 40%`. `status` shows the same lines while the switch is on. | none |

## Notes and limits

* Settings live in memory per bot (`BotAI`), not in the database: they are reset when the bot logs out. They are not cleared when the group disbands.
* The role selectors `tank`, `healer`, `dps` still match by class (`Bot.Chat.Role.*`); they do not look at the `role` override.
* Focus is stored by guid; a focus that died or left the map is ignored and nothing needs clearing.
* A parallel thread ("Bot conversations") also touches chat handling. This change keeps its edits to new verbs, one branch in the shape check of `Handle` and one in the status reply.

## Not run

Built with the worldserver and tests in a container; no sim. First things to check on a server with the switch on: `chat_command` rows per verb, a `role tank` on a non-warrior makes the others assist it, `stance defensive` ignores a mob that attacks nobody in the group, `distance 20` makes the follow leg stop about 10 yards away.
