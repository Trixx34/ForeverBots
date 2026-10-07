#!/usr/bin/env python3
"""Forever Discord bot: server status, in-game chat relay and account creation.

One bot for the whole server:
  - status: every worldserver (one per ruleset realm), bnetserver, the support website (and MySQL if wanted) is checked
    with a TCP connect; changes are posted ("Roleplay realm is up") and one status board message is kept up to date.
  - chat: each realm's worldserver writes its General / Trade / LookingForGroup / city channel chat to its own Chat.log
    (TrinityCore's chat_log script, enabled with a Logger in worldserver.conf); the bot posts new lines to that realm's
    Discord channel as "[General] <Name> hi guys".
  - /register: a form for e-mail + password; the account is created with "bnetaccount create" through a worldserver's
    Remote Access (RA), logged in with the launcher's console account.

Setup and every config key: README.md next to this file. Usage: python forever_bot.py [discord_bot.json]
"""
import asyncio
import json
import logging
import os
import re
import socket
import sys
import time
import traceback

import discord
from discord import app_commands

HERE = os.path.dirname(os.path.abspath(__file__))
CFG = {}
CFG_DIR = HERE
STATE = {}
STATE_PATH = ''
BOT_ID = b'forever-discord-bot\n'

log = logging.getLogger('forever_bot')
discord.VoiceClient.warn_nacl = discord.VoiceClient.warn_dave = False     # no voice: do not warn about its libraries


# ---------------------------------------------------------------- config and state

def config_path(path):
    """Paths in the config may be relative to the config file."""
    return path if not path or os.path.isabs(path) else os.path.normpath(os.path.join(CFG_DIR, path))


def load_config(path):
    global CFG, CFG_DIR, STATE_PATH
    with open(path, encoding='utf-8-sig') as f:
        CFG = json.load(f)
    CFG_DIR = os.path.dirname(os.path.abspath(path))
    STATE_PATH = config_path(CFG.get('state_file') or 'discord_bot_state.json')
    # no RA account of its own: use the launcher's console account (localservers.json "consoleUser"/"consolePassword")
    ra = CFG.setdefault('ra', {})
    if not ra.get('user') and ra.get('localservers'):
        try:
            with open(config_path(os.path.expandvars(ra['localservers'])), encoding='utf-8-sig') as f:
                for server in json.load(f).values():
                    if server.get('consoleUser'):
                        ra['user'], ra['password'] = server['consoleUser'], server.get('consolePassword', '')
                        break
        except (OSError, ValueError) as e:
            log.warning('RA account from localservers.json: %s', e)


def load_state():
    global STATE
    try:
        with open(STATE_PATH, encoding='utf-8') as f:
            STATE = json.load(f)
    except (OSError, ValueError):
        STATE = {}
    STATE.setdefault('accounts', {})


def save_state():
    tmp = STATE_PATH + '.tmp'
    with open(tmp, 'w', encoding='utf-8') as f:
        json.dump(STATE, f, indent=2)
    os.replace(tmp, STATE_PATH)


def channel_id(value):
    try:
        return int(value or 0)
    except (TypeError, ValueError):
        return 0


# ---------------------------------------------------------------- remote access (worldserver console)

def ra_command_sync(port, command):
    """Runs one console command on a worldserver through RA (worldserver.conf Ra.Enable) and returns its output."""
    ra = CFG['ra']
    with socket.create_connection((ra.get('host', '127.0.0.1'), int(port)), timeout=15) as sock:
        def read_until(*marks):
            buf = b''
            while not any(m in buf for m in marks):
                chunk = sock.recv(4096)
                if not chunk:
                    break
                buf += chunk
            return buf.decode('utf-8', 'replace')

        read_until(b'Username: ')
        sock.sendall(ra['user'].encode() + b'\r\n')
        read_until(b'Password: ')
        sock.sendall(ra.get('password', '').encode() + b'\r\n')
        if 'Authentication failed' in read_until(b'TC>', b'Authentication failed'):
            raise RuntimeError('RA login failed (check ra.user / ra.password; the account needs Ra.MinLevel on realm -1)')
        sock.sendall(command.encode('utf-8') + b'\r\n')
        out = read_until(b'TC>')
        sock.sendall(b'quit\r\n')
    return out.replace('TC>', '').strip()


async def ra_command(port, command):
    return await asyncio.to_thread(ra_command_sync, port, command)


