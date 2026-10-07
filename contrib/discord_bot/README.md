# Forever Discord bot

One bot for the whole server. It does three things:

| Feature | What players see |
|---|---|
| **Server status** | A message whenever something goes up or down (`🟢 Roleplay realm is up`, `🔴 Battle.net login is down`), plus one status board message that is kept up to date: every realm with its player count, the login server, the website and the database. |
| **Realm chat** | Each realm's General, Trade, LookingForGroup and LocalDefense chat in that realm's own Discord channel: `[General] <Thrall> hi guys`. |
| **`/register`** | A form for an e-mail and password. The bot creates the game account, which works on every realm. |

Also available: `/status`, which shows the status board to whoever asks (only they see it).

It runs on the same PC as the servers. You can start and stop it by hand or from the launcher's **Local server** tab.

---

## Contents

1. [How it works](#1-how-it-works)
2. [Create the Discord bot](#2-create-the-discord-bot)
3. [Find your server, channel and role IDs](#3-find-your-server-channel-and-role-ids)
4. [Install Python and discord.py](#4-install-python-and-discordpy)
5. [Turn on the chat log in each worldserver](#5-turn-on-the-chat-log-in-each-worldserver)
6. [Write the config file](#6-write-the-config-file)
7. [The Remote Access account](#7-the-remote-access-account)
8. [Run it](#8-run-it)
9. [Config reference](#9-config-reference)
10. [Troubleshooting](#10-troubleshooting)
11. [Security notes](#11-security-notes)

---

## 1. How it works

```
 worldserver Normal   :8085  RA :3443  Chat.log ─┐
 worldserver PvP      :8095  RA :3444  logs_pvp\Chat.log ─┤
 worldserver Roleplay :8105  RA :3445  logs_rp\Chat.log ──┼──> forever_bot.py ──> Discord
 worldserver Hardcore :8115  RA :3446  logs_hc\Chat.log ──┤        ^
 bnetserver           :1119                              │        │ 127.0.0.1:8190
 support website      :443                               │     launcher (finds it, stops it with Ctrl+C)
 MySQL                :3307 ─────────────────────────────┘
```

- **Status:** every 10 seconds the bot tries to open a TCP connection to each port. A worldserver opens its port only once it has finished loading, so "up" means players can log in. "Down" is reported after 2 missed checks in a row, so a busy moment does not set it off.
- **Player counts** come from the console command `server info`, which the bot runs on each realm through Remote Access (RA) once a minute.
- **Chat:** TrinityCore's `chat_log` script writes every message in a system channel (General, Trade, LookingForGroup, LocalDefense, city channels) to the log. One logger line in `worldserver.conf` sends those lines to `Chat.log`. The bot reads new lines every second and posts them every 2 seconds, several lines per Discord message. Item links become `[Linen Cloth]`. `@everyone` and role pings typed in game are neutralised. Whispers, party, guild, say and player-made channels are **never** relayed.
- **`/register`** runs `bnetaccount create <email> <password>` through RA on the first realm that is up. Accounts live in the shared `auth` database, so the account works on every realm. The bot remembers which Discord user made which account (in `discord_bot_state.json`) and allows one account per Discord user by default.

---

## 2. Create the Discord bot

1. Open <https://discord.com/developers/applications> and click **New Application**. Name it, for example "Forever".
2. On the **Bot** page:
   - Click **Reset Token**, then **Copy**. This is the bot's password. It goes into `discord_bot.json` as `"token"`. Never post it anywhere. If it leaks, reset it here.
   - Turn **Public Bot** off, so only you can add it to servers.
   - The bot needs **no** privileged gateway intents. Leave Presence, Server Members and Message Content off.
3. On the **OAuth2** page, copy the **Client ID**, then open this link with your client ID filled in:

   ```
   https://discord.com/oauth2/authorize?client_id=YOUR_CLIENT_ID&scope=bot+applications.commands&permissions=19456
   ```

   Choose your Discord server and click **Authorize**. `19456` = View Channels + Send Messages + Embed Links, which is all the bot needs.
4. Make sure the bot's role can see and write in the channels you give it. This matters for private channels, such as a staff log channel.

Suggested Discord channels:

| Channel | Use |
|---|---|
| `#server-status` | up/down messages and the status board. Read-only for players. |
| `#normal-chat`, `#pvp-chat`, `#rp-chat`, `#hardcore-chat` | one per realm. Read-only for players. |
| `#create-account` | where `/register` may be used (optional). |
| `#account-log` | staff only: who created which account (optional). |

---

## 3. Find your server, channel and role IDs

1. In Discord: **User Settings → Advanced → Developer Mode** on.
2. Right-click your **server icon → Copy Server ID**. That is `guild_id`.
3. Right-click a **channel → Copy Channel ID**.
4. Right-click a **role** (Server Settings → Roles) **→ Copy Role ID**.

IDs are long numbers. Put them in the config **without** quotes, e.g. `"channel_id": 1234567890123456789`. `0` means "not used".

---

## 4. Install Python and discord.py

The bot needs **Python 3.10 or newer** and the `discord.py` package.

**On a PC with a normal Python install** (the development setup), in a command prompt:

```
"C:\Program Files\Python311\python.exe" -m pip install -r "F:\wow stuff\TrinityCore-forever\contrib\discord_bot\requirements.txt"
```

**In the server repack**, the website's Python (`web\python`) is the small "embeddable" Python. It has no `pip`, but it loads every package placed in its own folder (that is how `pymysql` got there). On any PC with Python **3.11** and pip, install discord.py into that folder:

```
py -3.11 -m pip install discord.py --target "C:\path\to\ForeverRepack\web\python" --only-binary=:all: --platform win_amd64 --python-version 3.11
```

Then copy `forever_bot.py` and your `discord_bot.json` to `ForeverRepack\discord_bot\`.

Check that it worked: `python -c "import discord; print(discord.__version__)"` prints a version such as `2.6.x`.

---

## 5. Turn on the chat log in each worldserver

Skip this if you don't want the chat relay.

In **every** worldserver config (`worldserver.conf`, `worldserver_pvp.conf`, `worldserver_rp.conf`, `worldserver_hc.conf`) add one appender and one logger:

```
# next to the other Appender.* lines
Appender.Chat=2,2,0,Chat.log,w

# next to the other Logger.* lines
Logger.chat.log.system=2,Chat
```

- `Appender.Chat=2,2,0,Chat.log,w` = a file (2), debug level (2), no prefix (0), `Chat.log`, recreated at each start (`w`). The file goes into that realm's `LogsDir`. In the development setup that is `Chat.log` for Normal and `logs_pvp\`, `logs_rp\`, `logs_hc\` for the others. In the repack it is `server\logs\`, `server\logs_pvp\`, and so on.
- `Logger.chat.log.system=2,Chat` = only the system channels. Addon messages use another logger name and are not written.

Restart the worldservers. Then say something in General in game. A line like `Player Thrall tells channel General - Durotar: hi` should appear in that realm's `Chat.log`.

> The development configs in `build\bin\RelWithDebInfo\` already have these two lines (added 2026-10-06). The repack's configs do not yet.

---

## 6. Write the config file

1. Copy `discord_bot.example.json` to `discord_bot.json` in the same folder. `discord_bot.json` is git-ignored because it holds your token.
2. Fill in:
   - `token`: from step 2.
   - `guild_id`: from step 3. With it, slash commands appear at once. Without it, they can take up to an hour.
   - `status.channel_id`: your status channel.
   - Each realm's `chat_channel_id`: that realm's chat channel.
   - Each realm's `chat_log`: path to that realm's `Chat.log`. A relative path starts from the folder of `discord_bot.json`.
   - Optional: `accounts.channel_id`, `accounts.log_channel_id`.
3. Check the realm and service names. They are exactly what Discord shows: `Normal realm`, `Roleplay realm`, `Battle.net login`...

The example file already has the right ports for this server (worlds 8085/8095/8105/8115, RA 3443–3446, bnet 1119, website 443, MySQL 3307). For the repack, set MySQL to **3317**, use `"chat_log": "../server/logs/Chat.log"` and so on, and set `"localservers": "../localservers.json"`.

Every key is explained in [§9](#9-config-reference).

---

## 7. The Remote Access account

Player counts and `/register` use the worldservers' Remote Access console (`Ra.Enable = 1` in each worldserver config, ports 3443–3446). The account needs GM level 3 on all realms (`RealmID -1`).

You normally need nothing new. The launcher already has such an account, the **console account** (`consoleUser` / `consolePassword` in `localservers.json`). The repack's `Setup.bat` creates it. The bot reads it from there:

```json
"ra": { "host": "127.0.0.1", "user": "", "password": "", "localservers": "%APPDATA%/ForeverLauncher/localservers.json" }
```

To give the bot its own account instead, create one in a worldserver console and put it in `ra.user` / `ra.password`. That value wins over `localservers`:

```
bnetaccount create discordbot@local SomeLongPassword
account set seclevel 4#1 3 -1       <- the game account name printed by the line above
```

Without an RA account, status and chat still work. Player counts are left out, and `/register` answers "not available".

---

## 8. Run it

### By hand

```
cd "F:\wow stuff\TrinityCore-forever\contrib\discord_bot"
"C:\Program Files\Python311\python.exe" forever_bot.py
```

or with a config file somewhere else: `python forever_bot.py D:\path\discord_bot.json`.

It prints `logged in as Forever#1234`, the slash commands it registered, and the chat channels it relays. Stop it with **Ctrl+C**. Before it exits, it posts which services went down and marks the status board "Status bot offline".

### From the launcher

Add three lines to your server's block in `localservers.json`: `%APPDATA%\ForeverLauncher\localservers.json`, or the one next to the repack's `ForeverLauncher.exe`. They go next to `webExe` / `webArgs` / `webPort`:

```json
"botExe": "C:\\Program Files\\Python311\\python.exe",
"botArgs": "\"F:\\wow stuff\\TrinityCore-forever\\contrib\\discord_bot\\forever_bot.py\"",
"botPort": 8190
```

Repack version:

```json
"botExe": "{here}\\web\\python\\python.exe",
"botArgs": "\"{here}\\discord_bot\\forever_bot.py\"",
"botPort": 8190
```

Restart the launcher. The **Local server** tab then shows a **Discord bot** row with Start / Stop:

- **Start all** starts the bot **first**, so it reports MySQL, login, every realm and the website as they come up.
- **Stop all** stops it **last**, so it can report everything going down.
- The launcher recognises the running bot by `botPort`. That must be the same number as `control_port` in `discord_bot.json` (default 8190). This port only answers on 127.0.0.1. It also stops a second copy of the bot from starting.

### What users do

- `/status`: the status board, visible only to the person who asked.
- `/register`: opens a form with **E-mail**, **Password** and **Password again**. Only the person filling it in sees the form and the answer. The e-mail is the login name, so it doesn't have to be a real address, but it must look like one (`name@example.com`). The password must be 8–16 characters (configurable): letters, digits and symbols, no spaces or quotes.

---

## 9. Config reference

### Top level

| Key | Default | Meaning |
|---|---|---|
| `server_name` | `Forever` | Used in the status board title ("Forever server status"). |
| `token` | – | Bot token (step 2). **Required.** |
| `guild_id` | `0` | Your Discord server ID. With it, slash commands appear at once; with 0 they are global (up to an hour). |
| `control_port` | `8190` | Local port the launcher uses to find the bot. Same number as the launcher's `botPort`. `0` = none. |
| `state_file` | `discord_bot_state.json` | Where the bot keeps the status board message ID and who registered which account. |

### `status`

| Key | Default | Meaning |
|---|---|---|
| `channel_id` | `0` | Channel for up/down messages. |
| `board` | `true` | Keep one status board message up to date. |
| `board_channel_id` | = `channel_id` | Channel for the board. Use a separate read-only channel if the up/down messages push it out of sight. Deleting the board message is fine: the bot posts a new one. |
| `interval_seconds` | `10` | Time between checks (minimum 5). |
| `timeout_seconds` | `3` | How long a check waits for a connection. |
| `fails_before_down` | `2` | Missed checks in a row before something counts as down. Coming up is reported at once. |
| `players_interval_seconds` | `60` | How often player counts are read over RA. `0` = never. |
| `mention_role_id` | `0` | A role pinged when something goes **down** (e.g. `@Staff`). The role must be "mentionable", or the bot needs the Mention @everyone permission. |
| `announce` | `true` | Post up/down messages at all. With `false`, only the board changes. |
| `announce_on_start` | `false` | Also post the state found when the bot starts. Normally the bot starts before the servers, so it reports them coming up anyway. |
| `up_text` / `down_text` | `🟢 **{name}** is up` / `🔴 **{name}** is down` | Message text. `{name}` = the realm or service name. |
| `title` | `<server_name> server status` | Board title. |

### `realms` (one entry per worldserver)

| Key | Meaning |
|---|---|
| `name` | Shown in Discord: `Normal realm`, `PvP realm`, `Roleplay realm`, `Hardcore realm`. |
| `host`, `port` | The worldserver's `WorldServerPort` (8085, 8095, 8105, 8115). |
| `ra_port` | The worldserver's `Ra.Port` (3443–3446). Used for player counts and `/register`. |
| `chat_log` | Path to that realm's `Chat.log` (step 5). Leave it out for no chat relay. |
| `chat_channel_id` | Discord channel for that realm's chat. `0` = no chat relay. Several realms may share a channel; then add `{realm}` to `chat.format`. |

### `services` (anything else to watch)

A list of `{ "name": "...", "host": "...", "port": N }`. The example has Battle.net login (1119), Support website (443) and Database (3307). Remove the ones you don't want, or add more, e.g. the bnet REST login `8081`.

### `chat`

| Key | Default | Meaning |
|---|---|---|
| `channels` | General, Trade, LookingForGroup, LocalDefense | Which in-game channels to relay, and the label each gets in Discord: `{"General": "General", "LookingForGroup": "LFG"}` shows `[LFG]`. An empty `{}` relays every system channel. |
| `format` | `[{channel}] <{player}> {message}` | Line format. Placeholders: `{channel}`, `{player}`, `{message}`, `{zone}` (e.g. Durotar for General; empty for LFG), `{realm}`. Example: `**[{channel}]** <{player}> {message}`. |
| `poll_seconds` | `1` | How often the logs are read. |
| `flush_seconds` | `2` | How often waiting lines are posted. Lines are grouped, which keeps Discord's rate limits happy in a busy Trade chat. |

### `ra`

| Key | Default | Meaning |
|---|---|---|
| `host` | `127.0.0.1` | Where the worldservers' RA listens. |
| `user`, `password` | empty | Account for RA (see [§7](#7-the-remote-access-account)). Empty = take it from `localservers`. |
| `localservers` | – | Path to the launcher's `localservers.json`, which holds the console account. `%APPDATA%` and other variables are expanded; a relative path starts from the config's folder. |

### `accounts` (`/register`)

| Key | Default | Meaning |
|---|---|---|
| `enabled` | `true` | `false` = no `/register` command. |
| `channel_id` | `0` | Only allow `/register` in this channel. `0` = anywhere. |
| `required_role_id` | `0` | Only members with this role (e.g. "Verified") may register. |
| `log_channel_id` | `0` | Staff channel where each new account is logged with the Discord user. Passwords are never logged. |
| `max_per_user` | `1` | Accounts per Discord user. `0` = no limit. |
| `min_discord_account_age_days` | `7` | Discord accounts younger than this can't register (stops throw-away accounts). |
| `cooldown_seconds` | `300` | Wait between two attempts by the same user. Typos in the form don't start the wait. |
| `password_min_length` / `password_max_length` | `8` / `16` | Allowed password length (max 128; the game account part is cut to 16 by the server, so 16 keeps it simple). |
| `success_text` | `Log in to the launcher with **{email}** …` | Shown after a successful registration. `{email}` = the new login. |

To let someone register again (they forgot the e-mail, or you deleted their account), stop the bot, remove their entry from `"accounts"` in `discord_bot_state.json`, and start it again.

---

## 10. Troubleshooting

| Problem | Fix |
|---|---|
| `Discord rejected the bot token` | Copy the token again (Developer Portal → Bot → Reset Token). Paste it without spaces. |
| `port 8190 is in use: is the bot already running?` | A copy is already running (look for its console window, or stop it in the launcher). Or another program uses the port: change `control_port` **and** the launcher's `botPort`. |
| `ModuleNotFoundError: No module named 'discord'` | Step 4 was done with a different Python than the one that runs the bot. Install it with the same `python.exe` that's in `botExe`. |
| Slash commands don't show up | Set `guild_id`. Invite the bot with the link from step 2: it needs the `applications.commands` scope. Restart Discord (Ctrl+R). |
| Status messages don't appear | Check `status.channel_id`, and that the bot can see and write in that channel. The bot's console prints a warning for channels it can't use. |
| A realm shows down although it runs | The `port` must be that worldserver's `WorldServerPort`. A worldserver still loading counts as down until it opens its port. |
| No chat in Discord | 1) Is there a `Chat.log` in that realm's log folder, and does it grow when you talk in General? If not, see step 5 and restart that worldserver. 2) Check `chat_log` path and `chat_channel_id`. 3) Only General, Trade, LookingForGroup and LocalDefense are relayed by default (`chat.channels`). Player-made channels never are. |
| No player counts | The bot has no working RA account (console shows `RA login failed`): see [§7](#7-the-remote-access-account). |
| `/register` says "not available right now" | No RA account configured (§7). |
| `/register` says the servers are offline | No realm's RA port answered: at least one worldserver must run. |
| `/register` says "could not be created" | The console window shows the server's answer. Usually the RA account lacks the `bnetaccount create` permission: it needs GM level 3 on realm `-1`. |
| Launcher's Stop kills the bot instead of stopping it | It gets 20 seconds after Ctrl+C. If Discord is unreachable at that moment, the last status post can take longer. Nothing is lost except that last post. |

The bot's console (or the launcher-started console window, minimized) prints every status change, every account created, and every error with details.

---

## 11. Security notes

- The **token** controls your bot. Keep `discord_bot.json` private (it is git-ignored). If the token leaks, reset it in the Developer Portal.
- `/register` passwords go from Discord to the bot, then over a local RA connection (127.0.0.1) to the worldserver. They are **not** stored or logged by the bot. The Discord form doesn't hide the characters while typing, but only the person filling it in can see it.
- The e-mail and password are checked (letters, digits and symbols only; no spaces or quotes) before they are sent to the console, so nobody can slip a second console command in.
- In-game chat is shown as plain text in Discord: markdown is escaped, and `@everyone`, `@here`, user and role pings can't trigger.
- Up/down checks are plain TCP connects from this PC. With network logging at trace level (`Logger.network=1`), worldservers log those connections. Set it to 3 if the extra lines bother you.
