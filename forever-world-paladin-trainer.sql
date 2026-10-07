-- Horde (Undead) paladin class trainer, missing from the TDB world baseline: bots of race 5 (Forsaken) paladins got TRAIN_NO_TRAINER
-- 'no_friendly_trainer_for_race_in_world'. Cloned from Father Lazarus (4608, Undercity priest trainer: faction 68 Undead, same flags/model/level
-- style) with the trainer spell list of the Alliance paladin trainer Brother Wilhelm (trainer 1000927, 136 spells) and his mace/shield equipment.
-- Entry/trainer id 8200001, creature guids 26300001 (Deathknell church, next to the other class trainers) and 26300002 (Undercity, between the warrior and
-- priest trainers). Apply to the forever_world database. Idempotent. NOT applied on any main DB by the agent: the owner applies it. Needs a worldserver restart.
START TRANSACTION;

DELETE FROM `creature` WHERE `guid` IN (26300001, 26300002);
DELETE FROM `creature_trainer` WHERE `CreatureID` = 8200001;
DELETE FROM `trainer_spell` WHERE `TrainerId` = 8200001;
DELETE FROM `trainer` WHERE `Id` = 8200001;
DELETE FROM `creature_template_gossip` WHERE `CreatureID` = 8200001;
DELETE FROM `creature_equip_template` WHERE `CreatureID` = 8200001;
DELETE FROM `creature_template_model` WHERE `CreatureID` = 8200001;
DELETE FROM `creature_template_difficulty` WHERE `Entry` = 8200001;
DELETE FROM `creature_classic_level` WHERE `entry` = 8200001;
DELETE FROM `creature_template` WHERE `entry` = 8200001;

DROP TEMPORARY TABLE IF EXISTS tmp_pt;
CREATE TEMPORARY TABLE tmp_pt AS SELECT * FROM `creature_template` WHERE `entry` = 4608;
UPDATE tmp_pt SET `entry` = 8200001, `name` = 'Mortimer Graves', `subname` = 'Paladin Trainer', `trainer_class` = 2, `VerifiedBuild` = 0;
INSERT INTO `creature_template` SELECT * FROM tmp_pt;
DROP TEMPORARY TABLE tmp_pt;

DROP TEMPORARY TABLE IF EXISTS tmp_ptd;
CREATE TEMPORARY TABLE tmp_ptd AS SELECT * FROM `creature_template_difficulty` WHERE `Entry` = 4608 AND `DifficultyID` = 0;
UPDATE tmp_ptd SET `Entry` = 8200001, `CreatureDifficultyID` = 8200001, `VerifiedBuild` = 0;
INSERT INTO `creature_template_difficulty` SELECT * FROM tmp_ptd;
DROP TEMPORARY TABLE tmp_ptd;

INSERT INTO `creature_classic_level` (`entry`, `level_min`, `level_max`) VALUES (8200001, 50, 50);
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`) VALUES (8200001, 0, 2618, 1, 1, 0);
INSERT INTO `creature_equip_template` (`CreatureID`, `ID`, `ItemID1`, `AppearanceModID1`, `ItemVisual1`, `ItemID2`, `AppearanceModID2`, `ItemVisual2`, `ItemID3`, `AppearanceModID3`, `ItemVisual3`, `VerifiedBuild`)
  VALUES (8200001, 1, 2182, 0, 0, 1984, 0, 0, 0, 0, 0, 0);

-- same gossip menu as the other paladin trainers (4664), trainer link
INSERT INTO `creature_template_gossip` (`CreatureID`, `MenuID`, `VerifiedBuild`) VALUES (8200001, 4664, 0);
INSERT INTO `trainer` (`Id`, `Type`, `Greeting`, `VerifiedBuild`) VALUES (8200001, 0, 'Hello, paladin!  Ready for some training?', 0);
INSERT INTO `creature_trainer` (`CreatureID`, `TrainerID`, `MenuID`, `OptionID`) VALUES (8200001, 8200001, 4664, 0);
INSERT INTO `trainer_spell` (`TrainerId`, `SpellId`, `MoneyCost`, `ReqSkillLine`, `ReqSkillRank`, `ReqAbility1`, `ReqAbility2`, `ReqAbility3`, `ReqLevel`, `VerifiedBuild`)
  SELECT 8200001, `SpellId`, `MoneyCost`, `ReqSkillLine`, `ReqSkillRank`, `ReqAbility1`, `ReqAbility2`, `ReqAbility3`, `ReqLevel`, 0 FROM `trainer_spell` WHERE `TrainerId` = 1000927;

DROP TEMPORARY TABLE IF EXISTS tmp_pc;
CREATE TEMPORARY TABLE tmp_pc AS SELECT * FROM `creature` WHERE `guid` = 20031865;
UPDATE tmp_pc SET `guid` = 26300001, `id` = 8200001, `equipment_id` = 1, `position_x` = 1843.0, `position_y` = 1627.0, `position_z` = 97.0169, `orientation` = 3.6, `spawntimesecs` = 300, `VerifiedBuild` = 0;
INSERT INTO `creature` SELECT * FROM tmp_pc;
UPDATE tmp_pc SET `guid` = 26300002, `position_x` = 1763.5, `position_y` = 411.0, `position_z` = -57.1143, `orientation` = 3.9;
INSERT INTO `creature` SELECT * FROM tmp_pc;
DROP TEMPORARY TABLE tmp_pc;
COMMIT;