# ---------------------------------------------------------------- status

class Target:
    """One thing whose up/down state is watched: a realm's worldserver or a service (bnetserver, website, MySQL)."""

    def __init__(self, cfg, realm):
        self.cfg = cfg
        self.realm = realm
        self.name = cfg.get('name') or ('%s:%s' % (cfg.get('host'), cfg.get('port')))
        self.host = cfg.get('host', '127.0.0.1')
        self.port = int(cfg['port'])
        self.up = None              # None = not checked yet
        self.fails = 0
        self.since = time.time()
        self.players = None         # realms: "Connected players" from RA "server info"


async def probe(host, port, timeout):
    try:
        _, writer = await asyncio.wait_for(asyncio.open_connection(host, port), timeout)
    except (OSError, asyncio.TimeoutError):
        return False
    writer.close()
    try:
        await writer.wait_closed()
    except OSError:
        pass
    return True


PLAYERS_RE = re.compile(r'Connected players:\s*(\d+)', re.IGNORECASE)


# ---------------------------------------------------------------- chat relay

CHAT_RE = re.compile(r'Player (\S+) tells channel (.+?): (.*)$')
# WoW text escapes: colours (|cffRRGGBB, |cnIQ4:), hyperlinks (|Hitem:...|h[Name]|h -> [Name]), textures, line breaks
WOW_ESCAPES = re.compile(r'\|c[0-9a-fA-F]{8}|\|cn[^:|]*:|\|H[^|]*\|h|\|h|\|r|\|T[^|]*\|t|\|A[^|]*\|a|\|n')


class ChatTail:
    """Reads the lines a worldserver appends to its Chat.log. The file is rewritten when the server restarts (mode w)."""

    def __init__(self, path):
        self.path = path
        # existing chat is old news; a log that does not exist yet is read from its start once the server creates it
        try:
            self.offset = os.path.getsize(path)
        except OSError:
            self.offset = 0

    def read_new(self):
        try:
            size = os.path.getsize(self.path)
        except OSError:
            self.offset = 0
            return []
        if size < self.offset:
            self.offset = 0
        if size == self.offset:
            return []
        with open(self.path, 'rb') as f:
            f.seek(self.offset)
            data = f.read(size - self.offset)
        cut = data.rfind(b'\n')
        if cut < 0:
            return []               # wait until the line is complete
        self.offset += cut + 1
        return data[:cut].decode('utf-8', 'replace').splitlines()


def clean_wow_text(text):
    return WOW_ESCAPES.sub('', text).strip()


def discord_safe(text):
    return discord.utils.escape_mentions(discord.utils.escape_markdown(text))


def format_chat_line(line, realm):
    """'Player Foo tells channel General - Elwynn Forest: hi' -> '[General] <Foo> hi', or None if not relayed."""
    m = CHAT_RE.search(line)
    if not m:
        return None
    player, channel, message = m.groups()
    base, _, zone = channel.partition(' - ')
    chat = CFG.get('chat', {})
    names = {k.lower(): v for k, v in (chat.get('channels') or {}).items()}
    if names and base.lower() not in names:
        return None
    message = clean_wow_text(message)
    if not message:
        return None
    return chat.get('format', '[{channel}] <{player}> {message}').format(
        channel=names.get(base.lower(), base), zone=discord_safe(zone), player=discord_safe(player),
        message=discord_safe(message), realm=realm.get('name', ''))


# ---------------------------------------------------------------- accounts

EMAIL_RE = re.compile(r'^[A-Za-z0-9._%+-]{1,64}@[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)+$')
PASSWORD_RE = re.compile(r'^[!#$%&()*+,\-./0-9:;<=>?@A-Z\[\]^_a-z{|}~]+$')   # printable ASCII without spaces and quotes


def account_settings():
    a = CFG.get('accounts', {})
    return {
        'enabled': a.get('enabled', True),
        'channel_id': channel_id(a.get('channel_id')),
        'required_role_id': channel_id(a.get('required_role_id')),
        'log_channel_id': channel_id(a.get('log_channel_id')),
        'max_per_user': int(a.get('max_per_user', 1)),
        'min_discord_account_age_days': float(a.get('min_discord_account_age_days', 0)),
        'cooldown_seconds': int(a.get('cooldown_seconds', 300)),
        'password_min': int(a.get('password_min_length', 8)),
        'password_max': min(int(a.get('password_max_length', 16)), 128),
        'success_text': a.get('success_text', 'Log in to the launcher with **{email}** and the password you chose.'),
    }


