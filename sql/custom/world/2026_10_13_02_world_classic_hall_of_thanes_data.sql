-- Classic 1.60 (WoW Forever): templates, quests and NPC data of the dungeon Hall of Thanes (map 3065) from ymir sniffs of the official beta
-- (classic_re/sniff/sniff_import_dungeon.py): what the database lacked before (snapshot ids_pre_hall_of_thanes), the Forever creatures data
-- replaced. Spawns: sniff_import_instance.py; scripts: the dungeon generator. Safe to run again.

-- creatures
DELETE FROM `creature_template` WHERE `entry` IN (261306,261311,261316,261319,263389,263390,263396,263397,263438,263466,263467,263470,263980,264943,265002,265003);
INSERT INTO `creature_template` (`entry`,`KillCredit1`,`KillCredit2`,`name`,`femaleName`,`subname`,`TitleAlt`,`IconName`,`RequiredExpansion`,`VignetteID`,`faction`,`npcflag`,`speed_walk`,`speed_run`,`scale`,`Classification`,`dmgschool`,`BaseAttackTime`,`RangeAttackTime`,`BaseVariance`,`RangeVariance`,`unit_class`,`unit_flags`,`unit_flags2`,`unit_flags3`,`family`,`trainer_class`,`type`,`VehicleId`,`AIName`,`MovementType`,`ExperienceModifier`,`RacialLeader`,`movementId`,`WidgetSetID`,`WidgetSetUnitConditionID`,`RegenHealth`,`CreatureImmunitiesId`,`flags_extra`,`ScriptName`,`VerifiedBuild`) VALUES
(261306,0,0,'Faldrim Anvilmar',NULL,NULL,NULL,NULL,0,0,16,0,1.0,1.142857,1.0,1,0,2000,2000,1,1,2,0,2048,0,0,0,6,0,'',0,1,0,0,0,0,1,0,0,'',70124),
(261311,0,0,'Plunder',NULL,NULL,NULL,NULL,0,0,16,0,1.0,1.142857,1.0,1,0,2000,2000,1,1,1,0,2048,0,0,0,4,0,'',0,1,0,0,0,0,1,0,0,'',70124),
(261316,0,0,'Magmatus',NULL,NULL,NULL,NULL,0,0,16,0,1.0,1.142857,1.0,1,0,2000,2000,1,1,1,0,2048,0,0,0,4,0,'',0,1,0,0,0,0,1,0,0,'',70124),
(261319,0,0,'Durgen Dirgehammer',NULL,NULL,NULL,NULL,0,0,54,0,1.0,1.142857,1.0,1,0,2000,2000,1,1,1,0,2048,0,0,0,7,0,'',0,1,0,0,0,0,1,0,0,'',70124),
(263389,0,0,'Enraged Apparition',NULL,NULL,NULL,NULL,0,0,16,0,1.0,1.142857,1.02,1,0,2000,2000,1,1,1,0,2048,0,0,0,6,0,'',0,1,0,0,0,0,1,0,0,'',70124),
(263390,0,0,'Tormented Soul',NULL,NULL,NULL,NULL,0,0,16,0,1.0,1.142857,1.05,1,0,2000,2000,1,1,8,0,2048,0,0,0,6,0,'',0,1,0,0,0,0,1,0,0,'',70124),
(263396,0,0,'Dark Iron Looter',NULL,NULL,NULL,NULL,0,0,54,0,1.0,1.142857,0.93,1,0,2000,2000,1,1,4,0,2048,0,0,0,7,0,'',0,1,0,0,0,0,1,0,0,'',70124),
(263397,0,0,'Dark Iron Engineer',NULL,NULL,NULL,NULL,0,0,54,0,1.0,1.142857,1.08,1,0,2000,2000,1,1,1,0,2048,0,0,0,7,0,'',0,1,0,0,0,0,1,0,0,'',70124),
(263438,0,0,'Dark Iron Summoner',NULL,NULL,NULL,NULL,0,0,54,0,1.0,1.142857,1.05,1,0,2000,2000,1,1,8,0,2048,0,0,0,7,0,'',0,1,0,0,0,0,1,0,0,'',70124),
(263466,0,0,'Lesser Stone Golem',NULL,NULL,NULL,NULL,0,0,54,0,1.0,1.142857,1.0,1,0,2000,2000,1,1,1,0,2048,0,0,0,4,0,'',0,1,0,0,0,0,1,0,0,'',70124),
(263467,0,0,'Fiery Assistant',NULL,NULL,NULL,NULL,0,0,54,0,1.0,1.142857,1.0,0,0,2000,2000,1,1,1,0,2048,0,0,0,4,0,'',0,1,0,0,0,0,1,0,0,'',70124),
(263470,0,0,'Bloodhound Runt',NULL,NULL,NULL,NULL,0,0,54,0,1.0,1.142857,1.0,0,0,2000,2000,1,1,1,0,2048,0,0,0,1,0,'',0,1,0,0,0,0,1,0,0,'',70124),
(263980,0,0,'Seaforium Blasting Powder',NULL,NULL,NULL,NULL,0,0,7,0,1.0,0.992063,1.0,0,0,2000,2000,1,1,1,0,2048,0,0,0,10,0,'',0,1,0,0,0,0,1,0,0,'',70124),
(264943,0,0,'Afadra Dunwall',NULL,'Lorekeeper of Ironforge',NULL,NULL,0,0,55,2,1.0,1.142857,1.0,0,0,2000,2000,1,1,1,0,2048,0,0,0,7,0,'',0,1,0,1693,0,0,1,0,0,'',70124),
(265002,0,0,'Ghostly Attendant',NULL,NULL,NULL,NULL,0,0,35,2,1.0,1.142857,1.06,1,0,2000,2000,1,1,8,0,2048,0,0,0,6,0,'',0,1,0,0,0,0,1,0,0,'',70124),
(265003,0,0,'Thom Filch',NULL,NULL,NULL,NULL,0,0,474,2,1.0,1.142857,1.0,0,0,2000,2000,1,1,1,0,2048,0,0,0,7,0,'',0,1,0,1693,0,0,1,0,0,'',70124);
DELETE FROM `creature_template_difficulty` WHERE `Entry` IN (261306,261311,261316,261319,263389,263390,263396,263397,263438,263466,263467,263470,263980,264943,265002,265003);
INSERT INTO `creature_template_difficulty` (`Entry`,`DifficultyID`,`LevelScalingDeltaMin`,`LevelScalingDeltaMax`,`ContentTuningID`,`HealthScalingExpansion`,`HealthModifier`,`ManaModifier`,`ArmorModifier`,`DamageModifier`,`CreatureDifficultyID`,`TypeFlags`,`TypeFlags2`,`TypeFlags3`,`LootID`,`PickPocketLootID`,`SkinLootID`,`GoldMin`,`GoldMax`,`VerifiedBuild`) VALUES
(261306,0,0,0,0,0,4.2,1.5,1,1,344537,0,128,0,0,0,0,0,0,70124),
(261311,0,0,0,0,0,6.0,1.0,1,1,344542,0,128,0,0,0,0,0,0,70124),
(261316,0,0,0,0,0,4.2,1.0,1,1,344547,0,128,0,0,0,0,0,0,70124),
(261319,0,0,0,0,0,4.5,1.0,1,1,344550,0,128,0,0,0,0,0,0,70124),
(263389,0,0,0,0,0,3.0,1.0,1,1,346696,0,0,0,0,0,0,0,0,70124),
(263390,0,0,0,0,0,3.0,1.0,1,1,346697,0,0,0,0,0,0,0,0,70124),
(263396,0,0,0,0,0,3.0,1.0,1,1,346703,0,0,0,0,0,0,0,0,70124),
(263397,0,0,0,0,0,3.0,1.0,1,1,346704,0,0,0,0,0,0,0,0,70124),
(263438,0,0,0,0,0,3.0,1.2,1,1,346745,0,0,0,0,0,0,0,0,70124),
(263466,0,0,0,0,0,4.0,1.0,1,1,346773,0,0,0,0,0,0,0,0,70124),
(263467,0,0,0,0,0,0.8,1.0,1,1,346774,0,0,0,0,0,0,0,0,70124),
(263470,0,0,0,0,0,0.9,1.0,1,1,346777,0,0,0,0,0,0,0,0,70124),
(263980,0,0,0,0,0,0.1,1.0,1,1,347304,1610612736,67108864,0,0,0,0,0,0,70124),
(264943,0,0,0,0,0,1.0,1.0,1,1,348448,134217728,0,0,0,0,0,0,0,70124),
(265002,0,0,0,0,0,3.0,1.0,1,1,348507,0,0,0,0,0,0,0,0,70124),
(265003,0,0,0,0,0,1.0,1.0,1,1,348508,134217728,0,0,0,0,0,0,0,70124);
DELETE FROM `creature_template_model` WHERE `CreatureID` IN (261306,261311,261316,261319,263389,263390,263396,263397,263438,263466,263467,263470,263980,264943,265002,265003);
INSERT INTO `creature_template_model` (`CreatureID`,`Idx`,`CreatureDisplayID`,`DisplayScale`,`Probability`,`VerifiedBuild`) VALUES
(261306,0,142826,1.75,1.0,70124),
(261311,0,142840,1.5,1.0,70124),
(261316,0,1070,1.5,1.0,70124),
(261319,0,142837,1.5,1.0,70124),
(263389,0,143689,1.0,1.0,70124),
(263389,1,143694,1.0,1.0,70124),
(263389,2,143695,1.0,1.0,70124),
(263389,3,143696,1.0,1.0,70124),
(263390,0,143690,1.0,1.0,70124),
(263390,1,143692,1.0,1.0,70124),
(263390,2,143693,1.0,1.0,70124),
(263390,3,143691,1.0,1.0,70124),
(263396,0,3452,1.0,1.0,70124),
(263397,0,3954,1.0,1.0,70124),
(263438,0,143713,1.0,1.0,70124),
(263466,0,2693,0.9,1.0,70124),
(263467,0,1070,0.5,1.0,70124),
(263470,0,8180,0.6,1.0,70124),
(263980,0,142182,1.0,1.0,70124),
(264943,0,144322,1.0,100.0,70124),
(265002,0,144348,1.0,1.0,70124),
(265003,0,144349,1.0,100.0,70124);
INSERT IGNORE INTO `creature_model_info` (`DisplayID`,`BoundingRadius`,`CombatReach`,`DisplayID_Other_Gender`,`VerifiedBuild`) VALUES
(1070,0.389,1.5,0,70124),
(2693,0.389,1.5,0,70124),
(3452,0.389,1.5,0,70124),
(3954,0.389,1.5,0,70124),
(8180,0.389,1.5,0,70124),
(142182,0.389,1.5,0,70124),
(142826,0.389,1.5,0,70124),
(142837,0.389,1.5,0,70124),
(142840,0.389,1.5,0,70124),
(143689,0.389,1.5,0,70124),
(143690,0.389,1.5,0,70124),
(143691,0.389,1.5,0,70124),
(143692,0.389,1.5,0,70124),
(143693,0.389,1.5,0,70124),
(143694,0.389,1.5,0,70124),
(143695,0.389,1.5,0,70124),
(143696,0.389,1.5,0,70124),
(143713,0.389,1.5,0,70124),
(144322,0.389,1.5,0,70124),
(144348,0.389,1.5,0,70124),
(144349,0.389,1.5,0,70124);

