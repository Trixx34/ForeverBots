-- Death recap actors move from 9100000-9102000 to 8100000-8102000: the Classic clients creature GUID holds only 23 bits of
-- entry, so actors above 8388607 got a GUID entry that didnt match their object and failed the clients create validation
-- ("Failed to validate JamCliObjCreate", disconnect).
-- Safe to run again: an actor already in 8100000-8102000 is replaced only when its 9100000-9102000 copy is back.
DELETE a FROM `creature_template` a JOIN `creature_template` b ON b.`entry` = a.`entry` + 1000000 WHERE a.`entry` BETWEEN 8100000 AND 8102000;
DELETE a FROM `creature_template_model` a JOIN `creature_template_model` b ON b.`CreatureID` = a.`CreatureID` + 1000000 AND b.`Idx` = a.`Idx` WHERE a.`CreatureID` BETWEEN 8100000 AND 8102000;
DELETE a FROM `creature_template_difficulty` a JOIN `creature_template_difficulty` b ON b.`Entry` = a.`Entry` + 1000000 AND b.`DifficultyID` = a.`DifficultyID` WHERE a.`Entry` BETWEEN 8100000 AND 8102000;
UPDATE `creature_template` SET `entry` = `entry` - 1000000 WHERE `entry` BETWEEN 9100000 AND 9102000;
UPDATE `creature_template_model` SET `CreatureID` = `CreatureID` - 1000000 WHERE `CreatureID` BETWEEN 9100000 AND 9102000;
UPDATE `creature_template_difficulty` SET `Entry` = `Entry` - 1000000 WHERE `Entry` BETWEEN 9100000 AND 9102000;
