-- Training dummy of the bots (Bot.AI.Dummy.*, chat verb "dummy"): entry 9900001, a copy of the Theramore Combat Dummy (4952, ScriptName
-- npc_training_dummy: damage is zeroed, so it never dies, and it ends combat after 5 s without damage), hostile to everyone (faction 14).
-- The bot summons it on demand at the bot's level; it is never spawned in the world. Copies every row of 4952 so the column list of this
-- fork's schema does not matter. Safe to run again.
DROP TEMPORARY TABLE IF EXISTS `tmp_bot_dummy`;

CREATE TEMPORARY TABLE `tmp_bot_dummy` SELECT * FROM `creature_template` WHERE `entry` = 4952;
UPDATE `tmp_bot_dummy` SET `entry` = 9900001, `name` = 'Bot Training Dummy', `faction` = 14;
DELETE FROM `creature_template` WHERE `entry` = 9900001;
INSERT INTO `creature_template` SELECT * FROM `tmp_bot_dummy`;
DROP TEMPORARY TABLE `tmp_bot_dummy`;

CREATE TEMPORARY TABLE `tmp_bot_dummy` SELECT * FROM `creature_template_model` WHERE `CreatureID` = 4952;
UPDATE `tmp_bot_dummy` SET `CreatureID` = 9900001;
DELETE FROM `creature_template_model` WHERE `CreatureID` = 9900001;
INSERT INTO `creature_template_model` SELECT * FROM `tmp_bot_dummy`;
DROP TEMPORARY TABLE `tmp_bot_dummy`;

CREATE TEMPORARY TABLE `tmp_bot_dummy` SELECT * FROM `creature_template_difficulty` WHERE `Entry` = 4952;
UPDATE `tmp_bot_dummy` SET `Entry` = 9900001;
DELETE FROM `creature_template_difficulty` WHERE `Entry` = 9900001;
INSERT INTO `creature_template_difficulty` SELECT * FROM `tmp_bot_dummy`;
DROP TEMPORARY TABLE `tmp_bot_dummy`;

-- level range (the bot sets the real level at summon)
REPLACE INTO `creature_classic_level` (`entry`, `level_min`, `level_max`) VALUES (9900001, 1, 1);
