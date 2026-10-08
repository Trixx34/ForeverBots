-- Classic 1.60 (WoW Forever): Skyborne character start from a sniff of all Skyborne race / class combinations on the official beta
-- (first login of a new character each). 95 = High Order (Alliance), 96 = Windshapers (Horde). Safe to run again.

-- Starting action bars, as the official server sends them at the first login (both races alike, only the racial differs:
-- Read Ley Line 1259705 for 95, Skysight 1259686 for 96): Auto Attack, the two class spells, the racial, Walk on Air, water and food
-- on buttons 0-11; warriors also on the Battle Stance bar (72-83). Type 128 = item. 8 bytes per button in the packet (the client
-- has 360: 180-359 are a second set, not used by this server). Replaces the retail rows copied before the sniffs.
DELETE FROM `playercreateinfo_action` WHERE `race` IN (95, 96);
INSERT INTO `playercreateinfo_action` (`race`, `class`, `button`, `action`, `type`) VALUES
(95, 1, 0, 6603, 0),
(95, 1, 72, 6603, 0),
(95, 1, 73, 78, 0),
(95, 1, 81, 1259705, 0),
(95, 1, 82, 1259416, 0),
(95, 1, 83, 117, 128),
(95, 3, 0, 6603, 0),
(95, 3, 1, 2973, 0),
(95, 3, 2, 75, 0),
(95, 3, 8, 1259705, 0),
(95, 3, 9, 1259416, 0),
(95, 3, 10, 159, 128),
(95, 3, 11, 117, 128),
(95, 4, 0, 6603, 0),
(95, 4, 1, 1752, 0),
(95, 4, 2, 2098, 0),
(95, 4, 9, 1259705, 0),
(95, 4, 10, 1259416, 0),
(95, 4, 11, 4540, 128),
(95, 8, 0, 6603, 0),
(95, 8, 1, 133, 0),
(95, 8, 2, 168, 0),
(95, 8, 8, 1259705, 0),
(95, 8, 9, 1259416, 0),
(95, 8, 10, 159, 128),
(95, 8, 11, 117, 128),
(95, 11, 0, 6603, 0),
(95, 11, 1, 5176, 0),
(95, 11, 2, 5185, 0),
(95, 11, 8, 1259705, 0),
(95, 11, 9, 1259416, 0),
(95, 11, 10, 159, 128),
(95, 11, 11, 4536, 128),
(96, 1, 0, 6603, 0),
(96, 1, 72, 6603, 0),
(96, 1, 73, 78, 0),
(96, 1, 81, 1259686, 0),
(96, 1, 82, 1259416, 0),
(96, 1, 83, 117, 128),
(96, 3, 0, 6603, 0),
(96, 3, 1, 2973, 0),
(96, 3, 2, 75, 0),
(96, 3, 8, 1259686, 0),
(96, 3, 9, 1259416, 0),
(96, 3, 10, 159, 128),
(96, 3, 11, 117, 128),
(96, 4, 0, 6603, 0),
(96, 4, 1, 1752, 0),
(96, 4, 2, 2098, 0),
(96, 4, 9, 1259686, 0),
(96, 4, 10, 1259416, 0),
(96, 4, 11, 4540, 128),
(96, 7, 0, 6603, 0),
(96, 7, 1, 403, 0),
(96, 7, 2, 331, 0),
(96, 7, 8, 1259686, 0),
(96, 7, 9, 1259416, 0),
(96, 7, 10, 159, 128),
(96, 7, 11, 117, 128),
(96, 11, 0, 6603, 0),
(96, 11, 1, 5176, 0),
(96, 11, 2, 5185, 0),
(96, 11, 8, 1259686, 0),
(96, 11, 9, 1259416, 0),
(96, 11, 10, 159, 128),
(96, 11, 11, 4536, 128);

-- Racials: the official server gives Skysight only to the Windshapers (96) and Read Ley Line only to the High Order (95); the
-- rows made before the sniffs gave Skysight to both races. Race mask bits: 95 = 0x100000000, 96 = 0x200000000.
DELETE FROM `playercreateinfo_spell_custom` WHERE `Spell` IN (1259686, 1259705);
INSERT INTO `playercreateinfo_spell_custom` (`racemask`, `classmask`, `Spell`, `Note`) VALUES
(8589934592, 0, 1259686, 'Skyborne Windshapers (96) - Skysight'),
(4294967296, 0, 1259705, 'Skyborne High Order (95) - Read Ley Line');

-- Zephras Isle start quests: at the first login the official server offers only Coming of Age (Ailee Farheart); the others
-- were open from level 1 here. Level gated (grey "!" at level 1, open at the level 2 ding): The Anchors of Zephras, Falling With
-- Style, The Gift of Skysight. Opened by a quest (no marker until it is turned in): Infestation Investigation after Coming of
-- Age, Harvesting Windstones after Infestation Investigation, Aggressive Encroachment after Aetheen of the Gales.
UPDATE `quest_classic_level` SET `MinLevel` = 2 WHERE `ID` IN (94414, 92474, 92598);
UPDATE `quest_template_addon` SET `PrevQuestID` = 92460 WHERE `ID` = 92462;
UPDATE `quest_template_addon` SET `PrevQuestID` = 92462 WHERE `ID` = 93552;
UPDATE `quest_template_addon` SET `PrevQuestID` = 92471 WHERE `ID` = 92473;