# ---------------------------------------------------------------- the bot

class ForeverBot(discord.Client):
    def __init__(self):
        super().__init__(intents=discord.Intents.default(), allowed_mentions=discord.AllowedMentions.none())
        self.tree = app_commands.CommandTree(self)
        self.realms = CFG.get('realms', [])
        self.targets = [Target(r, r) for r in self.realms] + [Target(s, None) for s in CFG.get('services', [])]
        self.tails = [(r, ChatTail(config_path(r['chat_log']))) for r in self.realms
                      if r.get('chat_log') and channel_id(r.get('chat_channel_id'))]
        self.chat_queue = {}            # discord channel id -> lines waiting to be posted
        self.started = False
        self.last_register = {}         # discord user id -> time of the last /register try
        self.register_lock = asyncio.Lock()
        self.ra_error = None
        self.add_commands()

    # ------------------------------------------------ startup / shutdown

    async def setup_hook(self):
        guild_id = channel_id(CFG.get('guild_id'))
        if guild_id:
            # guild commands appear at once; global ones can take up to an hour
            guild = discord.Object(id=guild_id)
            self.tree.copy_global_to(guild=guild)
            synced = await self.tree.sync(guild=guild)
        else:
            synced = await self.tree.sync()
        log.info('slash commands: %s', ', '.join('/' + c.name for c in synced))

    async def on_ready(self):
        log.info('logged in as %s (%s)', self.user, self.user.id)
        if self.started:
            return                      # on_ready also fires after a reconnect
        self.started = True
        self.loop.create_task(self.guard(self.status_loop))
        if self.tails:
            self.loop.create_task(self.guard(self.chat_loop))
            log.info('chat relay: %s', ', '.join('%s -> #%s' % (r['name'], r['chat_channel_id']) for r, _ in self.tails))

    async def guard(self, loop_fn):
        """Keeps a background loop alive through unexpected errors (Discord hiccups, a log file in use...)."""
        while not self.is_closed():
            try:
                await loop_fn()
            except asyncio.CancelledError:
                raise
            except Exception:
                log.error('%s failed:\n%s', loop_fn.__name__, traceback.format_exc())
                await asyncio.sleep(10)

    async def final_sweep(self):
        """On stop (launcher Stop all stops the servers first): post what went down and mark the board as stale."""
        try:
            await asyncio.wait_for(self.check_targets(final=True), 8)
            await asyncio.wait_for(self.flush_chat(), 5)
            await asyncio.wait_for(self.update_board(offline=True), 5)
        except Exception as e:
            log.warning('shutdown update: %s', e)

    async def channel(self, cid):
        if not cid:
            return None
        ch = self.get_channel(cid)
        if ch is None:
            try:
                ch = await self.fetch_channel(cid)
            except discord.DiscordException as e:
                log.warning('channel %s: %s', cid, e)
                return None
        return ch

    async def send(self, ch, *args, **kwargs):
        """ch.send that logs instead of failing (no permission in that channel, Discord down)."""
        try:
            return await ch.send(*args, **kwargs)
        except discord.HTTPException as e:
            log.warning('cannot post in #%s (%s): %s', getattr(ch, 'name', '?'), ch.id, e)
            return None

    # ------------------------------------------------ status

    def status_cfg(self):
        s = CFG.get('status', {})
        return {
            'channel_id': channel_id(s.get('channel_id')),
            'board_channel_id': channel_id(s.get('board_channel_id')) or channel_id(s.get('channel_id')),
            'board': s.get('board', True),
            'announce': s.get('announce', True),
            'announce_on_start': s.get('announce_on_start', False),
            'interval': max(5, int(s.get('interval_seconds', 10))),
            'timeout': float(s.get('timeout_seconds', 3)),
            'fails_before_down': max(1, int(s.get('fails_before_down', 2))),
            'players_interval': int(s.get('players_interval_seconds', 60)),
            'mention_role_id': channel_id(s.get('mention_role_id')),
            'up_text': s.get('up_text', '🟢 **{name}** is up'),
            'down_text': s.get('down_text', '🔴 **{name}** is down'),
            'title': s.get('title', CFG.get('server_name', 'Forever') + ' server status'),
        }

    async def status_loop(self):
        sc = self.status_cfg()
        last_players = 0.0
        await self.check_targets(first=True)
        while True:
            if sc['players_interval'] > 0 and time.time() - last_players >= sc['players_interval']:
                last_players = time.time()
                await self.read_player_counts()
                await self.update_board()
            await asyncio.sleep(sc['interval'])
            await self.check_targets()

    async def check_targets(self, first=False, final=False):
        sc = self.status_cfg()
        results = await asyncio.gather(*(probe(t.host, t.port, sc['timeout']) for t in self.targets))
        changed = []
        for t, ok in zip(self.targets, results):
            if ok:
                t.fails = 0
                if t.up is not True:
                    changed.append((t, t.up))
                    t.up, t.since = True, time.time()
            else:
                t.fails += 1
                # a single missed connect (busy server, restart in progress) is not "down" yet; on the final sweep it is
                if t.up is not False and (t.up is None or final or t.fails >= sc['fails_before_down']):
                    changed.append((t, t.up))
                    # found down when the bot started: since when is unknown
                    t.up, t.since, t.players = False, (time.time() if t.up else None), None
        if not changed:
            return
        for t, before in changed:
            log.info('%s: %s', t.name, 'up' if t.up else 'down')
        if sc['announce'] and (not first or sc['announce_on_start']):
            ch = await self.channel(sc['channel_id'])
            if ch:
                lines = [(sc['up_text'] if t.up else sc['down_text']).format(name=t.name) for t, _ in changed]
                mention = ''
                if sc['mention_role_id'] and any(not t.up for t, _ in changed):
                    mention = '<@&%d> ' % sc['mention_role_id']
                await self.send(ch, mention + '\n'.join(lines),
                              allowed_mentions=discord.AllowedMentions(roles=bool(mention)))
        if any(t.realm and t.up for t, _ in changed):
            await self.read_player_counts()
        await self.update_board()

    async def read_player_counts(self):
        if not CFG['ra'].get('user'):
            return
        for t in self.targets:
            if t.realm and t.up and t.realm.get('ra_port'):
                try:
                    m = PLAYERS_RE.search(await ra_command(t.realm['ra_port'], 'server info'))
                    t.players = int(m.group(1)) if m else None
                    self.ra_error = None
                except Exception as e:
                    t.players = None
                    if str(e) != self.ra_error:     # say it once, not every minute
                        self.ra_error = str(e)
                        log.warning('player count from %s (RA port %s): %s', t.name, t.realm['ra_port'], e)

    def board_embed(self, offline=False):
        sc = self.status_cfg()
        all_up = all(t.up for t in self.targets)
        embed = discord.Embed(title=sc['title'], timestamp=discord.utils.utcnow(),
                              colour=discord.Colour.dark_grey() if offline else
                              discord.Colour.green() if all_up else discord.Colour.red())
        for t in self.targets:
            if t.up is None:
                value = '⚪ Unknown'
            elif t.up:
                value = '🟢 Online'
                if t.players is not None:
                    value += ' · %d player%s' % (t.players, '' if t.players == 1 else 's')
            else:
                value = '🔴 Offline' + (' since <t:%d:R>' % t.since if t.since else '')
            embed.add_field(name=t.name, value=value, inline=True)
        embed.set_footer(text='Status bot offline - last known state' if offline else 'Last checked')
        return embed

    async def update_board(self, offline=False):
        sc = self.status_cfg()
        if not sc['board']:
            return
        ch = await self.channel(sc['board_channel_id'])
        if not ch:
            return
        embed = self.board_embed(offline)
        msg_id = STATE.get('board_message_id')
        if msg_id and STATE.get('board_channel_id') == ch.id:
            try:
                await ch.get_partial_message(msg_id).edit(embed=embed)
                return
            except discord.NotFound:
                pass                    # deleted by someone: post a new one
            except discord.HTTPException as e:
                log.warning('status board: %s', e)
                return
        msg = await self.send(ch, embed=embed)
        if not msg:
            return
        STATE['board_message_id'], STATE['board_channel_id'] = msg.id, ch.id
        save_state()

    # ------------------------------------------------ chat relay

    async def chat_loop(self):
        poll = float(CFG.get('chat', {}).get('poll_seconds', 1))
        flush_every = float(CFG.get('chat', {}).get('flush_seconds', 2))
        last_flush = time.time()
        while True:
            for realm, tail in self.tails:
                for line in tail.read_new():
                    text = format_chat_line(line, realm)
                    if text:
                        self.chat_queue.setdefault(channel_id(realm['chat_channel_id']), []).append(text)
            if time.time() - last_flush >= flush_every:
                last_flush = time.time()
                await self.flush_chat()
            await asyncio.sleep(poll)

    async def flush_chat(self):
        """Posts the waiting lines, several per message (Discord allows 2000 characters, and ~5 messages / 5 s per channel)."""
        queue, self.chat_queue = self.chat_queue, {}
        for cid, lines in queue.items():
            ch = await self.channel(cid)
            if not ch:
                continue
            chunk = ''
            for line in lines:
                line = line[:1900]
                if chunk and len(chunk) + len(line) + 1 > 1900:
                    await self.send(ch, chunk)
                    chunk = ''
                chunk = chunk + '\n' + line if chunk else line
            if chunk:
                await self.send(ch, chunk)

    # ------------------------------------------------ slash commands

    def add_commands(self):
        bot = self

        @self.tree.command(name='status', description='Show which realms and services are online')
        async def status(interaction: discord.Interaction):
            await interaction.response.send_message(embed=bot.board_embed(), ephemeral=True)

        if not account_settings()['enabled']:
            return

        @self.tree.command(name='register', description='Create a game account')
        async def register(interaction: discord.Interaction):
            problem = bot.register_problem(interaction)
            if problem:
                await interaction.response.send_message(problem, ephemeral=True)
                return
            await interaction.response.send_modal(RegisterModal(bot))

    def register_problem(self, interaction):
        """Why this Discord user may not create an account (now), or None."""
        a = account_settings()
        user = interaction.user
        if a['channel_id'] and interaction.channel_id != a['channel_id']:
            return 'Please use this command in <#%d>.' % a['channel_id']
        if a['required_role_id'] and not any(r.id == a['required_role_id'] for r in getattr(user, 'roles', [])):
            return 'You need the <@&%d> role to create an account.' % a['required_role_id']
        age_days = (discord.utils.utcnow() - user.created_at).total_seconds() / 86400
        if age_days < a['min_discord_account_age_days']:
            return 'Your Discord account is too new to create a game account here.'
        made = STATE['accounts'].get(str(user.id), [])
        if a['max_per_user'] > 0 and len(made) >= a['max_per_user']:
            return 'You already created %s: %s.' % ('an account' if len(made) == 1 else '%d accounts' % len(made),
                                                    ', '.join('**%s**' % discord_safe(x['email']) for x in made))
        wait = self.last_register.get(user.id, 0) + a['cooldown_seconds'] - time.time()
        if wait > 0:
            return 'Please wait %d seconds before trying again.' % (wait + 1)
        return None

    async def create_account(self, interaction, email, password):
        """Returns the text shown to the user."""
        a = account_settings()
        if not EMAIL_RE.match(email) or len(email) > 320:
            return 'That is not a valid e-mail address (it is your login name, e.g. name@example.com).'
        if not (a['password_min'] <= len(password) <= a['password_max']):
            return 'The password must be %d to %d characters long.' % (a['password_min'], a['password_max'])
        if not PASSWORD_RE.match(password):
            return 'The password may only use letters, digits and symbols (no spaces or quotes).'
        if not CFG['ra'].get('user'):
            log.error('/register: no RA account configured (ra.user or ra.localservers)')
            return 'Account creation is not available right now. Please tell a game master.'
        # accounts are shared by all realms (auth database): any running worldserver can create them
        ports = [r['ra_port'] for r in self.realms if r.get('ra_port')]
        ports.sort(key=lambda p: not any(t.realm and t.realm.get('ra_port') == p and t.up for t in self.targets))
        async with self.register_lock:
            # the same user sending the form twice at once
            if a['max_per_user'] > 0 and len(STATE['accounts'].get(str(interaction.user.id), [])) >= a['max_per_user']:
                return 'You already created an account.'
            self.last_register[interaction.user.id] = time.time()
            out, error = None, None
            for port in ports:
                try:
                    out = await ra_command(port, 'bnetaccount create %s %s' % (email, password))
                    break
                except Exception as e:
                    error = e
            if out is None:
                log.error('/register %s: %s', email, error)
                return 'The game servers are offline, so no account can be created right now. Please try again later.'
            low = out.lower()
            if 'already exist' in low:
                return 'An account with that e-mail already exists.'
            if 'account created' not in low:       # "Battle.net account created: x with game account N#1"
                log.error('/register %s: unexpected answer: %s', email, out)
                return 'The account could not be created. Please tell a game master.'
            STATE['accounts'].setdefault(str(interaction.user.id), []).append(
                {'email': email, 'discord': str(interaction.user), 'created': int(time.time())})
            save_state()
        log.info('/register: %s created %s', interaction.user, email)
        logch = await self.channel(a['log_channel_id'])
        if logch:
            await self.send(logch, '📝 %s (`%d`) created account **%s**' % (interaction.user.mention, interaction.user.id,
                                                                        discord_safe(email)))
        return '✅ Account created!\n' + a['success_text'].format(email=discord_safe(email))


