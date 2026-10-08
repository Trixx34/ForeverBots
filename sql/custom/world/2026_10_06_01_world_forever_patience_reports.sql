-- Classic 1.60 (WoW Forever): Patience (99141, Executor Zygand, Brill). While the quest is in the log, Deathguard Dillinger, Gordo and
-- Deathguard Kristof offer "I need a report for Executor Zygand."; picking it casts a create loot spell that gives that report
-- (sniff of an Undead paladin, 2026-10-06: option 142723 -> spell 1319468 -> item 286200, 142730 -> 1319489 -> 286201,
-- 142737 -> 1319508 -> 286202). The quest wants all three reports. Safe to run again.

DELETE FROM `spell_loot_template` WHERE `Entry` IN (1319468, 1319489, 1319508);
INSERT INTO `spell_loot_template` (`Entry`, `ItemType`, `Item`, `Chance`, `QuestRequired`, `LootMode`, `GroupId`, `MinCount`, `MaxCount`, `Comment`) VALUES
(1319468, 0, 286200, 100, 0, 1, 0, 1, 1, 'Patience - report of Deathguard Dillinger'),
(1319489, 0, 286201, 100, 0, 1, 0, 1, 1, 'Patience - report of Deathguard Kristof'),
(1319508, 0, 286202, 100, 0, 1, 0, 1, 1, 'Patience - report of Gordo');

UPDATE `gossip_menu_option` SET `SpellID` = 1319468 WHERE `MenuID` = 44990 AND `GossipOptionID` = 142723;
UPDATE `gossip_menu_option` SET `SpellID` = 1319489 WHERE `MenuID` = 44991 AND `GossipOptionID` = 142730;
UPDATE `gossip_menu_option` SET `SpellID` = 1319508 WHERE `MenuID` = 44993 AND `GossipOptionID` = 142737;

-- only while Patience is in the log, and only until that report is in the bags
DELETE FROM `conditions` WHERE `SourceTypeOrReferenceId` = 15 AND ((`SourceGroup` = 44990 AND `SourceEntry` = 6) OR (`SourceGroup` = 44991 AND `SourceEntry` = 6) OR (`SourceGroup` = 44993 AND `SourceEntry` = 0));
INSERT INTO `conditions` (`SourceTypeOrReferenceId`, `SourceGroup`, `SourceEntry`, `SourceId`, `ElseGroup`, `ConditionTypeOrReference`, `ConditionTarget`, `ConditionValue1`, `ConditionValue2`, `ConditionValue3`, `NegativeCondition`, `Comment`) VALUES
(15, 44990, 6, 0, 0, 9, 0, 99141, 0, 0, 0, 'Patience - Dillinger report option: quest taken'),
(15, 44990, 6, 0, 0, 2, 0, 286200, 1, 0, 1, 'Patience - Dillinger report option: report not in bags'),
(15, 44991, 6, 0, 0, 9, 0, 99141, 0, 0, 0, 'Patience - Kristof report option: quest taken'),
(15, 44991, 6, 0, 0, 2, 0, 286201, 1, 0, 1, 'Patience - Kristof report option: report not in bags'),
(15, 44993, 0, 0, 0, 9, 0, 99141, 0, 0, 0, 'Patience - Gordo report option: quest taken'),
(15, 44993, 0, 0, 0, 2, 0, 286202, 1, 0, 1, 'Patience - Gordo report option: report not in bags');

-- Deathguard Dillinger uses his own menu (44990: the guard directions plus his quest options) like the official server, not
-- the generic Brill guard menu as well
DELETE FROM `creature_template_gossip` WHERE `CreatureID` = 1496 AND `MenuID` = 3356;
