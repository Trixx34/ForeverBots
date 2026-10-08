-- Classic 1.60 (WoW Forever), the way to Lorthuna and The Turncoats house (quests The Inner Sanctum 93958 / Confront Lorthuna 92646, official beta sniff
-- 70205 of 2026-10-04). Safe to run again.

-- Portal To Rohashi Spires (586726, spellcaster object, appears when The Inner Sanctum is accepted): 1271396 triggers 1259410 after
-- 2 s, a teleport to the Rohashi Spires platform (the player landed at 3078.55 428.91 1202.74). Usable while either quest is open.
DELETE FROM `spell_target_position` WHERE `ID` = 1259410 AND `EffectIndex` = 0;
INSERT INTO `spell_target_position` (`ID`, `EffectIndex`, `OrderIndex`, `MapID`, `PositionX`, `PositionY`, `PositionZ`, `Orientation`, `VerifiedBuild`) VALUES
(1259410, 0, 0, 2991, 3078.55, 428.91, 1202.74, 4.89, 70205);
DELETE FROM `conditions` WHERE `SourceTypeOrReferenceId` = 17 AND `SourceEntry` = 1271396;
INSERT INTO `conditions` (`SourceTypeOrReferenceId`, `SourceGroup`, `SourceEntry`, `SourceId`, `ElseGroup`, `ConditionTypeOrReference`, `ConditionTarget`, `ConditionValue1`, `ConditionValue2`, `ConditionValue3`, `ConditionStringValue1`, `NegativeCondition`, `ErrorType`, `ErrorTextId`, `ScriptName`, `Comment`) VALUES
(17, 0, 1271396, 0, 0, 9, 0, 93958, 0, 0, '', 0, 0, 0, '', 'Portal To Rohashi Spires - The Inner Sanctum taken'),
(17, 0, 1271396, 0, 1, 9, 0, 92646, 0, 0, '', 0, 0, 0, '', 'Portal To Rohashi Spires - or Confront Lorthuna taken');

-- The wind at the platform edge (3079 265.5 1202.7) launches the player to Lorthuna (1256704: jump to the database destination,
-- landing 3013.0 118.4 1164.6; 1259231 restores health and mana there). Not in the clients AreaTrigger.db2: a static server area
-- trigger, a 6 yd sphere (classic_at_rohashi_wind).
DELETE FROM `spell_target_position` WHERE `ID` = 1256704 AND `EffectIndex` = 0;
INSERT INTO `spell_target_position` (`ID`, `EffectIndex`, `OrderIndex`, `MapID`, `PositionX`, `PositionY`, `PositionZ`, `Orientation`, `VerifiedBuild`) VALUES
(1256704, 0, 0, 2991, 3013.0, 118.4, 1164.6, 4.29, 70205);

DELETE FROM `areatrigger_template` WHERE `Id` = 43254 AND `IsCustom` = 1;
INSERT INTO `areatrigger_template` (`Id`, `IsCustom`, `Flags`, `ActionSetId`, `ActionSetFlags`, `VerifiedBuild`) VALUES
(43254, 1, 0, 0, 0, 0);
DELETE FROM `areatrigger_create_properties` WHERE `Id` = 43254 AND `IsCustom` = 0;
INSERT INTO `areatrigger_create_properties` (`Id`, `IsCustom`, `AreaTriggerId`, `IsAreatriggerCustom`, `Flags`, `MoveCurveId`,
`ScaleCurveId`, `MorphCurveId`, `FacingCurveId`, `AnimId`, `AnimKitId`, `DecalPropertiesId`, `SpellForVisuals`, `PositionalSoundKitId`,
`TimeToTargetScale`, `Speed`, `SpeedIsTime`, `Shape`, `ShapeData0`, `ShapeData1`, `ShapeData2`, `ShapeData3`, `ShapeData4`,
`ShapeData5`, `ShapeData6`, `ShapeData7`, `Roll`, `Pitch`, `Yaw`, `TargetRoll`, `TargetPitch`, `TargetYaw`, `ScriptName`,
`VerifiedBuild`) VALUES
(43254, 0, 43254, 1, 0, 0, 0, 0, 0, -1, -1, 0, NULL, 0, 0, 0, 0, 0, 6, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'classic_at_rohashi_wind', 0);
DELETE FROM `areatrigger` WHERE `SpawnId` = 1;
INSERT INTO `areatrigger` (`SpawnId`, `AreaTriggerCreatePropertiesId`, `IsCustom`, `MapId`, `SpawnDifficulties`, `PosX`, `PosY`, `PosZ`, `Orientation`, `PhaseUseFlags`, `PhaseId`, `PhaseGroup`, `ScriptName`, `Comment`, `VerifiedBuild`) VALUES
(1, 43254, 0, 2991, '0', 3079.0, 265.5, 1202.7, 4.27, 0, 0, 0, 'classic_at_rohashi_wind', 'Rohashi Spires - wind to Lorthuna', 70205);

-- The Turncoat (92643): "Find the secluded house in Shendar Highlands" (255013) came when the player reached 2706.5 1100.7 769.9
-- (official beta sniff 70205): a 10 yd static area trigger there (classic_at_secluded_house).
DELETE FROM `areatrigger_template` WHERE `Id` = 43255 AND `IsCustom` = 1;
INSERT INTO `areatrigger_template` (`Id`, `IsCustom`, `Flags`, `ActionSetId`, `ActionSetFlags`, `VerifiedBuild`) VALUES
(43255, 1, 0, 0, 0, 0);
DELETE FROM `areatrigger_create_properties` WHERE `Id` = 43255 AND `IsCustom` = 0;
INSERT INTO `areatrigger_create_properties` (`Id`, `IsCustom`, `AreaTriggerId`, `IsAreatriggerCustom`, `Flags`, `MoveCurveId`,
`ScaleCurveId`, `MorphCurveId`, `FacingCurveId`, `AnimId`, `AnimKitId`, `DecalPropertiesId`, `SpellForVisuals`, `PositionalSoundKitId`,
`TimeToTargetScale`, `Speed`, `SpeedIsTime`, `Shape`, `ShapeData0`, `ShapeData1`, `ShapeData2`, `ShapeData3`, `ShapeData4`,
`ShapeData5`, `ShapeData6`, `ShapeData7`, `Roll`, `Pitch`, `Yaw`, `TargetRoll`, `TargetPitch`, `TargetYaw`, `ScriptName`,
`VerifiedBuild`) VALUES
(43255, 0, 43255, 1, 0, 0, 0, 0, 0, -1, -1, 0, NULL, 0, 0, 0, 0, 0, 10, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'classic_at_secluded_house', 0);
DELETE FROM `areatrigger` WHERE `SpawnId` = 2;
INSERT INTO `areatrigger` (`SpawnId`, `AreaTriggerCreatePropertiesId`, `IsCustom`, `MapId`, `SpawnDifficulties`, `PosX`, `PosY`, `PosZ`, `Orientation`, `PhaseUseFlags`, `PhaseId`, `PhaseGroup`, `ScriptName`, `Comment`, `VerifiedBuild`) VALUES
(2, 43255, 0, 2991, '0', 2706.5, 1100.7, 769.9, 0, 0, 0, 0, 'classic_at_secluded_house', 'Shen\'dar Highlands - the turncoat\'s secluded house', 70205);