class RegisterModal(discord.ui.Modal, title='Create a game account'):
    email = discord.ui.TextInput(label='E-mail (your login name)', placeholder='name@example.com', max_length=100)
    password = discord.ui.TextInput(label='Password', placeholder='only you can see this form', min_length=1, max_length=128)
    confirm = discord.ui.TextInput(label='Password again', min_length=1, max_length=128)

    def __init__(self, bot):
        super().__init__()
        self.bot = bot
        a = account_settings()
        self.password.min_length = self.confirm.min_length = a['password_min']
        self.password.max_length = self.confirm.max_length = a['password_max']

    async def on_submit(self, interaction: discord.Interaction):
        await interaction.response.defer(ephemeral=True, thinking=True)
        email, password = self.email.value.strip(), self.password.value
        if password != self.confirm.value:
            text = 'The two passwords are not the same. Please try again.'
        else:
            problem = self.bot.register_problem(interaction)
            text = problem or await self.bot.create_account(interaction, email, password)
        await interaction.followup.send(text, ephemeral=True)

    async def on_error(self, interaction: discord.Interaction, error: Exception):
        log.error('/register form: %s', ''.join(traceback.format_exception(error)))
        try:
            await interaction.followup.send('Something went wrong. Please tell a game master.', ephemeral=True)
        except discord.DiscordException:
            pass


