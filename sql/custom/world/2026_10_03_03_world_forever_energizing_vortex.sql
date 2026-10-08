-- Re-run after the sniff files (2026_10_01_03/04, 2026_10_02_00/06, 2026_10_03_00) were regenerated: they rewrite rows this file adjusts (last: 2026-10-05, sniff of 2026-10-04 20:05).
-- Classic 1.60 (WoW Forever): the Energizing Vortex (267599) tornadoes on Zephras Isle carry Energizing Winds (1299003, aura 395 =
-- area trigger, create properties 43253). Touching one gives Blessing of Zephras (1258510: run speed +40% for 5 min, ends on
-- hostile action), seen on the player in the official beta sniff (70170); the player casts it on himself
-- (classic_at_energizing_vortex), so leaving the moving trigger does not remove it. The shape is not in the client data: a 5 yd
-- sphere, about the size of the tornado model. Safe to run again.
DELETE FROM `areatrigger_template` WHERE `Id` = 43253 AND `IsCustom` = 1;
INSERT INTO `areatrigger_template` (`Id`, `IsCustom`, `Flags`, `ActionSetId`, `ActionSetFlags`, `VerifiedBuild`) VALUES
(43253, 1, 0, 0, 0, 0);

DELETE FROM `areatrigger_template_actions` WHERE `AreaTriggerId` = 43253 AND `IsCustom` = 1;

DELETE FROM `areatrigger_create_properties` WHERE `Id` = 43253 AND `IsCustom` = 0;
INSERT INTO `areatrigger_create_properties` (`Id`, `IsCustom`, `AreaTriggerId`, `IsAreatriggerCustom`, `Flags`, `MoveCurveId`,
`ScaleCurveId`, `MorphCurveId`, `FacingCurveId`, `AnimId`, `AnimKitId`, `DecalPropertiesId`, `SpellForVisuals`, `PositionalSoundKitId`,
`TimeToTargetScale`, `Speed`, `SpeedIsTime`, `Shape`, `ShapeData0`, `ShapeData1`, `ShapeData2`, `ShapeData3`, `ShapeData4`,
`ShapeData5`, `ShapeData6`, `ShapeData7`, `Roll`, `Pitch`, `Yaw`, `TargetRoll`, `TargetPitch`, `TargetYaw`, `ScriptName`,
`VerifiedBuild`) VALUES
(43253, 0, 43253, 1, 0, 0, 0, 0, 0, -1, -1, 0, NULL, 0, 0, 0, 0, 0, 5, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'classic_at_energizing_vortex', 0);
