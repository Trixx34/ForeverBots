-- Classic 1.60 (WoW Forever), Gustberry Lowlands: quest A Firm Response (93746). "Belathaan! The Windshapers demand to know..."
-- (gossip 41553, option 137326) starts the Living Storm scene (classic_npc_belathaan_brightwish, official beta sniff 70205): High
-- Priestess Lorthuna and two storms come down next to him, the storms strike him ~31 s later (credit "Confront Belathaan Brightwish",
-- objective 467315) and attack the player. The option shows only while that objective is open. He was back ~90 s after the strike.
-- Safe to run again; re-run after the sniff files (2026_10_01_03/04, 2026_10_02_00) were regenerated (last: 2026-10-05, sniff of 2026-10-04 20:05).
UPDATE `creature_template` SET `AIName` = '', `ScriptName` = 'classic_npc_belathaan_brightwish' WHERE `entry` = 256247;
DELETE FROM `smart_scripts` WHERE `entryorguid` = 256247 AND `source_type` = 0;
UPDATE `creature` SET `spawntimesecs` = 90 WHERE `id` = 256247;

-- Their talk while the storms wait (official sniff 70205: 17.9, 23.4, 32.4 and 36.2 s after the gossip)
DELETE FROM `creature_text` WHERE `CreatureID` IN (256247, 256249);
INSERT INTO `creature_text` (`CreatureID`, `GroupID`, `ID`, `Text`, `Type`, `Language`, `Probability`, `Emote`, `Duration`, `Sound`, `SoundPlayType`, `BroadcastTextId`, `TextRange`, `comment`) VALUES
(256247, 0, 0, 'No, no! Lorthuna, you do not understand. This windshaper is merely passing by. They did not come here with me!', 12, 0, 100, 0, 0, 0, 0, 304794, 0, 'Belathaan Brightwish - A Firm Response'),
(256247, 1, 0, 'Lorthuna, be reasonable. Elaadrin sent me to discuss a path to peace and...', 12, 0, 100, 0, 0, 0, 0, 304796, 0, 'Belathaan Brightwish - A Firm Response'),
(256249, 0, 0, 'It matters not. I think that I''ll just have my pets kill the both of you and send your tongue back to Elaadrin as a warning to keep his little mages in Valanaar, where they belong.', 12, 0, 100, 0, 0, 0, 0, 304795, 0, 'High Priestess Lorthuna - A Firm Response'),
(256249, 1, 0, 'Enough. Goodbye, Belathaan and... whomever you are.', 12, 0, 100, 0, 0, 0, 0, 304797, 0, 'High Priestess Lorthuna - A Firm Response');

-- Lorthuna and the storms are summoned by the script, the sniff import had spawned the ones it saw
DELETE FROM `creature` WHERE `id` IN (256249, 256250) AND `map` = 2991 AND `position_x` BETWEEN 2840 AND 2870 AND `position_y` BETWEEN 880 AND 910;

-- The Generic Bunnys sniff AI cast Belathaans kill credit (1268651) on every reset, everywhere the bunny stands
UPDATE `creature_template` SET `AIName` = '' WHERE `entry` = 270313;
DELETE FROM `smart_scripts` WHERE `entryorguid` = 270313 AND `source_type` = 0;

DELETE FROM `conditions` WHERE `SourceTypeOrReferenceId` = 15 AND `SourceGroup` = 41553 AND `SourceEntry` = 137326;
INSERT INTO `conditions` (`SourceTypeOrReferenceId`, `SourceGroup`, `SourceEntry`, `SourceId`, `ElseGroup`, `ConditionTypeOrReference`, `ConditionTarget`, `ConditionValue1`, `ConditionValue2`, `ConditionValue3`, `ConditionStringValue1`, `NegativeCondition`, `ErrorType`, `ErrorTextId`, `ScriptName`, `Comment`) VALUES
(15, 41553, 137326, 0, 0, 47, 0, 93746, 8, 0, '', 0, 0, 0, '', 'Belathaan Brightwish - show gossip option if A Firm Response is in progress'),
(15, 41553, 137326, 0, 0, 48, 0, 467315, 0, 0, '', 0, 0, 0, '', 'Belathaan Brightwish - show gossip option if he was not confronted yet');

-- The storms spells are in the 70205 client data (Lightning Strike 1268650, Shock 1259652, Lightning Cloud 1269323, credit 1268651):
-- the server side copies an earlier version of this file added are not needed (the server refused them anyway).
DELETE FROM `serverside_spell` WHERE `Id` IN (1268650, 1268651, 1259652, 1269323);
DELETE FROM `serverside_spell_effect` WHERE `SpellID` IN (1268650, 1268651, 1259652, 1269323);

-- Lightning Strike hits the nearby entry: Belathaan
DELETE FROM `conditions` WHERE `SourceTypeOrReferenceId` = 13 AND `SourceEntry` = 1268650;
INSERT INTO `conditions` (`SourceTypeOrReferenceId`, `SourceGroup`, `SourceEntry`, `SourceId`, `ElseGroup`, `ConditionTypeOrReference`, `ConditionTarget`, `ConditionValue1`, `ConditionValue2`, `ConditionValue3`, `ConditionStringValue1`, `NegativeCondition`, `ErrorType`, `ErrorTextId`, `ScriptName`, `Comment`) VALUES
(13, 1, 1268650, 0, 0, 31, 0, 3, 256247, 0, '', 0, 0, 0, '', 'Lightning Strike - target Belathaan Brightwish');

-- Living Storm: Shock every 6-9 s, Lightning Cloud every 12-16 s in combat (official sniff 70205)
UPDATE `creature_template` SET `AIName` = 'SmartAI', `ScriptName` = '' WHERE `entry` = 256250;
DELETE FROM `smart_scripts` WHERE `entryorguid` = 256250 AND `source_type` = 0;
INSERT INTO `smart_scripts` (`entryorguid`,`source_type`,`id`,`link`,`event_type`,`event_phase_mask`,`event_chance`,`event_flags`,`event_param1`,`event_param2`,`event_param3`,`event_param4`,`action_type`,`action_param1`,`action_param2`,`target_type`,`comment`) VALUES
(256250,0,0,0,0,0,100,0,2000,4000,6000,9000,11,1259652,0,2,'Living Storm - In Combat - Cast Shock on Victim'),
(256250,0,1,0,0,0,100,0,6000,8000,12000,16000,11,1269323,0,2,'Living Storm - In Combat - Cast Lightning Cloud on Victim');
