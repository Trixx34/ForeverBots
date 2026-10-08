#!/usr/bin/env python3
"""Read-only check: per-entry spawn count change of the sniff maps import between two commits.

Usage: python3 -I docs/playerbots/check_spawn_delta.py [old_rev] [new_rev]   (run from the repo root; defaults fc54d291 3f3bbd53)

Counts creature and gameobject rows per (entry, map) in sql/custom/world/2026_10_03_00_world_forever_sniff_maps.sql at both revisions
(guids are renumbered between them, so only the entry counts are compared) and lists the entries whose count changed.
"Quest-relevant" means the entry appears as a creature_queststarter/questender row, a MONSTER (type 0) or GAMEOBJECT (type 2)
quest_objectives row in sql/custom/world. Entries that dropped to zero in this file are flagged: BotQuest::Analyze would turn them
into NO_TARGET_SPAWN only if the database has no other spawn of the entry (this file adds only entries with no spawn on that map).
"""
import collections, glob, re, subprocess, sys

F = 'sql/custom/world/2026_10_03_00_world_forever_sniff_maps.sql'
old, new = (sys.argv[1:3] + ['fc54d291', '3f3bbd53'][len(sys.argv[1:3]):])


def counts(rev):
    text = subprocess.run(['git', 'show', f'{rev}:{F}'], capture_output=True, text=True, check=True).stdout
    cur, c = None, {'creature': collections.Counter(), 'gameobject': collections.Counter()}
    for line in text.splitlines():
        m = re.match(r'INSERT INTO `(creature|gameobject)`', line)
        if m:
            cur = m.group(1)
            continue
        m = re.match(r'\((\d+),(\d+),(\d+),', line)
        if m and cur:
            c[cur][(int(m.group(2)), int(m.group(3)))] += 1
    return c


def quest_entries():
    npcs, gos = set(), set()
    for path in glob.glob('sql/custom/world/*.sql'):
        table = None
        for line in open(path, encoding='utf-8', errors='replace'):
            m = re.match(r'(?:REPLACE|INSERT)(?: IGNORE)? INTO `(\w+)`', line)
            if m:
                table = m.group(1)
            if table in ('creature_queststarter', 'creature_questender'):
                npcs.update(int(x) for x in re.findall(r'\((\d+),\d+,', line))
            elif table == 'quest_objectives':
                m = re.match(r'\(\d+,\d+,(\d+),\d+,\d+,(-?\d+),', line)
                if m and m.group(1) == '0':
                    npcs.add(int(m.group(2)))
                elif m and m.group(1) == '2':
                    gos.add(int(m.group(2)))
    return {'creature': npcs, 'gameobject': gos}


a, b, rel = counts(old), counts(new), quest_entries()
print(f'# Spawn count change per entry, {old} -> {new}\n# file: {F}')
for kind in ('creature', 'gameobject'):
    ch = sorted((e, a[kind][e], b[kind][e]) for e in set(a[kind]) | set(b[kind]) if a[kind][e] != b[kind][e])
    q = [x for x in ch if x[0][0] in rel[kind]]
    print(f'\n## {kind}: entries with spawns {len({e[0] for e in a[kind]})} -> {len({e[0] for e in b[kind]})}, '
          f'changed {len(ch)} (up {sum(1 for x in ch if x[2] > x[1])}, down {sum(1 for x in ch if x[2] < x[1])}), '
          f'dropped to zero {sum(1 for x in ch if x[2] == 0)}, quest-relevant changed {len(q)}')
    for (entry, mp), x, y in q:
        print(f'{kind} entry {entry} map {mp}: {x} -> {y}' + ('   ZERO' if y == 0 else '   DOWN' if y < x else ''))
