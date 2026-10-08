-- Classic 1.60 (WoW Forever): creatures of the new companion pets (BattlePetSpecies summon spells, effect 28). The client data
-- (Creature.db2) has name, type and model; the rest is copied from the vanilla companion Bombay (7385). Without them the summon
-- spell cast but nothing appeared.
DELETE FROM `creature_template` WHERE `entry` IN (260580, 261680, 263559, 265797, 267842, 268840, 271368, 271636, 271867, 271870, 271874, 271875, 271876, 271877, 271878, 271888, 271889, 271890, 271891, 271892, 271893, 271913, 271915, 271972, 271979, 271980, 271993, 272328, 272329, 272330, 273391, 273956, 274412, 274947, 276331);
DELETE FROM `creature_template_model` WHERE `CreatureID` IN (260580, 261680, 263559, 265797, 267842, 268840, 271368, 271636, 271867, 271870, 271874, 271875, 271876, 271877, 271878, 271888, 271889, 271890, 271891, 271892, 271893, 271913, 271915, 271972, 271979, 271980, 271993, 272328, 272329, 272330, 273391, 273956, 274412, 274947, 276331);
DELETE FROM `creature_template_difficulty` WHERE `Entry` IN (260580, 261680, 263559, 265797, 267842, 268840, 271368, 271636, 271867, 271870, 271874, 271875, 271876, 271877, 271878, 271888, 271889, 271890, 271891, 271892, 271893, 271913, 271915, 271972, 271979, 271980, 271993, 272328, 272329, 272330, 273391, 273956, 274412, 274947, 276331);
DROP TEMPORARY TABLE IF EXISTS `tmp_classic_pet`;
CREATE TEMPORARY TABLE `tmp_classic_pet` (`entry` INT UNSIGNED NOT NULL PRIMARY KEY, `name` VARCHAR(255) NOT NULL, `type` TINYINT UNSIGNED NOT NULL);
INSERT INTO `tmp_classic_pet` (`entry`, `name`, `type`) VALUES
(260580, 'Mini Uber Diablo', 12),
(261680, 'Snowy Wolf Pup', 12),
(263559, 'Child of By''zaali', 12),
(265797, 'Wild Chicken', 12),
(267842, 'Bitter Baitling', 12),
(268840, 'Spectral Bear Cub', 12),
(271368, 'Shadowgale Squirrel', 12),
(271636, 'Weevil K. Neevil', 12),
(271867, 'Pachimari', 12),
(271870, 'Galestrider Chick', 12),
(271874, 'Black Furbolg Pet', 12),
(271875, 'Brown Furbolg Pet', 12),
(271876, 'Gray Furbolg Pet', 12),
(271877, 'Tan Furbolg Pet', 12),
(271878, 'White Furbolg Pet', 12),
(271888, 'Purple Ent Pet', 12),
(271889, 'Red Ent Pet', 12),
(271890, 'Blue Ent Pet', 12),
(271891, 'Gray Ent Pet', 12),
(271892, 'Orange Ent Pet', 12),
(271893, 'Yellow Ent Pet', 12),
(271913, 'Plagued Cockroach', 12),
(271915, 'Jungle Boa', 12),
(271972, 'Brown Prairie Dog', 12),
(271979, 'Brown Ground Squirrel', 12),
(271980, 'Red Ground Squirrel', 12),
(271993, 'Brown Rabbit', 12),
(272328, 'Mini Diablo', 12),
(272329, 'Zergling', 12),
(272330, 'Panda Cub', 12),
(273391, 'Baby Crocolisk', 12),
(273956, 'Condor Hatchling', 12),
(274412, 'Minfernal', 12),
(274947, 'Minimule', 12),
(276331, 'Spawn of Grubthor', 12);
DROP TEMPORARY TABLE IF EXISTS `tmp_classic_pet_template`;
CREATE TEMPORARY TABLE `tmp_classic_pet_template` SELECT * FROM `creature_template` WHERE `entry` = 7385;
DROP TEMPORARY TABLE IF EXISTS `tmp_classic_pet_difficulty`;
CREATE TEMPORARY TABLE `tmp_classic_pet_difficulty` SELECT * FROM `creature_template_difficulty` WHERE `Entry` = 7385;
INSERT INTO `creature_template` (`entry`,`KillCredit1`,`KillCredit2`,`name`,`femaleName`,`subname`,`TitleAlt`,`IconName`,`RequiredExpansion`,`VignetteID`,`faction`,`npcflag`,`speed_walk`,`speed_run`,`scale`,`Classification`,`dmgschool`,`BaseAttackTime`,`RangeAttackTime`,`BaseVariance`,`RangeVariance`,`unit_class`,`unit_flags`,`unit_flags2`,`unit_flags3`,`family`,`trainer_class`,`type`,`VehicleId`,`AIName`,`MovementType`,`ExperienceModifier`,`RacialLeader`,`movementId`,`WidgetSetID`,`WidgetSetUnitConditionID`,`RegenHealth`,`CreatureImmunitiesId`,`flags_extra`,`ScriptName`,`StringId`,`VerifiedBuild`)
SELECT p.`entry`,0,0,p.`name`,t.`femaleName`,NULL,NULL,NULL,t.`RequiredExpansion`,0,t.`faction`,0,t.`speed_walk`,t.`speed_run`,1,0,0,t.`BaseAttackTime`,t.`RangeAttackTime`,1,1,t.`unit_class`,t.`unit_flags`,t.`unit_flags2`,t.`unit_flags3`,0,0,p.`type`,0,'',0,1,0,t.`movementId`,0,0,1,0,t.`flags_extra`,'',NULL,70205 FROM `tmp_classic_pet` p JOIN `tmp_classic_pet_template` t;
INSERT INTO `creature_template_difficulty` (`Entry`,`DifficultyID`,`LevelScalingDeltaMin`,`LevelScalingDeltaMax`,`ContentTuningID`,`HealthScalingExpansion`,`HealthModifier`,`ManaModifier`,`ArmorModifier`,`DamageModifier`,`CreatureDifficultyID`,`TypeFlags`,`TypeFlags2`,`TypeFlags3`,`LootID`,`PickPocketLootID`,`SkinLootID`,`GoldMin`,`GoldMax`,`StaticFlags1`,`StaticFlags2`,`StaticFlags3`,`StaticFlags4`,`StaticFlags5`,`StaticFlags6`,`StaticFlags7`,`StaticFlags8`,`VerifiedBuild`)
SELECT p.`entry`,0,0,0,d.`ContentTuningID`,d.`HealthScalingExpansion`,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,70205 FROM `tmp_classic_pet` p JOIN `tmp_classic_pet_difficulty` d;
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`) VALUES
(260580, 0, 142494, 1.0, 1, 70205),
(261680, 0, 142952, 1.0, 1, 70205),
(263559, 0, 143743, 1.0, 1, 70205),
(265797, 0, 6193, 1.0, 1, 70205),
(267842, 0, 145177, 1.0, 1, 70205),
(268840, 0, 145638, 1.0, 1, 70205),
(271368, 0, 146639, 1.0, 1, 70205),
(271636, 0, 117132, 1.0, 1, 70205),
(271867, 0, 146839, 1.0, 1, 70205),
(271870, 0, 144905, 1.0, 1, 70205),
(271874, 0, 146842, 1.0, 1, 70205),
(271875, 0, 146843, 1.0, 1, 70205),
(271876, 0, 146844, 1.0, 1, 70205),
(271877, 0, 146845, 1.0, 1, 70205),
(271878, 0, 146846, 1.0, 1, 70205),
(271888, 0, 146849, 1.0, 1, 70205),
(271889, 0, 146850, 1.0, 1, 70205),
(271890, 0, 146851, 1.0, 1, 70205),
(271891, 0, 146852, 1.0, 1, 70205),
(271892, 0, 146853, 1.0, 1, 70205),
(271893, 0, 146854, 1.0, 1, 70205),
(271913, 0, 146859, 1.0, 1, 70205),
(271915, 0, 2958, 1.0, 1, 70205),
(271972, 0, 1072, 1.0, 1, 70205),
(271979, 0, 146884, 1.0, 1, 70205),
(271980, 0, 146882, 1.0, 1, 70205),
(271993, 0, 4626, 1.0, 1, 70205),
(272328, 0, 146956, 1.0, 1, 70205),
(272329, 0, 146954, 1.0, 1, 70205),
(272330, 0, 146955, 1.0, 1, 70205),
(273391, 0, 147381, 1.0, 1, 70205),
(273956, 0, 3248, 1.0, 1, 70205),
(274412, 0, 147738, 1.0, 1, 70205),
(274947, 0, 147969, 1.0, 1, 70205),
(276331, 0, 12336, 1.0, 1, 70205);
DROP TEMPORARY TABLE `tmp_classic_pet`;
DROP TEMPORARY TABLE `tmp_classic_pet_template`;
DROP TEMPORARY TABLE `tmp_classic_pet_difficulty`;
-- model info for the new pet models (without it the server refuses the model and the pet is invisible); Bombay values
DELETE FROM `creature_model_info` WHERE `DisplayID` IN (117132,142494,142952,143743,145177,145638,146639,146839,146842,146843,146844,146845,146846,146849,146850,146851,146852,146853,146854,146859,146882,146884,146954,146955,146956,147381,147738,147969);
INSERT INTO `creature_model_info` (`DisplayID`, `BoundingRadius`, `CombatReach`, `DisplayID_Other_Gender`, `VerifiedBuild`) VALUES
(117132, 0.138685, 0.5, 0, 70205),
(142494, 0.138685, 0.5, 0, 70205),
(142952, 0.138685, 0.5, 0, 70205),
(143743, 0.138685, 0.5, 0, 70205),
(145177, 0.138685, 0.5, 0, 70205),
(145638, 0.138685, 0.5, 0, 70205),
(146639, 0.138685, 0.5, 0, 70205),
(146839, 0.138685, 0.5, 0, 70205),
(146842, 0.138685, 0.5, 0, 70205),
(146843, 0.138685, 0.5, 0, 70205),
(146844, 0.138685, 0.5, 0, 70205),
(146845, 0.138685, 0.5, 0, 70205),
(146846, 0.138685, 0.5, 0, 70205),
(146849, 0.138685, 0.5, 0, 70205),
(146850, 0.138685, 0.5, 0, 70205),
(146851, 0.138685, 0.5, 0, 70205),
(146852, 0.138685, 0.5, 0, 70205),
(146853, 0.138685, 0.5, 0, 70205),
(146854, 0.138685, 0.5, 0, 70205),
(146859, 0.138685, 0.5, 0, 70205),
(146882, 0.138685, 0.5, 0, 70205),
(146884, 0.138685, 0.5, 0, 70205),
(146954, 0.138685, 0.5, 0, 70205),
(146955, 0.138685, 0.5, 0, 70205),
(146956, 0.138685, 0.5, 0, 70205),
(147381, 0.138685, 0.5, 0, 70205),
(147738, 0.138685, 0.5, 0, 70205),
(147969, 0.138685, 0.5, 0, 70205)
;
