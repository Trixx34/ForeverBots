-- WoW Classic 1.60.1.70009 ("WoW Forever"): enable the Skyborne races
-- ChrRaces 95 = High Order Skyborne (Alliance), 96 = Windshaper Skyborne (Horde); starting zone Zephras Isle (map 2991, area 16593)
-- Class combinations from the clients CharBaseInfo.db2:
--   95: Warrior, Hunter, Rogue, Mage, Druid
--   96: Warrior, Hunter, Rogue, Shaman, Druid

DELETE FROM `race_unlock_requirement` WHERE `raceID` IN (95, 96);
INSERT INTO `race_unlock_requirement` (`raceID`, `expansion`, `achievementId`) VALUES
(95, 0, 0),
(96, 0, 0);

DELETE FROM `class_expansion_requirement` WHERE `RaceID` IN (95, 96);
INSERT INTO `class_expansion_requirement` (`ClassID`, `RaceID`, `ActiveExpansionLevel`, `AccountExpansionLevel`) VALUES
(1, 95, 0, 0), (3, 95, 0, 0), (4, 95, 0, 0), (8, 95, 0, 0), (11, 95, 0, 0),
(1, 96, 0, 0), (3, 96, 0, 0), (4, 96, 0, 0), (7, 96, 0, 0), (11, 96, 0, 0);

-- Start position: Zephras Isle settlement (client CollectableSourceVendorSparse vendor position, area 16593)
DELETE FROM `playercreateinfo` WHERE `race` IN (95, 96);
INSERT INTO `playercreateinfo` (`race`, `class`, `map`, `position_x`, `position_y`, `position_z`, `orientation`) VALUES
(95, 1, 2991, 2148.17, 711.715, 662.367, 0),
(95, 3, 2991, 2148.17, 711.715, 662.367, 0),
(95, 4, 2991, 2148.17, 711.715, 662.367, 0),
(95, 8, 2991, 2148.17, 711.715, 662.367, 0),
(95, 11, 2991, 2148.17, 711.715, 662.367, 0),
(96, 1, 2991, 2148.17, 711.715, 662.367, 0),
(96, 3, 2991, 2148.17, 711.715, 662.367, 0),
(96, 4, 2991, 2148.17, 711.715, 662.367, 0),
(96, 7, 2991, 2148.17, 711.715, 662.367, 0),
(96, 11, 2991, 2148.17, 711.715, 662.367, 0);

-- Action bars: copied from existing races with the same class (Human, Night Elf for Druid, Orc for Shaman)
DELETE FROM `playercreateinfo_action` WHERE `race` IN (95, 96);
INSERT INTO `playercreateinfo_action` (`race`, `class`, `button`, `action`, `type`)
SELECT 95, `class`, `button`, `action`, `type` FROM `playercreateinfo_action` WHERE `race` = 1 AND `class` IN (1, 3, 4, 8);
INSERT INTO `playercreateinfo_action` (`race`, `class`, `button`, `action`, `type`)
SELECT 95, `class`, `button`, `action`, `type` FROM `playercreateinfo_action` WHERE `race` = 4 AND `class` = 11;
INSERT INTO `playercreateinfo_action` (`race`, `class`, `button`, `action`, `type`)
SELECT 96, `class`, `button`, `action`, `type` FROM `playercreateinfo_action` WHERE `race` = 2 AND `class` IN (1, 3, 4, 7);
INSERT INTO `playercreateinfo_action` (`race`, `class`, `button`, `action`, `type`)
SELECT 96, `class`, `button`, `action`, `type` FROM `playercreateinfo_action` WHERE `race` = 4 AND `class` = 11;

-- Base stat modifiers (required: worldserver aborts without level 1 stats); placeholder = Blood Elf values (high elven descent)
DELETE FROM `player_racestats` WHERE `race` IN (95, 96);
INSERT INTO `player_racestats` (`race`, `str`, `agi`, `sta`, `inte`, `spi`) VALUES
(95, -3, 1, 0, 2, 0),
(96, -3, 1, 0, 2, 0);
