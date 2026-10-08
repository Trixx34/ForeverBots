-- Re-run after the sniff files (2026_10_01_03/04, 2026_10_02_00/06, 2026_10_03_00) were regenerated: they rewrite rows this file adjusts (last: 2026-10-05, sniff of 2026-10-04 20:05).
-- Classic 1.60: level range of the Forever quests (ID >= 90000). The official servers quest data gives only the quest level
-- (quest_template_classic_level, from the sniffs); like vanilla (minimum mostly 2-4 below the quest level) they are offered from
-- quest level - 3, at least 1.
DELETE FROM `quest_classic_level` WHERE `ID` >= 90000;
INSERT INTO `quest_classic_level` (`ID`,`QuestLevel`,`MinLevel`,`MaxLevel`)
SELECT `ID`, `QuestLevel`, GREATEST(1, `QuestLevel` - 3), 0 FROM `quest_template_classic_level` WHERE `ID` >= 90000 AND `QuestLevel` > 0;
