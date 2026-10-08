-- Classic 1.60 (WoW Forever): Undead paladin start in Deathknell, from a sniff of an Undead paladin (2026-10-06). Safe to run again.

-- paladin only (the sniffed quests carry no class mask)
INSERT INTO `quest_template_addon` (`ID`, `AllowableClasses`) VALUES
(98601, 2),   -- A Difficult Path
(90902, 2),   -- Rediscovering the Light
(91208, 2),   -- Coming to Terms
(91209, 2),   -- Continue Your Training
(91282, 2)    -- A Second Home
ON DUPLICATE KEY UPDATE `AllowableClasses` = VALUES(`AllowableClasses`);

-- Rediscovering the Light (90902): accepting it casts Rediscovering the Light (1240330), which teaches Holy Light (1240334 -> 635)
-- and stays on as a debuff: every heal triggers 1240329, a hit for 10% of the paladin's maximum health. The reward spell
-- (1240331) removes it. Heal 5 Injured Deathguards (259377) with Holy Light.
INSERT INTO `quest_template_addon` (`ID`, `SourceSpellID`) VALUES (90902, 1240330)
ON DUPLICATE KEY UPDATE `SourceSpellID` = VALUES(`SourceSpellID`);

DELETE FROM `spell_linked_spell` WHERE `spell_trigger` = 1240330 AND `spell_effect` = 1240334;
INSERT INTO `spell_linked_spell` (`spell_trigger`, `spell_effect`, `type`, `comment`) VALUES
(1240330, 1240334, 0, 'Rediscovering the Light - learn Holy Light');

-- procs on the paladin's own heals only (helpful spells and abilities, heal type, on hit)
DELETE FROM `spell_proc` WHERE `SpellId` = 1240330;
INSERT INTO `spell_proc` (`SpellId`, `SchoolMask`, `SpellFamilyName`, `SpellFamilyMask0`, `SpellFamilyMask1`, `SpellFamilyMask2`, `SpellFamilyMask3`,
`ProcFlags`, `ProcFlags2`, `SpellTypeMask`, `SpellPhaseMask`, `HitMask`, `AttributesMask`, `DisableEffectsMask`, `ProcsPerMinute`, `Chance`, `Cooldown`, `Charges`) VALUES
(1240330, 0, 0, 0, 0, 0, 0, 0x4400, 0, 0x2, 0x2, 0, 0, 0, 0, 100, 0, 0);

-- Injured Deathguard: stands around at half health and does not regenerate; Holy Light counts for the quest, and sometimes
-- they answer (sniff: 3 of 5 heals)
DELETE FROM `creature_text` WHERE `CreatureID` = 259377;
INSERT INTO `creature_text` (`CreatureID`, `GroupID`, `ID`, `Text`, `Type`, `Language`, `Probability`, `Emote`, `Duration`, `Sound`, `SoundPlayType`, `BroadcastTextId`, `TextRange`, `comment`) VALUES
(259377, 0, 0, 'I need no help from the light. Begone, paladin.', 12, 0, 100, 0, 0, 0, 0, 0, 0, 'Injured Deathguard - healed'),
(259377, 0, 1, 'I... thank you, paladin.', 12, 0, 100, 0, 0, 0, 0, 0, 0, 'Injured Deathguard - healed');

-- they kneel (sniff: StandState 8) and keep their aura 436207
INSERT INTO `creature_template_addon` (`entry`, `StandState`, `SheathState`, `auras`) VALUES (259377, 8, 1, '436207')
ON DUPLICATE KEY UPDATE `StandState` = VALUES(`StandState`);

