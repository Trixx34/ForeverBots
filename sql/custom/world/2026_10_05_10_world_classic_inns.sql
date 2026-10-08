-- Classic 1.60: vanilla inns missing from areatrigger_tavern (VMaNGOS 1.12 list; the triggers are in the client AreaTrigger.db2)
DELETE FROM `areatrigger_tavern` WHERE `id` IN (713, 716, 720, 982, 1025, 2286);
INSERT INTO `areatrigger_tavern` (`id`, `name`) VALUES
(713, 'Wetlands - Menethil Harbor - Deepwater Tavern'),
(716, 'Darkshore - Auberdine'),
(720, 'Silverpine Forest - The Sepulcher'),
(982, 'The Barrens - Camp Taurajo'),
(1025, 'Feralas - Camp Mojache'),
(2286, 'Thousand Needles - Freewind Post');
-- Zephras Isle inns: the client data has no inn area triggers there (no AreaTrigger.db2 rows on map 2991, no rest-flagged
-- areas), so the inns are server side area triggers with the tavern action (template 137, TC custom). Cylinder of radius 22,
-- 15 high, from 3 below the innkeeper: Coriella Calmbreeze (Zephras Inn) and Donaal Downbreeze (south inn).
DELETE FROM `areatrigger_create_properties` WHERE `Id` = 182 AND `IsCustom` = 1;
INSERT INTO `areatrigger_create_properties` (`Id`, `IsCustom`, `AreaTriggerId`, `IsAreatriggerCustom`, `Flags`, `MoveCurveId`, `ScaleCurveId`, `MorphCurveId`, `FacingCurveId`, `AnimId`, `AnimKitId`, `DecalPropertiesId`, `SpellForVisuals`, `PositionalSoundKitId`, `TimeToTargetScale`, `Speed`, `SpeedIsTime`, `Shape`, `ShapeData0`, `ShapeData1`, `ShapeData2`, `ShapeData3`, `ShapeData4`, `ShapeData5`, `ShapeData6`, `ShapeData7`, `Roll`, `Pitch`, `Yaw`, `TargetRoll`, `TargetPitch`, `TargetYaw`, `ScriptName`, `VerifiedBuild`) VALUES
(182, 1, 137, 1, 0, 0, 0, 0, 0, -1, 0, 0, NULL, 0, 0, 0, 0, 4, 22, 22, 15, 15, 0, 0, 0, 0, 0, 0, 0, NULL, NULL, NULL, '', 0);
DELETE FROM `areatrigger` WHERE `SpawnId` IN (3, 4);
INSERT INTO `areatrigger` (`SpawnId`, `AreaTriggerCreatePropertiesId`, `IsCustom`, `MapId`, `SpawnDifficulties`, `PosX`, `PosY`, `PosZ`, `Orientation`, `PhaseUseFlags`, `PhaseId`, `PhaseGroup`, `ScriptName`, `Comment`, `VerifiedBuild`) VALUES
(3, 182, 1, 2991, '0', 3353.0, 1838.5, 826.4, 0, 1, 0, 0, '', 'Zephras Isle - Zephras Inn (rested)', 0),
(4, 182, 1, 2991, '0', 2263.4, 772.1, 676.7, 0, 1, 0, 0, '', 'Zephras Isle - south inn (rested)', 0);
