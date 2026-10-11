-- Classic 1.60: Ruins of Lordaeron dungeon, map 2999 (zone 16611; the Undercity layout), sniff of 2026-10-11.
-- Entrance: the official server has a server-side area trigger object (entry 42348) at the Ruins of Lordaeron, (1804.4, 180.0, 70.4)
-- on map 0; the client sends nothing when it walks in. Entering puts the player at (1804.15, 149.17, 70.4) on map 2999, walking out
-- (around (1804.5, 162, 70.4) inside, every exit in the sniff) back at (1804.86, 184.85, 70.4) on map 0. A death inside releases to the
-- Ruins of Lordaeron graveyard (safe loc 853).
DELETE FROM `instance_template` WHERE `map` = 2999;
INSERT INTO `instance_template` (`map`,`parent`,`script`) VALUES
(2999,0,'');

DELETE FROM `world_safe_locs` WHERE `ID` IN (1104100,1104101);
INSERT INTO `world_safe_locs` (`ID`,`MapID`,`LocX`,`LocY`,`LocZ`,`Facing`,`Comment`) VALUES
(1104100,2999,1804.15,149.174,70.399,4.65,'Ruins of Lordaeron (dungeon) - entrance inside'),
(1104101,0,1804.86,184.847,70.4,1.542,'Ruins of Lordaeron (dungeon) - exit outside');

-- custom area triggers: sphere, radius 3 (the exit lands 4.8 yd from the entrance trigger and 12 yd from the exit trigger)
DELETE FROM `areatrigger_template` WHERE `IsCustom` = 1 AND `Id` IN (300,301);
INSERT INTO `areatrigger_template` (`Id`,`IsCustom`,`Flags`,`ActionSetId`,`ActionSetFlags`,`VerifiedBuild`) VALUES
(300,1,0,0,0,0),
(301,1,0,0,0,0);
DELETE FROM `areatrigger_template_actions` WHERE `IsCustom` = 1 AND `AreaTriggerId` IN (300,301);
INSERT INTO `areatrigger_template_actions` (`AreaTriggerId`,`IsCustom`,`ActionType`,`ActionParam`,`TargetType`) VALUES
(300,1,2,1104100,5),
(301,1,2,1104101,5);
DELETE FROM `areatrigger_create_properties` WHERE `IsCustom` = 1 AND `Id` IN (300,301);
INSERT INTO `areatrigger_create_properties` (`Id`,`IsCustom`,`AreaTriggerId`,`IsAreatriggerCustom`,`Flags`,`MoveCurveId`,`ScaleCurveId`,`MorphCurveId`,`FacingCurveId`,`AnimId`,`AnimKitId`,`DecalPropertiesId`,`SpellForVisuals`,`PositionalSoundKitId`,`TimeToTargetScale`,`Speed`,`SpeedIsTime`,`Shape`,`ShapeData0`,`ShapeData1`,`ShapeData2`,`ShapeData3`,`ShapeData4`,`ShapeData5`,`ShapeData6`,`ShapeData7`,`Roll`,`Pitch`,`Yaw`,`TargetRoll`,`TargetPitch`,`TargetYaw`,`ScriptName`,`VerifiedBuild`) VALUES
(300,1,300,1,0,0,0,0,0,-1,0,0,NULL,0,0,0,0,0,3,3,0,0,0,0,0,0,0,0,0,NULL,NULL,NULL,'',0),
(301,1,301,1,0,0,0,0,0,-1,0,0,NULL,0,0,0,0,0,3,3,0,0,0,0,0,0,0,0,0,NULL,NULL,NULL,'',0);
DELETE FROM `areatrigger` WHERE `SpawnId` IN (5,6);
INSERT INTO `areatrigger` (`SpawnId`,`AreaTriggerCreatePropertiesId`,`IsCustom`,`MapId`,`SpawnDifficulties`,`PosX`,`PosY`,`PosZ`,`Orientation`,`PhaseUseFlags`,`PhaseId`,`PhaseGroup`,`ScriptName`,`Comment`,`VerifiedBuild`) VALUES
(5,300,1,0,'0',1804.4,180.0,70.4,0,1,0,0,'','Ruins of Lordaeron - dungeon entrance (official area trigger 42348)',0),
(6,301,1,2999,'1',1804.5,162.0,70.4,0,1,0,0,'','Ruins of Lordaeron (dungeon) - exit',0);

DELETE FROM `graveyard_zone` WHERE `GhostZone` = 16611;
INSERT INTO `graveyard_zone` (`ID`,`GhostZone`,`Comment`) VALUES
(853,16611,'Ruins of Lordaeron (dungeon) - Tirisfal Glades, Ruins of Lordaeron');
