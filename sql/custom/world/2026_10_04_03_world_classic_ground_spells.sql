-- Classic 1.60: ground spells of the official client data create an area trigger (effect 1, create properties below) and put a
-- periodic dummy aura on the caster (effect 2): Blizzard, Flamestrike (burning ground), Rain of Fire, Volley; Flare only the area.
-- Nothing of it was on the server: no ground effect, no damage ticks. Areas: spheres with the effect radius of the client data
-- (8 yd, Flamestrike 5 yd, Flare 10 yd), lasting the spells duration. Ticks: classic_spell_ground_area_damage (vanilla damage
-- per rank). The retail Rain of Fire / Flame Patch scripts are unbound from these ids. Safe to run again.
DELETE FROM `areatrigger_template` WHERE `Id` IN (41261,41262,41263,41264,41265,41260,41266,41267,41268,41269,41270,41271,41450,41452,41455,41456,41245,41244,41242,41236) AND `IsCustom` = 1;
INSERT INTO `areatrigger_template` (`Id`, `IsCustom`, `Flags`, `ActionSetId`, `ActionSetFlags`, `VerifiedBuild`) VALUES
(41261, 1, 0, 0, 0, 0),
(41262, 1, 0, 0, 0, 0),
(41263, 1, 0, 0, 0, 0),
(41264, 1, 0, 0, 0, 0),
(41265, 1, 0, 0, 0, 0),
(41260, 1, 0, 0, 0, 0),
(41266, 1, 0, 0, 0, 0),
(41267, 1, 0, 0, 0, 0),
(41268, 1, 0, 0, 0, 0),
(41269, 1, 0, 0, 0, 0),
(41270, 1, 0, 0, 0, 0),
(41271, 1, 0, 0, 0, 0),
(41450, 1, 0, 0, 0, 0),
(41452, 1, 0, 0, 0, 0),
(41455, 1, 0, 0, 0, 0),
(41456, 1, 0, 0, 0, 0),
(41245, 1, 0, 0, 0, 0),
(41244, 1, 0, 0, 0, 0),
(41242, 1, 0, 0, 0, 0),
(41236, 1, 0, 0, 0, 0);

