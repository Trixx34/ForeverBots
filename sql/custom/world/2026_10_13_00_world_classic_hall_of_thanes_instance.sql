-- Classic 1.60: The Hall of Thanes dungeon, map 3065 (zone 16919), from the official sniff of 2026-10-12 (gen_hall_of_thanes.py).
-- Entrance: the Instance Portal (620182) at the bottom of Old Ironforge, (-4822.49, -931.43, 398.98) on map 0. The client sends nothing when it
-- walks in (server side trigger, 2.5 yd: ported 0.6 yd from the portal, not at 2.8 yd); it lands at (-95.62, -0.16, 510.94) inside.
-- Way out: the Instance Portal inside at (-108.53, -0.03, 511.48) puts the player back at (-4821.00, -937.63, 399.05) on map 0, 6.7 yd from the
-- entrance portal. Ghosts release to the Gates of Ironforge graveyard (852, the cemetery list sent inside). Safe to run again.
DELETE FROM `instance_template` WHERE `map` = 3065;
INSERT INTO `instance_template` (`map`,`parent`,`script`) VALUES
(3065,0,'classic_instance_hall_of_thanes');

DELETE FROM `world_safe_locs` WHERE `ID` IN (1104102,1104103);
INSERT INTO `world_safe_locs` (`ID`,`MapID`,`LocX`,`LocY`,`LocZ`,`Facing`,`Comment`) VALUES
(1104102,3065,-95.62,-0.16,510.94,0.000,'The Hall of Thanes (dungeon) - entrance inside'),
(1104103,0,-4821.00,-937.63,399.05,5.075,'The Hall of Thanes (dungeon) - exit outside, Old Ironforge');

-- custom area triggers: sphere, radius 2.5, at the two portals
DELETE FROM `areatrigger_template` WHERE `IsCustom` = 1 AND `Id` IN (302,303);
INSERT INTO `areatrigger_template` (`Id`,`IsCustom`,`Flags`,`ActionSetId`,`ActionSetFlags`,`VerifiedBuild`) VALUES
(302,1,0,0,0,0),
(303,1,0,0,0,0);
DELETE FROM `areatrigger_template_actions` WHERE `IsCustom` = 1 AND `AreaTriggerId` IN (302,303);
INSERT INTO `areatrigger_template_actions` (`AreaTriggerId`,`IsCustom`,`ActionType`,`ActionParam`,`TargetType`) VALUES
(302,1,2,1104102,5),
(303,1,2,1104103,5);
DELETE FROM `areatrigger_create_properties` WHERE `IsCustom` = 1 AND `Id` IN (302,303);
INSERT INTO `areatrigger_create_properties` (`Id`,`IsCustom`,`AreaTriggerId`,`IsAreatriggerCustom`,`Flags`,`MoveCurveId`,`ScaleCurveId`,`MorphCurveId`,`FacingCurveId`,`AnimId`,`AnimKitId`,`DecalPropertiesId`,`SpellForVisuals`,`PositionalSoundKitId`,`TimeToTargetScale`,`Speed`,`SpeedIsTime`,`Shape`,`ShapeData0`,`ShapeData1`,`ShapeData2`,`ShapeData3`,`ShapeData4`,`ShapeData5`,`ShapeData6`,`ShapeData7`,`Roll`,`Pitch`,`Yaw`,`TargetRoll`,`TargetPitch`,`TargetYaw`,`ScriptName`,`VerifiedBuild`) VALUES
(302,1,302,1,0,0,0,0,0,-1,0,0,NULL,0,0,0,0,0,2.5,2.5,0,0,0,0,0,0,0,0,0,NULL,NULL,NULL,'',0),
(303,1,303,1,0,0,0,0,0,-1,0,0,NULL,0,0,0,0,0,2.5,2.5,0,0,0,0,0,0,0,0,0,NULL,NULL,NULL,'',0);
DELETE FROM `areatrigger` WHERE `SpawnId` IN (7,8);
INSERT INTO `areatrigger` (`SpawnId`,`AreaTriggerCreatePropertiesId`,`IsCustom`,`MapId`,`SpawnDifficulties`,`PosX`,`PosY`,`PosZ`,`Orientation`,`PhaseUseFlags`,`PhaseId`,`PhaseGroup`,`ScriptName`,`Comment`,`VerifiedBuild`) VALUES
(7,302,1,0,'0',-4822.49,-931.43,398.98,0,1,0,0,'','The Hall of Thanes - dungeon entrance (Instance Portal, Old Ironforge)',0),
(8,303,1,3065,'1',-108.53,-0.03,511.48,0,1,0,0,'','The Hall of Thanes (dungeon) - exit (Instance Portal)',0);

DELETE FROM `graveyard_zone` WHERE `GhostZone` = 16919;
INSERT INTO `graveyard_zone` (`ID`,`GhostZone`,`Comment`) VALUES
(852,16919,'The Hall of Thanes (dungeon) - Dun Morogh, Gates of Ironforge');
