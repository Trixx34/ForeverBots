#!/usr/bin/env python3
"""Read-only check: which quests get a different effective level / minimum level between two commits.

Usage: python3 -I docs/playerbots/check_quest_levels.py [old_rev] [new_rev]   (repo root; defaults fc54d291 3f3bbd53)

Replays the statements that fill quest_template_classic_level (quest level) and quest_classic_level (quest level, minimum level)
in sql/custom/world, in file name order, at both revisions, then compares. Only the repo SQL is replayed; quests that have no row
fall back to ContentTuning (not in the repo) and are listed as "no row". Bot gates compared (BotQuest.cpp):
  QUEST_LEVEL block  : bot level < MinLevel          (Player::SatisfyQuestMinLevel, called at BotQuest.cpp:1604)
  too-hard skip      : quest level > bot level + 2   (BotQuest.cpp:1611 and 2022)
The first bot level that may take a quest is max(MinLevel, QuestLevel - 2); a changed value there is a gate crossing.
"""
import re, subprocess, sys

old, new = (sys.argv[1:3] + ['fc54d291', '3f3bbd53'][len(sys.argv[1:3]):])
D = 'sql/custom/world/'
FILES = ['2026_09_27_00_world_forever_baseline_01.sql', '2026_09_27_00_world_forever_baseline_02.sql', '2026_10_01_05_world_quest_classic_level.sql',
         '2026_10_02_07_world_forever_quest_min_levels.sql', '2026_10_05_16_world_skyborne_start_sniff.sql', '2026_10_06_03_world_forever_quest_levels_sniff.sql']


def rows(text, table):
    out = []
    for m in re.finditer(r'(?:REPLACE|INSERT)(?: IGNORE)? INTO `%s`(?: \([^)]*\))? VALUES\s*(.*?);' % table, text, re.S):
        out += [tuple(int(x) for x in t.split(',')) for t in re.findall(r'\(([-\d,]+)\)', m.group(1))]
    return out


def replay(rev):
    tpl, cl = {}, {}   # id -> quest level ; id -> (quest level, min level)
    for f in FILES:
        text = subprocess.run(['git', 'show', f'{rev}:{D}{f}'], capture_output=True, text=True).stdout
        if not text:
            continue
        if 'DELETE FROM `quest_template_classic_level`;' in text:
            tpl.clear()
        for i, _t, ql in rows(text, 'quest_template_classic_level'):
            tpl[i] = ql
        for i, ql, mn, _mx in rows(text, 'quest_classic_level'):
            cl[i] = (ql, mn)
        if f.endswith('quest_min_levels.sql'):
            for i in [k for k in cl if k >= 90000]:
                del cl[i]
            for i, ql in tpl.items():
                if i >= 90000 and ql > 0:
                    cl[i] = (ql, max(1, ql - 3))
        m = re.search(r'UPDATE `quest_classic_level` SET `MinLevel` = (\d+) WHERE `ID` IN \(([\d, ]+)\)', text)
        if m:
            for i in (int(x) for x in m.group(2).split(',')):
                if i in cl:
                    cl[i] = (cl[i][0], int(m.group(1)))
    return tpl, cl


def first_level(ql, mn):
    return max(mn or 0, (ql or 0) - 2)


(ta, ca), (tb, cb) = replay(old), replay(new)
ids = sorted(set(ta) | set(tb) | set(ca) | set(cb))
chg = []
for i in ids:
    qa, qb = ta.get(i), tb.get(i)
    ma, mb = ca.get(i), cb.get(i)
    if (qa, ma) != (qb, mb):
        chg.append((i, qa, ma, qb, mb))
print(f'# Quest level changes {old} -> {new}: {len(chg)} of {len(ids)} quests with a row differ (tpl rows {len(ta)} -> {len(tb)}, classic rows {len(ca)} -> {len(cb)})')
print('# id: quest level (template) -> ; (quest level, min level) in quest_classic_level ; first bot level that may take it, before -> after')
crossing = 0
for i, qa, ma, qb, mb in chg:
    fa = first_level(*(ma or (qa, 0)))
    fb = first_level(*(mb or (qb, 0)))
    mark = 'CROSSES' if (ma is None) != (mb is None) or fa != fb else 'same gate'
    crossing += mark == 'CROSSES'
    print(f'{i}: tpl {qa} -> {qb}; classic {ma} -> {mb}; first bot level {fa if ma or qa else "no row"} -> {fb}; {mark}')
print(f'# gate moved for {crossing} quests')