-- creature equipment seen in the sniffs
INSERT IGNORE INTO `creature_equip_template` (`CreatureID`,`ID`,`ItemID1`,`AppearanceModID1`,`ItemVisual1`,`ItemID2`,`AppearanceModID2`,`ItemVisual2`,`ItemID3`,`AppearanceModID3`,`ItemVisual3`,`VerifiedBuild`) VALUES
(261306,1,5491,0,0,0,0,0,0,0,0,70124),
(261319,1,14875,0,0,0,0,0,0,0,0,70124),
(263389,1,3488,0,0,0,0,0,0,0,0,70124),
(263390,1,12421,0,0,0,0,0,0,0,0,70124),
(263396,1,4565,0,0,0,0,0,0,0,0,70124),
(263397,1,1903,0,0,0,0,0,0,0,0,70124),
(264943,1,13312,0,0,0,0,0,0,0,0,70124),
(265003,1,15247,0,0,0,0,0,0,0,0,70124);

-- creature levels seen in the sniffs (Forever creatures replaced, others only where missing)
DELETE FROM `creature_classic_level` WHERE `entry` IN (261306,261311,261316,261319,263389,263390,263396,263397,263438,263466,263470,263980,264943,265002,265003);
INSERT IGNORE INTO `creature_classic_level` (`entry`,`level_min`,`level_max`) VALUES
(4075,1,1),
(14881,1,1),
(261306,16,16),
(261311,16,16),
(261316,16,16),
(261319,16,16),
(263389,13,14),
(263390,13,14),
(263396,14,15),
(263397,14,15),
(263438,14,15),
(263466,14,15),
(263470,14,15),
(263980,13,13),
(264943,13,13),
(265002,13,13),
(265003,13,13);