# ---------------------------------------------------------------- launcher control port

async def control_handler(reader, writer):
    """The launcher finds the running bot by this port (like the website by 443) and stops it with Ctrl+C."""
    writer.write(BOT_ID)
    try:
        await writer.drain()
    finally:
        writer.close()


async def main():
    port = int(CFG.get('control_port', 8190))
    try:
        server = await asyncio.start_server(control_handler, '127.0.0.1', port) if port else None
    except OSError:
        log.error('port %d is in use: is the bot already running? (config "control_port")', port)
        return 1
    bot = ForeverBot()
    try:
        async with bot:
            try:
                await bot.start(CFG['token'])
            except discord.LoginFailure:
                log.error('Discord rejected the bot token: copy it again from the Developer Portal (README.md)')
                return 1
            except discord.PrivilegedIntentsRequired:
                log.error('Discord refused the gateway intents (README.md, "Create the Discord bot")')
                return 1
            finally:
                if bot.started:
                    log.info('stopping: posting the last status')
                    await bot.final_sweep()
    finally:
        if server:
            server.close()
    return 0


if __name__ == '__main__':
    for stream in (sys.stdout, sys.stderr):
        stream.reconfigure(encoding='utf-8', errors='replace')     # names and chat can hold any character
    discord.utils.setup_logging(level=logging.INFO)
    cfg_file = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'discord_bot.json')

    def not_set_up(message, *args):
        # started by the launcher's "Start all" before anyone set it up: keep the reason readable before the window closes
        log.error(message, *args)
        time.sleep(30)
        sys.exit(1)

    if not os.path.exists(cfg_file):
        not_set_up('%s not found: copy discord_bot.example.json to discord_bot.json and fill it in (README.md)', cfg_file)
    load_config(cfg_file)
    if not CFG.get('token') or CFG['token'].startswith('PUT'):
        not_set_up('no bot token in %s (README.md, "Create the Discord bot")', cfg_file)
    load_state()
    try:
        sys.exit(asyncio.run(main()))
    except KeyboardInterrupt:
        pass
