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
import datetime
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
        self.show = cfg.get('show', True)           # on the status board and /status
        self.announce = cfg.get('announce', True)   # "... is up / is down" messages
        self.host = cfg.get('host', '127.0.0.1')
        self.port = int(cfg['port'])
        self.up = None              # None = not checked yet
        self.fails = 0
        self.since = None           # when the bot saw it go up or down (None: already so when the bot started)
        self.players = None         # realms: "Connected players" from RA "server info"
        self.uptime = None          # realms: "Server uptime" from RA "server info", e.g. "3 Hour(s) 12 Minute(s)"
        self.factions = None        # realms: (alliance, horde) online characters from the characters database


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
UPTIME_RE = re.compile(r'Server uptime:\s*([^\r\n]+)', re.IGNORECASE)
UPTIME_PART_RE = re.compile(r'(\d+)\s*(Day|Hour|Minute|Second)', re.IGNORECASE)


def short_uptime(text):
    """'2 Day(s) 3 Hour(s) 12 Minute(s) 5 Second(s)' -> '2d 3h 12m' (seconds only while under a minute)."""
    parts = [(int(n), unit[0].lower()) for n, unit in UPTIME_PART_RE.findall(text)]
    shown = [f'{n}{u}' for n, u in parts if u != 's' and n] or [f'{n}{u}' for n, u in parts if u == 's']
    return ' '.join(shown) or text

# Faction of each playable race (server RaceMask.h RACEMASK_ALLIANCE: Skyborne 95 = High Order / Alliance, 96 = Windshaper /
# Horde); 24 = Pandaren before choosing a faction; every other playable race is Horde
ALLIANCE_RACES = {1, 3, 4, 7, 11, 22, 25, 29, 30, 32, 34, 37, 52, 85, 86, 95}
NEUTRAL_RACES = {24}


def faction_counts(characters_db):
    """(alliance, horde) characters online on one realm, from its characters database (config "db")."""
    import pymysql          # only needed for faction counts; the repack's Python ships it
    db = CFG['db']
    conn = pymysql.connect(host=db.get('host', '127.0.0.1'), port=int(db.get('port', 3306)), user=db['user'],
                           password=db.get('password', ''), database=characters_db, connect_timeout=5)
    try:
        with conn.cursor() as cur:
            cur.execute('SELECT race, COUNT(*) FROM characters WHERE online = 1 GROUP BY race')
            rows = cur.fetchall()
    finally:
        conn.close()
    alliance = sum(n for race, n in rows if race in ALLIANCE_RACES)
    horde = sum(n for race, n in rows if race not in ALLIANCE_RACES and race not in NEUTRAL_RACES)
    return alliance, horde


# ---------------------------------------------------------------- chat relay

# "Player Thrall (H) tells channel General - Durotar: hi" (the faction is written by servers since 2026-10-06, older ones leave it out)
CHAT_RE = re.compile(r'Player (\S+)(?: \(([AH])\))? tells channel (.+?): (.*)$')
DEFAULT_CHAT_FORMAT = '{faction}[{channel}] <{player}> {message}'
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
    """'Player Foo (A) tells channel General - Elwynn Forest: hi' -> '[A] [General] <Foo> hi', or None if not relayed."""
    m = CHAT_RE.search(line)
    if not m:
        return None
    player, faction, channel, message = m.groups()
    base, _, zone = channel.partition(' - ')
    chat = CFG.get('chat', {})
    names = {k.lower(): v for k, v in (chat.get('channels') or {}).items()}
    if names and base.lower() not in names:
        return None
    message = clean_wow_text(message)
    if not message:
        return None
    return chat.get('format', DEFAULT_CHAT_FORMAT).format(
        channel=names.get(base.lower(), base), zone=discord_safe(zone), player=discord_safe(player),
        message=discord_safe(message), realm=realm.get('name', ''), faction='[%s] ' % faction if faction else '')


# ---------------------------------------------------------------- Discord -> game (the in-game Discord channel)

