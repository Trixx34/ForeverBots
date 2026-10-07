-- Missing class trainers found from TRAIN_NO_TRAINER events in a bot simulation run. Apply to forever_world. Idempotent.
-- 1. Dwarf (race 3) shaman: new Ironforge trainer 8200002 (cloned from Haromm 986, faction 55 Alliance-friendly, dwarf model of Brandur Ironhammer 5149,
--    trainer spell list of 1000986), creature guid 26300003 between Brandur Ironhammer and Beldruk Doombrow.
-- 2. Exile's Reach (races 95/96, map 2991): the trainer NPCs already exist and are spawned (251964 warrior, 251376 hunter, 251389 rogue, 251374 shaman,
--    251379 mage) but have no creature_trainer link, so the bot found no trainer. Link them to the standard class trainer lists and add the missing
--    "I'd like training!" gossip option (copied from the druid 251373 menu 41121). Druid 251373 is already linked (trainer 1233, 5 spells).
-- 3. Blood elf paladin (16275): not added, no race 10 in playercreateinfo and no Silvermoon (map 530) bots.
-- Needs a worldserver restart.
START TRANSACTION;

-- ---- 1. dwarf shaman ----
DELETE FROM `creature` WHERE `guid` = 26300003;
DELETE FROM `creature_trainer` WHERE `CreatureID` = 8200002;
DELETE FROM `creature_template_gossip` WHERE `CreatureID` = 8200002;
DELETE FROM `creature_template_model` WHERE `CreatureID` = 8200002;
DELETE FROM `creature_template_difficulty` WHERE `Entry` = 8200002;
DELETE FROM `creature_classic_level` WHERE `entry` = 8200002;
DELETE FROM `creature_template` WHERE `entry` = 8200002;

DROP TEMPORARY TABLE IF EXISTS tmp_st;
CREATE TEMPORARY TABLE tmp_st AS SELECT * FROM `creature_template` WHERE `entry` = 986;
UPDATE tmp_st SET `entry` = 8200002, `name` = 'Brunhild Stonetotem', `subname` = 'Shaman Trainer', `faction` = 55, `trainer_class` = 7, `VerifiedBuild` = 0;
INSERT INTO `creature_template` SELECT * FROM tmp_st;
DROP TEMPORARY TABLE tmp_st;

DROP TEMPORARY TABLE IF EXISTS tmp_std;
CREATE TEMPORARY TABLE tmp_std AS SELECT * FROM `creature_template_difficulty` WHERE `Entry` = 986 AND `DifficultyID` = 0;
UPDATE tmp_std SET `Entry` = 8200002, `CreatureDifficultyID` = 8200002, `VerifiedBuild` = 0;
INSERT INTO `creature_template_difficulty` SELECT * FROM tmp_std;
DROP TEMPORARY TABLE tmp_std;

INSERT INTO `creature_classic_level` (`entry`, `level_min`, `level_max`) SELECT 8200002, `level_min`, `level_max` FROM `creature_classic_level` WHERE `entry` = 986;
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`)
  SELECT 8200002, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, 0 FROM `creature_template_model` WHERE `CreatureID` = 5149 AND `Idx` = 0;
INSERT INTO `creature_template_gossip` (`CreatureID`, `MenuID`, `VerifiedBuild`) VALUES (8200002, 4652, 0);
INSERT INTO `creature_trainer` (`CreatureID`, `TrainerID`, `MenuID`, `OptionID`) VALUES (8200002, 1000986, 4652, 0);

DROP TEMPORARY TABLE IF EXISTS tmp_sc;
CREATE TEMPORARY TABLE tmp_sc AS SELECT * FROM `creature` WHERE `guid` = 20001781;
UPDATE tmp_sc SET `guid` = 26300003, `id` = 8200002, `equipment_id` = 0, `position_x` = -4597.25, `position_y` = -902.08, `position_z` = 502.85, `orientation` = 4.0, `spawntimesecs` = 300, `VerifiedBuild` = 0;
INSERT INTO `creature` SELECT * FROM tmp_sc;
DROP TEMPORARY TABLE tmp_sc;

-- ---- 2. Exile's Reach trainer links ----
DELETE FROM `creature_trainer` WHERE `CreatureID` IN (251964, 251376, 251389, 251374, 251379);
INSERT INTO `creature_trainer` (`CreatureID`, `TrainerID`, `MenuID`, `OptionID`) VALUES
  (251964, 1000911, 41126, 0),      -- warrior
  (251376, 1005515, 41123, 0),      -- hunter
  (251389, 1000915, 41124, 0),      -- rogue
  (251374, 1000986, 41125, 0),      -- shaman
  (251379, 1000198, 92251379, 0);   -- mage (menu already has its option)

DELETE FROM `gossip_menu_option` WHERE `MenuID` IN (41123, 41124, 41125, 41126) AND `OptionID` = 0;
INSERT INTO `gossip_menu_option` (`MenuID`, `GossipOptionID`, `OptionID`, `OptionNpc`, `OptionText`, `OptionBroadcastTextID`, `Language`, `Flags`, `ActionMenuID`, `ActionPoiID`, `BoxCoded`, `BoxMoney`, `BoxText`, `BoxBroadcastTextID`, `VerifiedBuild`)
  SELECT DISTINCT m.`MenuID`, 136805 + m.`MenuID`, 0, 3, 'I''d like training!', 0, 0, 0, 0, 0, 0, 0, NULL, 0, 0
  FROM `gossip_menu` m WHERE m.`MenuID` IN (41123, 41124, 41125, 41126);
COMMIT;