UPDATE `creature_template` SET `AIName` = 'SmartAI', `ScriptName` = '' WHERE `entry` = 259377;
DELETE FROM `smart_scripts` WHERE `entryorguid` = 259377 AND `source_type` = 0;
INSERT INTO `smart_scripts` (`entryorguid`, `source_type`, `id`, `link`, `event_type`, `event_phase_mask`, `event_chance`, `event_flags`,
`event_param1`, `event_param2`, `event_param3`, `event_param4`, `action_type`, `action_param1`, `action_param2`, `action_param3`,
`target_type`, `target_param1`, `comment`) VALUES
(259377, 0, 0, 0, 37, 0, 100, 0, 0, 0, 0, 0, 102, 0, 0, 0, 1, 0, 'Injured Deathguard - On AI init - No health regeneration'),
(259377, 0, 1, 0, 25, 0, 100, 0, 0, 0, 0, 0, 142, 50, 0, 0, 1, 0, 'Injured Deathguard - On reset - Set health 50%'),
(259377, 0, 2, 0, 1, 0, 100, 0, 60000, 60000, 60000, 60000, 142, 50, 0, 0, 1, 0, 'Injured Deathguard - Out of combat every minute - Set health 50% again'),
(259377, 0, 3, 0, 8, 0, 100, 0, 635, 0, 0, 0, 33, 259377, 0, 0, 7, 0, 'Injured Deathguard - On Holy Light hit - Quest credit to the healer'),
(259377, 0, 4, 0, 8, 0, 60, 0, 635, 0, 0, 0, 1, 0, 0, 0, 7, 0, 'Injured Deathguard - On Holy Light hit - Say a line');

-- Coming to Terms (91208): the Frightened Paladin only talks to paladins on the quest. "Please don't do this..." makes her
-- refuse and attack; killing her completes the objective.
DELETE FROM `conditions` WHERE `SourceTypeOrReferenceId` = 15 AND `SourceGroup` IN (39645, 39644) AND `SourceEntry` = 0;
INSERT INTO `conditions` (`SourceTypeOrReferenceId`, `SourceGroup`, `SourceEntry`, `SourceId`, `ElseGroup`, `ConditionTypeOrReference`, `ConditionTarget`,
`ConditionValue1`, `ConditionValue2`, `ConditionValue3`, `NegativeCondition`, `Comment`) VALUES
(15, 39645, 0, 0, 0, 9, 0, 91208, 0, 0, 0, 'Frightened Paladin - greeting option: Coming to Terms taken'),
(15, 39644, 0, 0, 0, 9, 0, 91208, 0, 0, 0, 'Frightened Paladin - "Please don''t do this..." option: Coming to Terms taken');

DELETE FROM `creature_text` WHERE `CreatureID` = 246143;
INSERT INTO `creature_text` (`CreatureID`, `GroupID`, `ID`, `Text`, `Type`, `Language`, `Probability`, `Emote`, `Duration`, `Sound`, `SoundPlayType`, `BroadcastTextId`, `TextRange`, `comment`) VALUES
(246143, 0, 0, 'No! No, no, no! This can''t be. I''d rather be dead than Forsaken!', 12, 0, 100, 0, 0, 0, 0, 0, 0, 'Frightened Paladin - refuses'),
(246143, 1, 0, '%s raises her weapon, preparing to attack!', 16, 0, 100, 0, 0, 0, 0, 0, 0, 'Frightened Paladin - attacks');

-- she cowers in the hills (sniff: EmoteState 1088)
INSERT INTO `creature_template_addon` (`entry`, `emote`) VALUES (246143, 1088)
ON DUPLICATE KEY UPDATE `emote` = VALUES(`emote`);

