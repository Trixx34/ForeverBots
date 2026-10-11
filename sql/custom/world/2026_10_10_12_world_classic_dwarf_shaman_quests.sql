-- Classic 1.60: dwarf shaman start (Coldridge Valley, sniff 2026-10-10 of a dwarf shaman). Archaic Rune 98581 (Sten Stoutarm ->
-- Teo Hammerstorm 257446, the shaman trainer) and Call of Earth 94373 / 94374 / 94375 (reward: Stoneskin Totem via 8073) are shaman
-- quests; the quest query does not carry the class, the importer left them open to every class.
UPDATE `quest_template_addon` SET `AllowableClasses` = 64 WHERE `ID` IN (98581,94373,94374,94375,94472);
INSERT IGNORE INTO `quest_template_addon` (`ID`,`AllowableClasses`) VALUES (94472,64);

-- Call of Earth 94373: Frostmane Bear Pendant 264356 from Frostmane Troll Whelps (706, a vanilla creature, so the sniff loot import
-- skipped it): 2 pendants in the 3 kills made while the quest was in the log.
DELETE FROM `creature_loot_template` WHERE `Entry` = 706 AND `Item` = 264356;
INSERT INTO `creature_loot_template` (`Entry`,`ItemType`,`Item`,`Chance`,`QuestRequired`,`LootMode`,`GroupId`,`MinCount`,`MaxCount`,`Comment`) VALUES
(706,0,264356,66,1,1,0,1,1,'Frostmane Troll Whelp - Frostmane Bear Pendant (Call of Earth 94373, sniff 2 of 3 kills on the quest)');
UPDATE `creature_template_difficulty` SET `LootID` = 706 WHERE `Entry` = 706 AND `DifficultyID` = 0 AND `LootID` = 0;

-- Earth Sapta 94472 ("Speak to Teo Hammerstorm for another Earth Sapta"): repeatable from Teo while Call of Earth 94374 is in the
-- log and the Earth Sapta 6635 is gone. Not offered in the sniff (the sapta was not lost), giver from the quest text.
UPDATE `quest_template_addon` SET `SpecialFlags` = `SpecialFlags` | 1 WHERE `ID` = 94472;
DELETE FROM `creature_queststarter` WHERE `quest` = 94472;
INSERT INTO `creature_queststarter` (`id`,`quest`) VALUES (257446,94472);
DELETE FROM `creature_questender` WHERE `quest` = 94472;
INSERT INTO `creature_questender` (`id`,`quest`) VALUES (257446,94472);
DELETE FROM `conditions` WHERE `SourceTypeOrReferenceId` = 19 AND `SourceEntry` = 94472;
INSERT INTO `conditions` (`SourceTypeOrReferenceId`,`SourceGroup`,`SourceEntry`,`SourceId`,`ElseGroup`,`ConditionTypeOrReference`,`ConditionTarget`,`ConditionValue1`,`ConditionValue2`,`ConditionValue3`,`NegativeCondition`,`ErrorType`,`ErrorTextId`,`ScriptName`,`Comment`) VALUES
(19,0,94472,0,0,9,0,94374,0,0,0,0,0,'','Earth Sapta - Call of Earth 94374 in the log'),
(19,0,94472,0,0,2,0,6635,1,0,1,0,0,'','Earth Sapta - no Earth Sapta in the bags');
