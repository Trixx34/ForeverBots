-- Classic 1.60: Ruins of Lordaeron dungeon (map 2999): creatures the sniff never looted get a level-appropriate generic loot table,
-- copied from a vanilla creature of the same type and level (quest drops left out), gold included.
-- Captain Truman 265701 (humanoid, level 20) <- Shadowfang Darksoul 3855 (humanoid, 20-21, Shadowfang Keep)
-- Wailing Banshee 250650 (undead, level 16) <- Rot Hide Brute 1939 (undead, 16-17, Silverpine Forest)
DELETE FROM `creature_loot_template` WHERE `Entry` IN (265701,250650);
INSERT INTO `creature_loot_template` (`Entry`,`ItemType`,`Item`,`Chance`,`QuestRequired`,`LootMode`,`GroupId`,`MinCount`,`MaxCount`,`Comment`)
SELECT 265701, l.`ItemType`, l.`Item`, l.`Chance`, 0, l.`LootMode`, l.`GroupId`, l.`MinCount`, l.`MaxCount`, 'Captain Truman - generic loot (copy of Shadowfang Darksoul 3855)'
FROM `creature_loot_template` l JOIN `creature_template_difficulty` d ON d.`LootID` = l.`Entry` AND d.`Entry` = 3855 AND d.`DifficultyID` = 0
WHERE l.`QuestRequired` = 0;
INSERT INTO `creature_loot_template` (`Entry`,`ItemType`,`Item`,`Chance`,`QuestRequired`,`LootMode`,`GroupId`,`MinCount`,`MaxCount`,`Comment`)
SELECT 250650, l.`ItemType`, l.`Item`, l.`Chance`, 0, l.`LootMode`, l.`GroupId`, l.`MinCount`, l.`MaxCount`, 'Wailing Banshee - generic loot (copy of Rot Hide Brute 1939)'
FROM `creature_loot_template` l JOIN `creature_template_difficulty` d ON d.`LootID` = l.`Entry` AND d.`Entry` = 1939 AND d.`DifficultyID` = 0
WHERE l.`QuestRequired` = 0;
UPDATE `creature_template_difficulty` t
JOIN `creature_template_difficulty` s ON s.`Entry` = 3855 AND s.`DifficultyID` = 0
SET t.`LootID` = 265701, t.`GoldMin` = s.`GoldMin`, t.`GoldMax` = s.`GoldMax`
WHERE t.`Entry` = 265701 AND t.`DifficultyID` = 0;
UPDATE `creature_template_difficulty` t
JOIN `creature_template_difficulty` s ON s.`Entry` = 1939 AND s.`DifficultyID` = 0
SET t.`LootID` = 250650, t.`GoldMin` = s.`GoldMin`, t.`GoldMax` = s.`GoldMax`
WHERE t.`Entry` = 250650 AND t.`DifficultyID` = 0;