UPDATE `creature_template` SET `AIName` = 'SmartAI', `ScriptName` = '' WHERE `entry` = 246143;
DELETE FROM `smart_scripts` WHERE `entryorguid` = 246143 AND `source_type` = 0;
INSERT INTO `smart_scripts` (`entryorguid`, `source_type`, `id`, `link`, `event_type`, `event_phase_mask`, `event_chance`, `event_flags`,
`event_param1`, `event_param2`, `event_param3`, `event_param4`, `action_type`, `action_param1`, `action_param2`, `action_param3`,
`target_type`, `target_param1`, `comment`) VALUES
(246143, 0, 0, 1, 62, 0, 100, 0, 39644, 0, 0, 0, 72, 0, 0, 0, 7, 0, 'Frightened Paladin - On "Please don''t do this..." - Close gossip'),
(246143, 0, 1, 2, 61, 0, 100, 0, 0, 0, 0, 0, 1, 0, 0, 0, 7, 0, 'Frightened Paladin - Linked - Say refusal'),
(246143, 0, 2, 3, 61, 0, 100, 0, 0, 0, 0, 0, 1, 1, 0, 0, 7, 0, 'Frightened Paladin - Linked - Emote raises her weapon'),
(246143, 0, 3, 4, 61, 0, 100, 0, 0, 0, 0, 0, 2, 14, 0, 0, 1, 0, 'Frightened Paladin - Linked - Turn hostile'),
(246143, 0, 4, 7, 61, 0, 100, 0, 0, 0, 0, 0, 49, 0, 0, 0, 7, 0, 'Frightened Paladin - Linked - Attack the player'),
(246143, 0, 5, 0, 4, 0, 100, 0, 0, 0, 0, 0, 11, 21084, 0, 0, 1, 0, 'Frightened Paladin - On aggro - Seal of Righteousness'),
(246143, 0, 6, 8, 7, 0, 100, 0, 0, 0, 0, 0, 2, 0, 0, 0, 1, 0, 'Frightened Paladin - On evade - Back to her own faction'),
(246143, 0, 7, 0, 61, 0, 100, 0, 0, 0, 0, 0, 17, 0, 0, 0, 1, 0, 'Frightened Paladin - Linked - Stop cowering'),
(246143, 0, 8, 0, 61, 0, 100, 0, 0, 0, 0, 0, 17, 1088, 0, 0, 1, 0, 'Frightened Paladin - Linked - Cower again');

-- A Light in the Darkness (98389): killing a Webbed Forsaken (271961) tears the web open and frees a Forsaken Adventurer
-- (271968, the summon of 1309702), who says one line and heads back to Deathknell. The webbed ones do not fight back.
DELETE FROM `creature_text` WHERE `CreatureID` = 271968;
INSERT INTO `creature_text` (`CreatureID`, `GroupID`, `ID`, `Text`, `Type`, `Language`, `Probability`, `Emote`, `Duration`, `Sound`, `SoundPlayType`, `BroadcastTextId`, `TextRange`, `comment`) VALUES
(271968, 0, 0, 'I was in over my head!', 12, 0, 100, 0, 0, 0, 0, 0, 0, 'Forsaken Adventurer - freed'),
(271968, 0, 1, 'It had to be spiders...', 12, 0, 100, 0, 0, 0, 0, 0, 0, 'Forsaken Adventurer - freed'),
(271968, 0, 2, 'Has anyone seen my weapon?', 12, 0, 100, 0, 0, 0, 0, 0, 0, 'Forsaken Adventurer - freed'),
(271968, 0, 3, 'Thank you. I''ll head back to Deathknell.', 12, 0, 100, 0, 0, 0, 0, 0, 0, 'Forsaken Adventurer - freed');

UPDATE `creature_template` SET `AIName` = 'SmartAI', `ScriptName` = '' WHERE `entry` IN (271961, 271968);
DELETE FROM `smart_scripts` WHERE `entryorguid` IN (271961, 271968) AND `source_type` = 0;
INSERT INTO `smart_scripts` (`entryorguid`, `source_type`, `id`, `link`, `event_type`, `event_phase_mask`, `event_chance`, `event_flags`,
`event_param1`, `event_param2`, `event_param3`, `event_param4`, `action_type`, `action_param1`, `action_param2`, `action_param3`,
`target_type`, `target_param1`, `comment`) VALUES
(271961, 0, 0, 0, 37, 0, 100, 0, 0, 0, 0, 0, 8, 0, 0, 0, 1, 0, 'Webbed Forsaken - On AI init - Passive'),
(271961, 0, 1, 0, 6, 0, 100, 0, 0, 0, 0, 0, 12, 271968, 3, 12000, 1, 0, 'Webbed Forsaken - On death - Free a Forsaken Adventurer'),
(271968, 0, 0, 1, 54, 0, 100, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 'Forsaken Adventurer - On summoned - Say a line'),
(271968, 0, 1, 0, 61, 0, 100, 0, 0, 0, 0, 0, 89, 10, 0, 0, 1, 0, 'Forsaken Adventurer - Linked - Wander off');
