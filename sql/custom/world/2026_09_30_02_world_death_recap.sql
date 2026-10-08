-- Death recap (deathcam): replay actors. 9100000 is the base; 9100001-9102000 are identical copies whose names
-- the server picks per replay (a replayed player shows their own name). The actors never fight, loot or give XP.
DELETE FROM `creature_template` WHERE `entry` BETWEEN 9100000 AND 9102000;
INSERT INTO `creature_template` (`entry`,`KillCredit1`,`KillCredit2`,`name`,`femaleName`,`subname`,`TitleAlt`,`IconName`,`RequiredExpansion`,`VignetteID`,`faction`,`npcflag`,`speed_walk`,`speed_run`,`scale`,`Classification`,`dmgschool`,`BaseAttackTime`,`RangeAttackTime`,`BaseVariance`,`RangeVariance`,`unit_class`,`unit_flags`,`unit_flags2`,`unit_flags3`,`family`,`trainer_class`,`type`,`VehicleId`,`AIName`,`MovementType`,`ExperienceModifier`,`RacialLeader`,`movementId`,`WidgetSetID`,`WidgetSetUnitConditionID`,`RegenHealth`,`CreatureImmunitiesId`,`flags_extra`,`ScriptName`,`StringId`,`VerifiedBuild`)
WITH RECURSIVE `seq` (`n`) AS (SELECT 0 UNION ALL SELECT `n` + 1 FROM `seq` WHERE `n` < 99)
SELECT 9100000 + `a`.`n` * 100 + `b`.`n`,0,0,'Death Recap','','Death Recap',NULL,NULL,0,0,35,0,1,1.14286,1,0,0,2000,2000,1,1,1,0x302,0,0,0,0,7,0,'NullCreatureAI',0,0,0,0,0,0,0,0,0x40,'',NULL,0
FROM `seq` `a` JOIN `seq` `b` WHERE `a`.`n` <= 20 AND `a`.`n` * 100 + `b`.`n` <= 2000;

DELETE FROM `creature_template_model` WHERE `CreatureID` BETWEEN 9100000 AND 9102000;
INSERT INTO `creature_template_model` (`CreatureID`,`Idx`,`CreatureDisplayID`,`DisplayScale`,`Probability`,`VerifiedBuild`)
SELECT `entry`,0,49,1,1,0 FROM `creature_template` WHERE `entry` BETWEEN 9100000 AND 9102000;

DELETE FROM `creature_template_difficulty` WHERE `Entry` BETWEEN 9100000 AND 9102000;
INSERT INTO `creature_template_difficulty` (`Entry`,`DifficultyID`,`LevelScalingDeltaMin`,`LevelScalingDeltaMax`,`ContentTuningID`,`HealthScalingExpansion`,`HealthModifier`,`ManaModifier`,`ArmorModifier`,`DamageModifier`,`CreatureDifficultyID`,`TypeFlags`,`TypeFlags2`,`TypeFlags3`,`LootID`,`PickPocketLootID`,`SkinLootID`,`GoldMin`,`GoldMax`,`StaticFlags1`,`StaticFlags2`,`StaticFlags3`,`StaticFlags4`,`StaticFlags5`,`StaticFlags6`,`StaticFlags7`,`StaticFlags8`,`VerifiedBuild`)
SELECT `entry`,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 FROM `creature_template` WHERE `entry` BETWEEN 9100000 AND 9102000;
