-- Re-run after the sniff files (2026_10_01_03/04, 2026_10_02_00/06, 2026_10_03_00) were regenerated: they rewrite rows this file adjusts (last: 2026-10-05, sniff of 2026-10-04 20:05).
-- Classic 1.60 (WoW Forever): fishing loot of Zephras Isle (zone 16593) = the Durotar table (zone 14). The official beta sniff (70170)
-- shows the same two main catches there (Raw Brilliant Smallfish 12, Raw Longjaw Mud Snapper 7 of 19). Without loot the catch opened
-- no loot window and the bobber stayed. Safe to run again.
DELETE FROM `fishing_loot_template` WHERE `Entry` = 16593;
INSERT INTO `fishing_loot_template` (`Entry`, `ItemType`, `Item`, `Chance`, `QuestRequired`, `LootMode`, `GroupId`, `MinCount`, `MaxCount`, `Comment`)
SELECT 16593, `ItemType`, `Item`, `Chance`, `QuestRequired`, `LootMode`, `GroupId`, `MinCount`, `MaxCount`, 'Zephras Isle (copy of Durotar)'
FROM `fishing_loot_template` WHERE `Entry` = 14;