CUSTOM_EMOJI_RE = re.compile(r'<a?(:\w+:)\d+>')         # <:pepe:1234> -> :pepe:
NAME_KEEP_RE = re.compile(r'[^\w\-]', re.UNICODE)


def discord_to_game(message):
    """(sender, text) of a Discord message for the game, plain text only, or None when nothing is left to send.
    Attachments, embeds, stickers and reactions are dropped; mentions become plain names (clean_content); no '|' (WoW escapes),
    no line breaks; the server cuts the text to 255 characters."""
    text = CUSTOM_EMOJI_RE.sub(r'\1', message.clean_content)
    text = ' '.join(text.replace('|', '/').split())
    if not text:
        return None                 # only an image, a sticker...
    name = NAME_KEEP_RE.sub('', message.author.display_name.replace(' ', '_'))[:24] or 'Discord'
    return name, text


# ---------------------------------------------------------------- Group Finder listings -> LFG channels

CLASS_NAMES = {1: 'Warrior', 2: 'Paladin', 3: 'Hunter', 4: 'Rogue', 5: 'Priest', 6: 'Death Knight', 7: 'Shaman', 8: 'Mage',
               9: 'Warlock', 10: 'Monk', 11: 'Druid', 12: 'Demon Hunter', 13: 'Evoker'}
_activity_names = None


def activity_name(activity_id):
    """GroupFinderActivity names from the client data (group_finder_activities.json next to this file)."""
    global _activity_names
    if _activity_names is None:
        try:
            with open(os.path.join(HERE, 'group_finder_activities.json'), encoding='utf-8') as f:
                _activity_names = {int(k): v for k, v in json.load(f).items()}
        except (OSError, ValueError):
            _activity_names = {}
    return _activity_names.get(activity_id, 'Activity %d' % activity_id)


class LfgLog:
    """The current listings of one realm, from its GroupFinder.log (one JSON line per change, written anew at server start)."""

    def __init__(self, path):
        self.path = path
        self.offset = 0
        self.listings = {}          # listing id -> last listed/updated event
        self.restarted = False      # the server started a new log: every post of this realm is stale

    def read(self):
        """Applies the new lines; returns True if anything changed."""
        try:
            size = os.path.getsize(self.path)
        except OSError:
            return False
        changed = False
        if size < self.offset:
            self.offset, self.listings, self.restarted, changed = 0, {}, True, True
        if size == self.offset:
            return changed
        with open(self.path, 'rb') as f:
            f.seek(self.offset)
            data = f.read(size - self.offset)
        cut = data.rfind(b'\n')
        if cut < 0:
            return changed
        self.offset += cut + 1
        for line in data[:cut].decode('utf-8', 'replace').splitlines():
            start = line.find('{')
            if start < 0:
                continue
            try:
                ev = json.loads(line[start:])
            except ValueError:
                continue
            if ev.get('event') == 'delisted':
                self.listings.pop(ev.get('id'), None)
            elif ev.get('event') in ('listed', 'updated'):
                self.listings[ev.get('id')] = ev
            changed = True
        return changed


PLAY_STYLES = {1: 'Learning', 2: 'Relaxed', 3: 'Competitive', 4: 'Carry Offered'}     # the listing's play style dropdown