-- permanent creature auras seen in the sniffs (full aura updates of at least half of the creatures units)
INSERT INTO `creature_template_addon` (`entry`,`auras`) VALUES (263389,'1264628') ON DUPLICATE KEY UPDATE `auras` = '1264628';
INSERT INTO `creature_template_addon` (`entry`,`auras`) VALUES (263390,'1264628') ON DUPLICATE KEY UPDATE `auras` = '1264628';
INSERT INTO `creature_template_addon` (`entry`,`auras`) VALUES (263396,'1264628') ON DUPLICATE KEY UPDATE `auras` = '1264628';
INSERT INTO `creature_template_addon` (`entry`,`auras`) VALUES (263397,'1264628') ON DUPLICATE KEY UPDATE `auras` = '1264628';
INSERT INTO `creature_template_addon` (`entry`,`auras`) VALUES (263438,'1264628') ON DUPLICATE KEY UPDATE `auras` = '1264628';
INSERT INTO `creature_template_addon` (`entry`,`auras`) VALUES (265002,'1264628') ON DUPLICATE KEY UPDATE `auras` = '1264628';

-- gameobjects
DELETE FROM `gameobject_template` WHERE `entry` IN (457387,642023,642024,642025,642026,642027,642857,642858,642859,645389,645390,645391,645392,645393,648667,648668,648669,648670,648671,648672,648673,648674,648675,648676,648677,648678,650183,650720,652080,652081,652082,652083,652084,652085,652086,652087,652088,652089,652090,652091,652092,652093,652094,652095,652227,652228,673473,673474,703593,703594,703595,703596,703597,703598,703599,703600,703601,703602,703603,703604,703605,703606,703607,703608,703609,703610,703611,703612,703613,703614,703615,703616,703617,703618,703619,703620,703621,703622,703623,703624,703625,703626,703627,703628,703629,703630,703631,703632,703633,703634,703635,703636,703637,703638,703639,703640,703641,703642,703643,703644,703645,703646,703647,703648);
INSERT INTO `gameobject_template` (`entry`,`type`,`displayId`,`name`,`IconName`,`castBarCaption`,`unk1`,`size`,`Data0`,`Data1`,`Data2`,`Data3`,`Data4`,`Data5`,`Data6`,`Data7`,`Data8`,`Data9`,`Data10`,`Data11`,`Data12`,`Data13`,`Data14`,`Data15`,`Data16`,`Data17`,`Data18`,`Data19`,`Data20`,`Data21`,`Data22`,`Data23`,`Data24`,`Data25`,`Data26`,`Data27`,`Data28`,`Data29`,`Data30`,`Data31`,`Data32`,`Data33`,`Data34`,`ContentTuningId`,`RequiredLevel`,`AIName`,`ScriptName`,`VerifiedBuild`) VALUES
(457387,3,88067,'Dwarven Heirloom','','','',0.35,93,141018,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4097,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2180,1,'','',70124),
(642023,0,401,'Portcullis','','','',0.72,1,0,3000,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(642024,0,401,'Portcullis','','','',0.72,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(642025,0,401,'Portcullis','','','',0.72,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(642026,0,401,'Portcullis','','','',0.72,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(642027,0,401,'Portcullis','','','',0.72,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(642857,0,401,'Portcullis','','','',0.72,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(642858,0,401,'Portcullis','','','',0.829999,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(642859,0,401,'Portcullis','','','',0.829999,0,0,3000,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(645389,0,2752,'Iron Door','','','',1.0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(645390,0,2752,'Iron Door','','','',1.0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(645391,0,2752,'Iron Door','','','',1.0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(645392,0,2752,'Iron Door','','','',1.0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(645393,0,2752,'Vault Door','','','',1.0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(648667,0,401,'Portcullis','','','',0.9,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(648668,0,401,'Portcullis','','','',0.9,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(648669,0,3075,'Vault Door','','','',1.5,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(648670,0,3075,'Vault Door','','','',1.5,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(648671,0,3075,'Vault Door','','','',1.5,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(648672,0,3075,'Vault Door','','','',1.5,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(648673,0,3075,'Vault Door','','','',1.5,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(648674,0,3075,'Vault Door','','','',1.5,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(648675,0,3075,'Vault Door','','','',1.5,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(648676,0,3075,'Vault Door','','','',1.5,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(648677,0,3075,'Vault Door','','','',1.5,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(648678,0,3075,'Vault Door','','','',1.5,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(650183,5,12419,'Faldrim Anvilmar, Thane of Ironforge','','','',2.0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(650720,23,5494,'Meeting Stone','','','',0.66,1,60,16919,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,7420,10,'','',70124),
(652080,5,3011,'Bael\'gan Morogh','','','',2.0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(652081,5,3011,'Aegrim Bronzebeard','','','',2.0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(652082,5,3011,'Furgus Algaz','','','',2.0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(652083,5,3011,'Modimus Anvilmar','','','',2.0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(652084,5,3011,'Hargran Thandol','','','',2.0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(652085,5,3011,'Eimear Bronzebeard','','','',2.0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(652086,5,3011,'Madoran Bronzebeard','','','',2.0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(652087,5,3011,'PlaqueBronze01','','','',2.0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(652088,5,3011,'PlaqueBronze01','','','',2.0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(652089,5,3011,'PlaqueBronze01','','','',2.0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(652090,5,3011,'PlaqueBronze01','','','',2.0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(652091,5,3011,'PlaqueBronze01','','','',2.0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(652092,5,3011,'PlaqueBronze01','','','',2.0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(652093,5,3011,'PlaqueBronze01','','','',2.0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(652094,5,3011,'PlaqueBronze01','','','',2.0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(652095,5,3011,'Muradin Bronzebeard','','','',2.0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(652227,3,125243,'Dwarven Heirloom','','','',0.2,93,141018,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4097,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2180,1,'','',70124),
(652228,3,125245,'Dwarven Heirloom','','','',0.85,93,141018,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4097,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2180,1,'','',70124),
(673473,1,295,'Ancient Switch','','','',1.0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(673474,2,129586,'Treaty of Understanding','','','',0.35,0,30571,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2180,1,'','',70124),
(703593,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703594,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703595,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703596,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703597,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703598,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703599,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703600,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703601,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703602,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703603,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703604,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703605,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703606,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703607,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703608,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703609,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703610,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703611,8,197,'Dwarven Brazier','','','',0.999998,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703612,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703613,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703614,8,197,'Dwarven Brazier','','','',0.999998,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703615,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703616,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703617,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703618,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703619,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703620,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703621,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703622,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703623,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703624,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703625,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703626,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703627,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703628,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703629,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703630,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703631,8,197,'Dwarven Brazier','','','',0.999998,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703632,8,197,'Dwarven Brazier','','','',0.999998,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703633,8,197,'Dwarven Brazier','','','',0.999998,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703634,8,197,'Dwarven Brazier','','','',0.999998,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703635,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703636,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703637,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703638,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703639,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703640,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703641,8,197,'Dwarven Brazier','','','',0.999817,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703642,8,197,'Dwarven Brazier','','','',0.999817,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703643,8,197,'Dwarven Brazier','','','',0.999817,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703644,8,197,'Dwarven Brazier','','','',0.999817,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703645,8,197,'Dwarven Brazier','','','',0.999817,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703646,8,197,'Dwarven Brazier','','','',0.999998,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703647,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124),
(703648,8,197,'Dwarven Brazier','','','',0.999999,4,5,2061,0,0,0,0,0,0,0,0,0,0,0,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'','',70124);

-- quest items of the new quest objects (chests drop them while the quest needs them)
DELETE FROM `gameobject_questitem` WHERE `GameObjectEntry` IN (457387,652227,652228);
INSERT INTO `gameobject_questitem` (`GameObjectEntry`,`Idx`,`ItemId`,`VerifiedBuild`) VALUES
(457387,0,274289,70124),
(652227,0,274289,70124),
(652228,0,274289,70124);
DELETE FROM `gameobject_loot_template` WHERE (`Entry`, `Item`) IN ((141018,274289));
INSERT INTO `gameobject_loot_template` (`Entry`,`ItemType`,`Item`,`Chance`,`QuestRequired`,`LootMode`,`GroupId`,`MinCount`,`MaxCount`,`Comment`) VALUES
(141018,0,274289,100,1,1,0,1,1,'Dwarven Heirloom (sniff, quest item)');

-- loot of the Forever chests opened in the sniffs that have their own loot id (chance = drops / openings of all its chests)
DELETE FROM `gameobject_loot_template` WHERE `Entry` IN (141018);
INSERT INTO `gameobject_loot_template` (`Entry`,`ItemType`,`Item`,`Chance`,`QuestRequired`,`LootMode`,`GroupId`,`MinCount`,`MaxCount`,`Comment`) VALUES
(141018,0,274289,100.0,1,1,0,1,1,'sniff: 8 of 8 openings (chests 457387 652227 652228)');

-- quests
DELETE FROM `quest_template` WHERE `ID` IN (96393,96394,96395,96403,98423);
INSERT INTO `quest_template` (`ID`,`QuestType`,`QuestPackageID`,`ContentTuningID`,`QuestSortID`,`QuestInfoID`,`SuggestedGroupNum`,`RewardNextQuest`,`RewardXPDifficulty`,`RewardXPMultiplier`,`RewardMoneyDifficulty`,`RewardMoneyMultiplier`,`RewardBonusMoney`,`RewardSpell`,`RewardHonor`,`RewardKillHonor`,`RewardFavor`,`RewardArtifactXPDifficulty`,`RewardArtifactXPMultiplier`,`RewardArtifactCategoryID`,`StartItem`,`Flags`,`FlagsEx`,`FlagsEx2`,`FlagsEx3`,`RewardItem1`,`RewardAmount1`,`ItemDrop1`,`ItemDropQuantity1`,`RewardItem2`,`RewardAmount2`,`ItemDrop2`,`ItemDropQuantity2`,`RewardItem3`,`RewardAmount3`,`ItemDrop3`,`ItemDropQuantity3`,`RewardItem4`,`RewardAmount4`,`ItemDrop4`,`ItemDropQuantity4`,`RewardChoiceItemID1`,`RewardChoiceItemQuantity1`,`RewardChoiceItemDisplayID1`,`RewardChoiceItemID2`,`RewardChoiceItemQuantity2`,`RewardChoiceItemDisplayID2`,`RewardChoiceItemID3`,`RewardChoiceItemQuantity3`,`RewardChoiceItemDisplayID3`,`RewardChoiceItemID4`,`RewardChoiceItemQuantity4`,`RewardChoiceItemDisplayID4`,`RewardChoiceItemID5`,`RewardChoiceItemQuantity5`,`RewardChoiceItemDisplayID5`,`RewardChoiceItemID6`,`RewardChoiceItemQuantity6`,`RewardChoiceItemDisplayID6`,`POIContinent`,`POIx`,`POIy`,`POIPriority`,`RewardTitle`,`RewardArenaPoints`,`RewardSkillLineID`,`RewardNumSkillUps`,`PortraitGiver`,`PortraitGiverMount`,`PortraitGiverModelSceneID`,`PortraitTurnIn`,`RewardFactionID1`,`RewardFactionValue1`,`RewardFactionOverride1`,`RewardFactionCapIn1`,`RewardFactionID2`,`RewardFactionValue2`,`RewardFactionOverride2`,`RewardFactionCapIn2`,`RewardFactionID3`,`RewardFactionValue3`,`RewardFactionOverride3`,`RewardFactionCapIn3`,`RewardFactionID4`,`RewardFactionValue4`,`RewardFactionOverride4`,`RewardFactionCapIn4`,`RewardFactionID5`,`RewardFactionValue5`,`RewardFactionOverride5`,`RewardFactionCapIn5`,`RewardFactionFlags`,`RewardCurrencyID1`,`RewardCurrencyQty1`,`RewardCurrencyID2`,`RewardCurrencyQty2`,`RewardCurrencyID3`,`RewardCurrencyQty3`,`RewardCurrencyID4`,`RewardCurrencyQty4`,`AcceptedSoundKitID`,`CompleteSoundKitID`,`AreaGroupID`,`TimeAllowed`,`AllowableRaces`,`Expansion`,`ManagedWorldStateID`,`QuestSessionBonus`,`ResetByScheduler`,`LogTitle`,`LogDescription`,`QuestDescription`,`AreaDescription`,`PortraitGiverText`,`PortraitGiverName`,`PortraitTurnInText`,`PortraitTurnInName`,`QuestCompletionLog`,`RewardDisplaySpell1`,`RewardDisplaySpell2`,`RewardDisplaySpell3`,`VerifiedBuild`) VALUES
(96393,2,9,0,16919,81,0,0,6,2.2,5,1.0,0,0,0,0.0,0,0,1.0,0,0,8,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,279894,1,0,279895,1,0,279896,1,0,0,0,0,0,0,0,0,0,0,0,0.0,0.0,0,0,0,0,0,0,0,0,0,47,5,0,7,54,5,0,7,0,0,0,7,0,0,0,7,0,0,0,7,3,0,0,0,0,0,0,0,0,890,878,0,0,18446744073709551615,-2,0,0,0,'Old Ironforge Incursion','Enter the Hall of Thanes beneath Old Ironforge and claim the Head of Durgen Dirgehammer.','Gather some allies and head down into the caverns beneath Old Ironforge. Enter the Hall of Thanes there and push to the lower vaults. \r\n\r\nMy Dark Iron is a bit rusty, but I can make out a name--one Durgen Dirgehammer. It\'s likely that he\'s the commander. Take his head and deliver it to King Magni Bronzebeard. \r\n\r\nOh, and when you do, tell him his old friend Farsen sends his regards!','','','','','','',0,0,0,70124),
(96394,2,10,0,16919,81,0,0,5,2.2,5,1.0,0,0,0,0.0,0,0,1.0,0,0,8,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,279897,1,0,280095,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0.0,0.0,0,0,0,0,0,0,0,0,0,47,5,0,7,0,0,0,7,0,0,0,7,0,0,0,7,0,0,0,7,3,0,0,0,0,0,0,0,0,890,878,0,0,6130900294268439629,-2,0,0,0,'The Restless Dead','Kill $1oa Enraged Apparitions, $2oa Tormented Souls and put the spirit of Anvilmar to rest.','Something has disturbed the tombs down in the Hall of Thanes, and strange noises have been heard coming from deep within the vaults. The spirits of the dead are light sleepers, and whatever is going on down there seems to have startled them.\r\n\r\nHead down there and see what you can do about that for us. If any of the dead are awake--well, put them back down!','','','','','','',0,0,0,70124),
(96395,2,10,0,16919,81,0,0,5,2.2,0,1.0,0,0,0,0.0,0,0,1.0,0,0,8,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,279899,1,0,279900,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0.0,0.0,0,0,0,0,0,0,0,0,0,0,0,0,7,0,0,0,7,0,0,0,7,0,0,0,7,0,0,0,7,3,0,0,0,0,0,0,0,0,890,878,0,0,18446744073709551615,-2,0,0,0,'An Ancient Grudge','Put the spirit of Faldrim Anvilmar to rest and then return to the Ghostly Attendant in the Hall of Thanes.','The... Thane... my mighty... Thane... he... needs to... rest.\r\n\r\nPlease... help him... help him... rest...\r\n\r\nConsumed... by... ancient... grudge... No... rest...','','','','','','',0,0,0,70124),
(96403,2,10,0,16919,81,0,0,6,2.2,5,1.0,0,0,0,0.0,0,0,1.0,0,0,8,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,279898,1,0,280096,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0.0,0.0,0,0,0,0,0,0,0,0,0,369,5,0,7,0,0,0,7,0,0,0,7,0,0,0,7,0,0,0,7,3,0,0,0,0,0,0,0,0,890,878,0,0,18446744073709551615,-2,0,0,0,'Important Heirlooms','Collect $1oa Dwarven Heirlooms from the Hall of Thanes.','You there, adventurer... I could really use your aid.\r\n\r\nI was sneaking--uh... \'patrolling\' in the vaults when a force of Dark Iron dwarves emerged from the depths in mole machines! I don\'t know what they are here for, but there is priceless loot--uh or, rather... I mean, \'important cultural heirlooms\' down there! Please, recover any such relics you see and return them here.\r\n\r\nI assure you that anything you recover will be catalogued and returned to its proper place of honor.','','','','','','',0,0,0,70124),
(98423,2,9,0,1,0,0,0,5,2.2,6,1.0,0,0,0,0.0,0,0,1.0,0,281030,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0.0,0.0,0,0,0,0,0,0,0,0,0,47,5,0,7,54,5,0,7,0,0,0,7,0,0,0,7,0,0,0,7,3,0,0,0,0,0,0,0,0,890,878,0,0,6130900294268439629,-2,0,0,0,'The Treaty of Understanding','Deliver the Treaty of Understanding to Magni Bronzebeard in Ironforge.','Inside of the vault you find a tablet detailing the terms of a peace treaty between the Bronzebeard and Wildhammer dwarf clans. It appears to date back to time period just after the War of the Three Hammers, and is very likely a notable piece of dwarven history. The king of Ironforge would no doubt be interested in this artifact.','','','','','','',0,0,0,70124);
DELETE FROM `quest_objectives` WHERE `QuestID` IN (96393,96394,96395,96403,98423);
INSERT INTO `quest_objectives` (`ID`,`QuestID`,`Type`,`Order`,`StorageIndex`,`ObjectID`,`Amount`,`ConditionalAmount`,`Flags`,`Flags2`,`ProgressBarWeight`,`ParentObjectiveID`,`Visible`,`Description`,`VerifiedBuild`) VALUES
(473137,96393,1,0,0,274286,1,-842150451,32,1,0.0,0,1,'',70124),
(473140,96394,0,0,0,263389,15,-842150451,32,0,0.0,0,1,'',70124),
(473142,96394,0,1,1,263390,10,-842150451,32,0,0.0,0,1,'',70124),
(473143,96395,0,0,2,261306,1,-842150451,32,0,0.0,0,1,'',70124),
(473157,96403,1,0,0,274289,8,-842150451,32,1,0.0,0,1,'',70124),
(477608,98423,1,0,0,281030,1,-842150451,32,0,0.0,0,1,'',70124);

-- quest givers and takers seen in the sniffs
INSERT IGNORE INTO `creature_queststarter` (`id`,`quest`,`VerifiedBuild`) VALUES
(264943,96394,70124),
(265003,96403,70124),
(265002,96395,70124);
INSERT IGNORE INTO `gameobject_queststarter` (`id`,`quest`,`VerifiedBuild`) VALUES
(673474,98423,70124);
INSERT IGNORE INTO `creature_questender` (`id`,`quest`,`VerifiedBuild`) VALUES
(264943,96394,70124),
(265003,96403,70124),
(265002,96395,70124),
(2784,98423,70124);

-- quest POIs (objective areas on the map and minimap)
DELETE FROM `quest_poi` WHERE `QuestID` IN (96394,96395,96403,98423);
INSERT INTO `quest_poi` (`QuestID`,`BlobIndex`,`Idx1`,`ObjectiveIndex`,`QuestObjectiveID`,`QuestObjectID`,`MapID`,`UiMapID`,`Priority`,`Flags`,`WorldEffectID`,`PlayerConditionID`,`NavigationPlayerConditionID`,`SpawnTrackingID`,`AlwaysAllowMergingBlobs`,`VerifiedBuild`) VALUES
(96394,0,0,32,0,0,0,1426,0,0,0,0,0,29384,0,70124),
(96395,0,0,32,0,0,0,1426,0,0,0,0,0,29384,0,70124),
(96403,0,0,-1,0,0,0,1455,0,0,0,0,0,3254787,0,70124),
(96403,0,1,32,0,0,0,1455,0,0,0,0,0,3254787,0,70124),
(98423,0,0,-1,0,0,0,1455,0,0,0,0,0,46858,0,70124),
(98423,0,1,32,0,0,0,1455,0,0,0,0,0,0,0,70124);
DELETE FROM `quest_poi_points` WHERE `QuestID` IN (96394,96395,96403,98423);
INSERT INTO `quest_poi_points` (`QuestID`,`Idx1`,`Idx2`,`X`,`Y`,`Z`,`VerifiedBuild`) VALUES
(96394,0,0,-5573,99,484,70124),
(96395,0,0,-5573,99,484,70124),
(96403,0,0,-4804,-968,399,70124),
(96403,1,0,-4804,-968,399,70124),
(98423,0,0,-4866,-1023,506,70124),
(98423,1,0,-4866,-1023,506,70124);

-- quest levels as the official server sends them (sniff_quest_levels.py)
REPLACE INTO `quest_template_classic_level` (`ID`,`LevelType`,`QuestLevel`) VALUES
(96393,2,16),
(96394,2,15),
(96395,2,15),
(96403,2,15),
(98423,2,16);
REPLACE INTO `quest_classic_level` (`ID`,`QuestLevel`,`MinLevel`,`MaxLevel`) VALUES
(96393,16,9,0),
(96394,15,10,0),
(96395,15,10,0),
(96403,15,10,0),
(98423,16,9,0);


-- quest_offer_reward: the quest givers text (Forever and new quests replaced, others only where missing)
DELETE FROM `quest_offer_reward` WHERE `ID` IN (96394,96395,96403,98423);
INSERT IGNORE INTO `quest_offer_reward` (`ID`,`Emote1`,`Emote2`,`Emote3`,`Emote4`,`EmoteDelay1`,`EmoteDelay2`,`EmoteDelay3`,`EmoteDelay4`,`RewardText`,`VerifiedBuild`) VALUES
(96394,0,0,0,0,0,0,0,0,'Ah good, it\'s done then. I can\'t have the other keepers refusing to enter the hall to do their duties on account of "ghosts" and "angry dead", can I?',70124),
(96395,0,0,0,0,0,0,0,0,'My... Thane... he... rests... yes... rest.\r\n\r\nThank... you...\r\n\r\nI... rest...',70124),
(96403,0,0,0,0,0,0,0,0,'Look at you, as brave as you are enterprising! I\'m a gnome of my word, and your reward is well earned! \r\n\r\nNow, let me take a look at the haul--uh, I mean... the recovered heirlooms. I need to get started appraising--excuse me, \'cataloging\' them so that they can be put back in their proper places as quickly as possible.',70124),
(98423,0,0,0,0,0,0,0,0,'Ah yes, my ancestor Madoran was the first Bronzebeard king of Ironforge following the great civil war between the Wildhammer, Bronzebeard, and Dark Iron clans. This treaty was drafted to codify the cessation of hostilities and terms of peace after the Bronzebeards won the mountain.\r\n\r\nAs you can see, the Wildhammers and Bronzebeards signed it... and the Dark Irons did not.\r\n\r\nThank you for recovering this and bringing it to me, $c.',70124);

-- quest_request_items: the quest givers text (Forever and new quests replaced, others only where missing)
DELETE FROM `quest_request_items` WHERE `ID` IN (96403,98423);
INSERT IGNORE INTO `quest_request_items` (`ID`,`EmoteOnComplete`,`EmoteOnIncomplete`,`EmoteOnCompleteDelay`,`EmoteOnIncompleteDelay`,`CompletionText`,`VerifiedBuild`) VALUES
(96403,0,0,0,0,'<Thom appears to absentmindedly rub his hands together as he looks at you.>',70124),
(98423,0,0,0,0,'What have you brought from the depths, $c.',70124);

-- loot of the Forever creatures (chance = drops / kills seen in the sniffs)
UPDATE `creature_template_difficulty` SET `LootID` = `Entry` WHERE `DifficultyID` = 0 AND `Entry` IN (261306,261319,263389,263396,263397,263438,263470);
UPDATE `creature_template_difficulty` SET `GoldMin` = 11, `GoldMax` = 35 WHERE `DifficultyID` = 0 AND `Entry` = 263389;
UPDATE `creature_template_difficulty` SET `GoldMin` = 19, `GoldMax` = 43 WHERE `DifficultyID` = 0 AND `Entry` = 263396;
UPDATE `creature_template_difficulty` SET `GoldMin` = 56, `GoldMax` = 73 WHERE `DifficultyID` = 0 AND `Entry` = 263438;
DELETE FROM `creature_loot_template` WHERE `Entry` IN (261306,261319,263389,263396,263397,263438,263470);
INSERT INTO `creature_loot_template` (`Entry`,`ItemType`,`Item`,`Chance`,`QuestRequired`,`LootMode`,`GroupId`,`MinCount`,`MaxCount`,`Comment`) VALUES
(261306,0,271097,100.0,0,1,0,1,1,'sniff: 1 of 1 kills'),
(261319,0,270256,100.0,0,1,0,1,1,'sniff: 1 of 1 kills'),
(263389,0,1501,16.67,0,1,0,1,1,'sniff: 1 of 6 kills'),
(263389,0,2589,33.33,0,1,0,4,4,'sniff: 2 of 6 kills'),
(263389,0,9759,16.67,0,1,0,1,1,'sniff: 1 of 6 kills'),
(263396,0,858,16.67,0,1,0,1,1,'sniff: 1 of 6 kills'),
(263396,0,1514,16.67,0,1,0,1,1,'sniff: 1 of 6 kills'),
(263396,0,1732,16.67,0,1,0,1,1,'sniff: 1 of 6 kills'),
(263396,0,2589,33.33,0,1,0,5,5,'sniff: 2 of 6 kills'),
(263396,0,2977,16.67,0,1,0,1,1,'sniff: 1 of 6 kills'),
(263396,0,6543,16.67,0,1,0,1,1,'sniff: 1 of 6 kills'),
(263396,0,275893,33.33,0,1,0,1,1,'sniff: 2 of 6 kills'),
(263397,0,818,50.0,0,1,0,1,1,'sniff: 1 of 2 kills'),
(263397,0,1732,50.0,0,1,0,1,1,'sniff: 1 of 2 kills'),
(263438,0,2971,33.33,0,1,0,1,1,'sniff: 1 of 3 kills'),
(263438,0,4293,33.33,0,1,0,1,1,'sniff: 1 of 3 kills'),
(263470,0,2318,42.86,0,1,0,1,2,'sniff: 3 of 7 kills'),
(263470,0,2319,14.29,0,1,0,1,1,'sniff: 1 of 7 kills'),
(263470,0,2672,42.86,0,1,0,1,1,'sniff: 3 of 7 kills');

-- quest items of the creatures (creature query) and their drops when no sniffed loot had them
DELETE FROM `creature_questitem` WHERE `CreatureEntry` IN (261319);
INSERT INTO `creature_questitem` (`CreatureEntry`,`DifficultyID`,`Idx`,`ItemId`,`VerifiedBuild`) VALUES
(261319,0,0,274286,70124);
UPDATE `creature_template_difficulty` SET `LootID` = `Entry` WHERE `DifficultyID` = 0 AND `LootID` = 0 AND `Entry` IN (261319);
DELETE FROM `creature_loot_template` WHERE (`Entry`, `Item`) IN ((261319,274286));
INSERT INTO `creature_loot_template` (`Entry`,`ItemType`,`Item`,`Chance`,`QuestRequired`,`LootMode`,`GroupId`,`MinCount`,`MaxCount`,`Comment`) VALUES
(261319,0,274286,100,1,1,0,1,1,'quest item of the creature query (drop not seen in the sniffs)');
