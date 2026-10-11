# Bot guilds (Bot.Guild.*)

Requested by **Trixx34**. Everything is off until `Bot.Guild.Enabled = 1` in `botserver.conf` (entries in `botserver.conf.dist`, section GUILDS).

## What players see

- **Invites.** Invite a bot to your guild and it joins (`BotGuildPlan::InviteCheck`). Alt bots accept only their owner account; world bots accept anyone the core did not refuse, `Bot.Guild.Invite.AcceptPct` of the time. A bot that declines whispers a short reason. After joining, the bot says hello in guild chat.
- **Guild chat.** Bots in a guild: welcome a member who logs in, congratulate level-ups (every 5th level by default), answer a short hello / goodbye / thanks / "how are you" (one bot), answer when their name is used, and now and then say something unprompted while a human is online. Answers come after a random delay, with a per-bot cooldown. Alt bots (the player's own characters) stay quiet. Bots never answer bots.
- **Whisper invite.** Whisper "inv", "invite" or "can i join the guild" to a bot of a guild: it invites you if its rank may invite, otherwise it whispers why not (not in a guild, no invite right, you are already in a guild). One answer per player and bot per minute.
- **Bot guilds** (`Bot.Guild.Auto.Enabled`). Free world bots found guilds with generated names (a bot as guild master, a MOTD) and join guilds led by an online world bot of their faction, up to `Auto.MaxMembers`, so there are guilds for players to find and join.

## Code

- `BotGuildPlan.*`: pure logic (invite check, chat classification, who answers, lines, auto founding/joining, name generator). Unit tests: `tests/game/BotGuildPlan.cpp`.
- `BotGuild.*`: world-thread glue, a delayed message queue, config.
- Core hooks (one line each): `Guild::HandleInviteMember` (invite), `ChatHandler` (guild chat, whisper), `CharacterHandler` (member login), `Player::GiveLevel` (level-up), `BotMgr::Update`.
- Log reasons `GUILD_*` fall in the `social` log category.

## Limits

- A guild counts as bot-led only while its leader bot is online; auto guild building waits 5 minutes after start for that reason.
- Not compiled in the cloud container and not run on a sim server (no Boost, DB or game data there). The pure logic is tested with a Catch2 shim.