def lfg_embed(ev, realm_name):
    names = [activity_name(a) for a in ev.get('activities', [])]
    title = ', '.join(names[:3]) + (' +%d' % (len(names) - 3) if len(names) > 3 else '') or 'Group'
    alliance = ev.get('faction') == 'A'
    text = '\n'.join(discord_safe(t) for t in (ev.get('title'), ev.get('comment')) if t)[:900]
    style = ev.get('playstyle') or 0
    if style:
        # green: Discord colours "+" lines of a diff code block (desktop and mobile)
        text += ('\n' if text else '') + '```diff\n+ %s\n```' % PLAY_STYLES.get(style, 'Play style %d' % style)
    embed = discord.Embed(title=title[:250], colour=discord.Colour.blue() if alliance else discord.Colour.red(),
                          description=text or None)
    lines = []
    for m in ev.get('members', []):
        roles = (' 🛡️' if m.get('tank') else '') + (' ➕' if m.get('healer') else '') + (' 🗡️' if m.get('damage') else '')
        lines.append('%s**%s** %d %s%s' % ('👑 ' if m.get('leader') else '', discord_safe(m.get('name', '?')), m.get('level', 0),
                                           CLASS_NAMES.get(m.get('class'), ''), roles))
    embed.add_field(name='%s · %d member%s' % ('Alliance' if alliance else 'Horde', len(lines), '' if len(lines) == 1 else 's'),
                    value='\n'.join(lines)[:1000] or '-', inline=False)
    embed.set_footer(text='%s · listed' % realm_name)
    if ev.get('time'):
        embed.timestamp = datetime.datetime.fromtimestamp(ev['time'], datetime.timezone.utc)
    return embed


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
        intents = discord.Intents.default()
        # Discord -> game needs to read message text: the privileged Message Content intent (switched on in the Developer Portal)
        self.from_discord = bool(CFG.get('chat', {}).get('from_discord', False))
        intents.message_content = self.from_discord
        super().__init__(intents=intents, allowed_mentions=discord.AllowedMentions.none())
        self.tree = app_commands.CommandTree(self)
        self.realms = CFG.get('realms', [])
        # Discord channel id -> the realms whose in-game Discord channel gets its messages
        self.game_feeds = {}
        for r in self.realms:
            cid = channel_id(r.get('chat_channel_id'))
            if self.from_discord and cid and r.get('ra_port'):
                self.game_feeds.setdefault(cid, []).append(r)
        self.feed_locks = {}            # ra port -> lock: one console command at a time per realm, in order
        # Group Finder: realm -> its log; no LFG channel set = nothing posted for that realm
        self.lfg = [(r, LfgLog(config_path(r['lfg_log']))) for r in self.realms
                    if r.get('lfg_log') and channel_id(r.get('lfg_channel_id'))]
        self.targets = [Target(r, r) for r in self.realms] + [Target(s, None) for s in CFG.get('services', [])]
        self.tails = [(r, ChatTail(config_path(r['chat_log']))) for r in self.realms
                      if r.get('chat_log') and channel_id(r.get('chat_channel_id'))]
        self.chat_queue = {}            # discord channel id -> lines waiting to be posted
        self.started = False
        self.last_register = {}         # discord user id -> time of the last /register try
        self.register_lock = asyncio.Lock()
        self.ra_error = None
        self.db_error = None
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
        if self.lfg:
            self.loop.create_task(self.guard(self.lfg_loop))
            log.info('LFG posts: %s', ', '.join('%s -> #%s' % (r['name'], r['lfg_channel_id']) for r, _ in self.lfg))
            log.info('chat relay: %s', ', '.join('%s -> #%s' % (r['name'], r['chat_channel_id']) for r, _ in self.tails))
        if self.game_feeds:
            log.info('Discord -> game: %s', ', '.join('#%s -> %s' % (cid, ', '.join(r['name'] for r in rs)) for cid, rs in self.game_feeds.items()))

    async def on_message(self, message):
        """A message in a realm's Discord chat channel goes to that realm's in-game Discord channel (".discord say")."""
        realms = self.game_feeds.get(message.channel.id)
        if not realms or message.author.bot or message.webhook_id or message.type not in (discord.MessageType.default, discord.MessageType.reply):
            return                      # bots include this one: what it posts from the game never goes back in
        out = discord_to_game(message)
        if not out or not CFG['ra'].get('user'):
            return
        sender, text = out
        for realm in realms:
            port = realm['ra_port']
            lock = self.feed_locks.setdefault(port, asyncio.Lock())
            async with lock:
                try:
                    reply = await ra_command(port, 'discord say %s %s' % (sender, text))
                    if 'sent to' not in reply:
                        log.warning('Discord -> %s: %s', realm['name'], reply.strip()[:200])
                except Exception as e:
                    log.warning('Discord -> %s (RA port %s): %s', realm['name'], port, e)

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
            'tidy': s.get('tidy', True),                        # delete older up/down messages, keep the board
            'keep': max(0, int(s.get('keep_messages', 1))),     # how many of the newest up/down messages stay
        }

    async def status_loop(self):
        sc = self.status_cfg()
        last_players = 0.0
        if sc['tidy']:
            await self.sweep_status_channel()
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
                    # found up when the bot started: since when is unknown (realms still show their own uptime)
                    t.up, t.since = True, (time.time() if t.up is False else None)
            else:
                t.fails += 1
                # a single missed connect (busy server, restart in progress) is not "down" yet; on the final sweep it is
                if t.up is not False and (t.up is None or final or t.fails >= sc['fails_before_down']):
                    changed.append((t, t.up))
                    # found down when the bot started: since when is unknown
                    t.up, t.since, t.players, t.uptime, t.factions = False, (time.time() if t.up else None), None, None, None
        if not changed:
            return
        for t, before in changed:
            log.info('%s: %s', t.name, 'up' if t.up else 'down')
        announced = [(t, before) for t, before in changed if t.announce]
        if sc['announce'] and announced and (not first or sc['announce_on_start']):
            ch = await self.channel(sc['channel_id'])
            if ch:
                lines = [(sc['up_text'] if t.up else sc['down_text']).format(name=t.name) for t, _ in announced]
                mention = ''
                if sc['mention_role_id'] and any(not t.up for t, _ in announced):
                    mention = '<@&%d> ' % sc['mention_role_id']
                msg = await self.send(ch, mention + '\n'.join(lines),
                                      allowed_mentions=discord.AllowedMentions(roles=bool(mention)))
                if msg and sc['tidy']:
                    await self.tidy_announcements(ch, msg.id)
        if any(t.realm and t.up for t, _ in changed):
            await self.read_player_counts()
        await self.update_board()

    async def tidy_announcements(self, ch, new_id):
        """Keeps only the newest 'keep_messages' up/down messages of the bot; the status board is never touched."""
        keep = self.status_cfg()['keep']
        posted = STATE.setdefault('status_messages', [])
        posted.append(new_id)
        old, STATE['status_messages'] = posted[:-keep] if keep else posted, posted[-keep:] if keep else []
        save_state()
        for mid in old:
            if mid != STATE.get('board_message_id'):
                await self.delete_message(ch.id, mid)

    async def sweep_status_channel(self):
        """At start: the bot's own older messages in the status channel (from before tidying, or a crash) go, except the board
        and the newest up/down messages. Messages of people are never touched. Needs Read Message History in that channel."""
        sc = self.status_cfg()
        ch = await self.channel(sc['channel_id'])
        if not ch:
            return
        mine = []
        try:
            async for m in ch.history(limit=500):
                if m.author.id == self.user.id and m.id != STATE.get('board_message_id'):
                    mine.append(m)                              # newest first
        except discord.Forbidden:
            log.warning('status channel: cannot read its history to clean up old messages; give the bot "Read Message History" '
                        'in #%s (new messages are tidied anyway)', getattr(ch, 'name', ch.id))
            return
        except discord.HTTPException as e:
            log.warning('status channel history: %s', e)
            return
        keep = sc['keep']
        STATE['status_messages'] = [m.id for m in reversed(mine[:keep])]
        save_state()
        removed = 0
        for m in mine[keep:]:
            try:
                await m.delete()
                removed += 1
            except discord.HTTPException:
                pass
        if removed:
            log.info('status channel: removed %d old messages', removed)

    async def read_player_counts(self):
        for t in self.targets:
            if t.realm and t.up and t.realm.get('characters_db') and CFG.get('db'):
                try:
                    t.factions = await asyncio.to_thread(faction_counts, t.realm['characters_db'])
                    self.db_error = None
                except Exception as e:
                    t.factions = None
                    if str(e) != self.db_error:
                        self.db_error = str(e)
                        log.warning('faction counts from %s (database %s): %s', t.name, t.realm['characters_db'], e)
        if not CFG['ra'].get('user'):
            return
        for t in self.targets:
            if t.realm and t.up and t.realm.get('ra_port'):
                try:
                    info = await ra_command(t.realm['ra_port'], 'server info')
                    m = PLAYERS_RE.search(info)
                    t.players = int(m.group(1)) if m else None
                    m = UPTIME_RE.search(info)
                    t.uptime = short_uptime(m.group(1).strip()) if m else None
                    self.ra_error = None
                except Exception as e:
                    t.players = None
                    if str(e) != self.ra_error:     # say it once, not every minute
                        self.ra_error = str(e)
                        log.warning('player count from %s (RA port %s): %s', t.name, t.realm['ra_port'], e)

    def board_embed(self, offline=False):
        sc = self.status_cfg()
        shown = [t for t in self.targets if t.show]
        all_up = all(t.up for t in shown)
        embed = discord.Embed(title=sc['title'], timestamp=discord.utils.utcnow(),
                              colour=discord.Colour.dark_grey() if offline else
                              discord.Colour.green() if all_up else discord.Colour.red())
        for t in shown:
            if t.up is None:
                value = '⚪ Unknown'
            elif t.up:
                value = '🟢 Online'
                if t.players is not None:
                    value += ' · %d player%s' % (t.players, '' if t.players == 1 else 's')
                if t.factions:
                    value += '\nAlliance %d · Horde %d' % t.factions
                if t.uptime:
                    value += '\nUp %s' % t.uptime
                elif t.since:
                    value += '\nUp since <t:%d:R>' % t.since
            else:
                value = '🔴 Offline' + ('\nDown since <t:%d:R>' % t.since if t.since else '')
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

    async def lfg_loop(self):
        """Keeps one message per Group Finder listing in each realm's LFG channel: posted, edited as it changes, deleted."""
        posts = STATE.setdefault('lfg_posts', {})       # realm name -> {listing id: [channel id, message id]}
        # posts of an earlier run are not tied to this run's listings: remove them, the log rebuilds the current ones
        for realm, _ in self.lfg:
            for post in list(posts.get(realm['name'], {}).values()):
                await self.delete_message(post[0], post[1])
            posts[realm['name']] = {}
        save_state()
        while True:
            for realm, feed in self.lfg:
                if not feed.read():
                    continue
                mine = posts.setdefault(realm['name'], {})
                if feed.restarted:                          # the realm restarted: no listing survives
                    feed.restarted = False
                    for post in list(mine.values()):
                        await self.delete_message(post[0], post[1])
                    mine.clear()
                ch = await self.channel(channel_id(realm['lfg_channel_id']))
                if not ch:
                    continue
                for lid in [k for k in mine if int(k) not in feed.listings]:
                    post = mine.pop(lid)
                    await self.delete_message(post[0], post[1])
                for lid, ev in feed.listings.items():
                    key = str(lid)
                    sig = json.dumps(ev, sort_keys=True)            # only listings that really changed are edited
                    if key in mine and mine[key][2:] == [sig]:
                        continue
                    embed = lfg_embed(ev, realm['name'])
                    if key in mine:
                        try:
                            await ch.get_partial_message(mine[key][1]).edit(embed=embed)
                            mine[key] = mine[key][:2] + [sig]
                            continue
                        except discord.NotFound:
                            del mine[key]                           # deleted by someone: post it again
                        except discord.HTTPException as e:
                            log.warning('LFG post: %s', e)
                            continue
                    msg = await self.send(ch, embed=embed)
                    if msg:
                        mine[key] = [ch.id, msg.id, sig]
                save_state()
            await asyncio.sleep(float(CFG.get('lfg', {}).get('poll_seconds', 2)))

    async def delete_message(self, cid, mid):
        try:
            ch = await self.channel(cid)
            if ch:
                await ch.get_partial_message(mid).delete()
        except discord.HTTPException:
            pass                        # already gone

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
                log.error('Discord refused the Message Content intent that "from_discord" needs: Developer Portal -> your bot -> Bot -> '
                          'Privileged Gateway Intents -> turn on MESSAGE CONTENT INTENT (or set chat.from_discord to false)')
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