DELETE FROM `areatrigger_create_properties` WHERE `Id` IN (41261,41262,41263,41264,41265,41260,41266,41267,41268,41269,41270,41271,41450,41452,41455,41456,41245,41244,41242,41236) AND `IsCustom` = 0;
INSERT INTO `areatrigger_create_properties` (`Id`, `IsCustom`, `AreaTriggerId`, `IsAreatriggerCustom`, `Flags`, `MoveCurveId`,
`ScaleCurveId`, `MorphCurveId`, `FacingCurveId`, `AnimId`, `AnimKitId`, `DecalPropertiesId`, `SpellForVisuals`, `PositionalSoundKitId`,
`TimeToTargetScale`, `Speed`, `SpeedIsTime`, `Shape`, `ShapeData0`, `ShapeData1`, `ShapeData2`, `ShapeData3`, `ShapeData4`,
`ShapeData5`, `ShapeData6`, `ShapeData7`, `Roll`, `Pitch`, `Yaw`, `TargetRoll`, `TargetPitch`, `TargetYaw`, `ScriptName`,
`VerifiedBuild`) VALUES
(41261, 0, 41261, 1, 0, 0, 0, 0, 0, -1, -1, 0, NULL, 0, 0, 0, 0, 0, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '', 0), -- Blizzard R1,
(41262, 0, 41262, 1, 0, 0, 0, 0, 0, -1, -1, 0, NULL, 0, 0, 0, 0, 0, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '', 0), -- Blizzard R2,
(41263, 0, 41263, 1, 0, 0, 0, 0, 0, -1, -1, 0, NULL, 0, 0, 0, 0, 0, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '', 0), -- Blizzard R3,
(41264, 0, 41264, 1, 0, 0, 0, 0, 0, -1, -1, 0, NULL, 0, 0, 0, 0, 0, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '', 0), -- Blizzard R4,
(41265, 0, 41265, 1, 0, 0, 0, 0, 0, -1, -1, 0, NULL, 0, 0, 0, 0, 0, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '', 0), -- Blizzard R5,
(41260, 0, 41260, 1, 0, 0, 0, 0, 0, -1, -1, 0, NULL, 0, 0, 0, 0, 0, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '', 0), -- Blizzard R6,
(41266, 0, 41266, 1, 0, 0, 0, 0, 0, -1, -1, 0, NULL, 0, 0, 0, 0, 0, 5, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '', 0), -- Flamestrike R1,
(41267, 0, 41267, 1, 0, 0, 0, 0, 0, -1, -1, 0, NULL, 0, 0, 0, 0, 0, 5, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '', 0), -- Flamestrike R2,
(41268, 0, 41268, 1, 0, 0, 0, 0, 0, -1, -1, 0, NULL, 0, 0, 0, 0, 0, 5, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '', 0), -- Flamestrike R3,
(41269, 0, 41269, 1, 0, 0, 0, 0, 0, -1, -1, 0, NULL, 0, 0, 0, 0, 0, 5, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '', 0), -- Flamestrike R4,
(41270, 0, 41270, 1, 0, 0, 0, 0, 0, -1, -1, 0, NULL, 0, 0, 0, 0, 0, 5, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '', 0), -- Flamestrike R5,
(41271, 0, 41271, 1, 0, 0, 0, 0, 0, -1, -1, 0, NULL, 0, 0, 0, 0, 0, 5, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '', 0), -- Flamestrike R6,
(41450, 0, 41450, 1, 0, 0, 0, 0, 0, -1, -1, 0, NULL, 0, 0, 0, 0, 0, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '', 0), -- Rain of Fire R1,
(41452, 0, 41452, 1, 0, 0, 0, 0, 0, -1, -1, 0, NULL, 0, 0, 0, 0, 0, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '', 0), -- Rain of Fire R2,
(41455, 0, 41455, 1, 0, 0, 0, 0, 0, -1, -1, 0, NULL, 0, 0, 0, 0, 0, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '', 0), -- Rain of Fire R3,
(41456, 0, 41456, 1, 0, 0, 0, 0, 0, -1, -1, 0, NULL, 0, 0, 0, 0, 0, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '', 0), -- Rain of Fire R4,
(41245, 0, 41245, 1, 0, 0, 0, 0, 0, -1, -1, 0, NULL, 0, 0, 0, 0, 0, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '', 0), -- Volley R1,
(41244, 0, 41244, 1, 0, 0, 0, 0, 0, -1, -1, 0, NULL, 0, 0, 0, 0, 0, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '', 0), -- Volley R2,
(41242, 0, 41242, 1, 0, 0, 0, 0, 0, -1, -1, 0, NULL, 0, 0, 0, 0, 0, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '', 0), -- Volley R3,
(41236, 0, 41236, 1, 0, 0, 0, 0, 0, -1, -1, 0, NULL, 0, 0, 0, 0, 0, 10, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '', 0); -- Flare

DELETE FROM `spell_script_names` WHERE (`spell_id` = 5740 AND `ScriptName` = 'spell_warl_rain_of_fire') OR (`spell_id` = 2120 AND `ScriptName` = 'spell_mage_flame_patch');
DELETE FROM `spell_script_names` WHERE `ScriptName` = 'classic_spell_ground_area_damage';
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(10, 'classic_spell_ground_area_damage'),
(6141, 'classic_spell_ground_area_damage'),
(8427, 'classic_spell_ground_area_damage'),
(10185, 'classic_spell_ground_area_damage'),
(10186, 'classic_spell_ground_area_damage'),
(10187, 'classic_spell_ground_area_damage'),
(2120, 'classic_spell_ground_area_damage'),
(2121, 'classic_spell_ground_area_damage'),
(8422, 'classic_spell_ground_area_damage'),
(8423, 'classic_spell_ground_area_damage'),
(10215, 'classic_spell_ground_area_damage'),
(10216, 'classic_spell_ground_area_damage'),
(5740, 'classic_spell_ground_area_damage'),
(6219, 'classic_spell_ground_area_damage'),
(11677, 'classic_spell_ground_area_damage'),
(11678, 'classic_spell_ground_area_damage'),
(1510, 'classic_spell_ground_area_damage'),
(14294, 'classic_spell_ground_area_damage'),
(14295, 'classic_spell_ground_area_damage');
