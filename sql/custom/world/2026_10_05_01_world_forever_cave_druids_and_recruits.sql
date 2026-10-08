-- Re-run after the sniff files (2026_10_01_03/04) were regenerated: they rewrite the gossip and creatures this file adjusts (last: 2026-10-05, sniff of 2026-10-04 20:05).
-- Classic 1.60 (WoW Forever), official beta sniff 70205 of 2026-10-04. Safe to run again.

-- Quest 94489 (Elegael Thornpaw, Shendar cave): the injured druids have the spell click flag; clicking one makes the player cast
-- 1276047 on it (classic_spell_heal_injured_druid: "Injured Druids healed" 257964 x7 + the druids own hidden objective).
DELETE FROM `npc_spellclick_spells` WHERE `npc_entry` IN (258134,258137,258138,258275,258277,258288,258289) AND `spell_id` = 1276047;
INSERT INTO `npc_spellclick_spells` (`npc_entry`, `spell_id`, `cast_flags`, `user_type`) VALUES
(258134, 1276047, 1, 0),
(258137, 1276047, 1, 0),
(258138, 1276047, 1, 0),
(258275, 1276047, 1, 0),
(258277, 1276047, 1, 0),
(258288, 1276047, 1, 0),
(258289, 1276047, 1, 0);
DELETE FROM `spell_script_names` WHERE `spell_id` = 1276047;
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES (1276047, 'classic_spell_heal_injured_druid');

DELETE FROM `conditions` WHERE `SourceTypeOrReferenceId` = 18 AND `SourceGroup` IN (258134,258137,258138,258275,258277,258288,258289) AND `SourceEntry` = 1276047;
INSERT INTO `conditions` (`SourceTypeOrReferenceId`, `SourceGroup`, `SourceEntry`, `SourceId`, `ElseGroup`, `ConditionTypeOrReference`, `ConditionTarget`, `ConditionValue1`, `ConditionValue2`, `ConditionValue3`, `ConditionStringValue1`, `NegativeCondition`, `ErrorType`, `ErrorTextId`, `ScriptName`, `Comment`) VALUES
(18, 258134, 1276047, 0, 0, 47, 0, 94489, 8, 0, '', 0, 0, 0, '', 'Injured druid - heal while quest 94489 is in progress'),
(18, 258134, 1276047, 0, 0, 48, 0, 468887, 0, 0, '', 0, 0, 0, '', 'Injured druid - heal if this druid was not healed yet'),
(18, 258137, 1276047, 0, 0, 47, 0, 94489, 8, 0, '', 0, 0, 0, '', 'Injured druid - heal while quest 94489 is in progress'),
(18, 258137, 1276047, 0, 0, 48, 0, 468888, 0, 0, '', 0, 0, 0, '', 'Injured druid - heal if this druid was not healed yet'),
(18, 258138, 1276047, 0, 0, 47, 0, 94489, 8, 0, '', 0, 0, 0, '', 'Injured druid - heal while quest 94489 is in progress'),
(18, 258138, 1276047, 0, 0, 48, 0, 468889, 0, 0, '', 0, 0, 0, '', 'Injured druid - heal if this druid was not healed yet'),
(18, 258275, 1276047, 0, 0, 47, 0, 94489, 8, 0, '', 0, 0, 0, '', 'Injured druid - heal while quest 94489 is in progress'),
(18, 258275, 1276047, 0, 0, 48, 0, 468973, 0, 0, '', 0, 0, 0, '', 'Injured druid - heal if this druid was not healed yet'),
(18, 258277, 1276047, 0, 0, 47, 0, 94489, 8, 0, '', 0, 0, 0, '', 'Injured druid - heal while quest 94489 is in progress'),
(18, 258277, 1276047, 0, 0, 48, 0, 468974, 0, 0, '', 0, 0, 0, '', 'Injured druid - heal if this druid was not healed yet'),
(18, 258288, 1276047, 0, 0, 47, 0, 94489, 8, 0, '', 0, 0, 0, '', 'Injured druid - heal while quest 94489 is in progress'),
(18, 258288, 1276047, 0, 0, 48, 0, 468975, 0, 0, '', 0, 0, 0, '', 'Injured druid - heal if this druid was not healed yet'),
(18, 258289, 1276047, 0, 0, 47, 0, 94489, 8, 0, '', 0, 0, 0, '', 'Injured druid - heal while quest 94489 is in progress'),
(18, 258289, 1276047, 0, 0, 48, 0, 468976, 0, 0, '', 0, 0, 0, '', 'Injured druid - heal if this druid was not healed yet');

-- Gossip credits (the official server credited right after these options):
--   94489 Find Jorel Windsinger: corpse of Jorel (258130), menu 41923 "<Leave to tend to the other druids.>"
--   92640 Desperate Times: Ayessa (251968) 40961 and Elaadrin (252475) 40967 "Understood." (Valennia: see 2026_10_05_03)
-- The options that start these talks show only while the objective is open.
UPDATE `creature_template` SET `AIName` = 'SmartAI', `ScriptName` = '' WHERE `entry` IN (258130, 251968, 252475);
-- Valennia (252383) gives her credit in classic_npc_valennia_stormfist (2026_10_05_03), with the lines that follow it
DELETE FROM `smart_scripts` WHERE `entryorguid` IN (258130, 252383, 251968, 252475) AND `source_type` = 0;
INSERT INTO `smart_scripts` (`entryorguid`,`source_type`,`id`,`link`,`event_type`,`event_phase_mask`,`event_chance`,`event_flags`,`event_param1`,`event_param2`,`action_type`,`action_param1`,`target_type`,`comment`) VALUES
(258130,0,0,1,62,0,100,0,41923,0,33,258130,7,'Jorel Windsinger - On Gossip Select - Quest Credit Find Jorel Windsinger'),
(258130,0,1,0,61,0,100,0,0,0,72,0,7,'Jorel Windsinger - Linked - Close Gossip'),
(251968,0,0,1,62,0,100,0,40961,0,33,251968,7,'Ayessa Dawnsinger - On Gossip Select - Quest Credit Recruit the Windshapers'),
(251968,0,1,0,61,0,100,0,0,0,72,0,7,'Ayessa Dawnsinger - Linked - Close Gossip'),
(252475,0,0,1,62,0,100,0,40967,0,33,252475,7,'Elaadrin Evengale - On Gossip Select - Quest Credit Recruit the High Order'),
(252475,0,1,0,61,0,100,0,0,0,72,0,7,'Elaadrin Evengale - Linked - Close Gossip');

DELETE FROM `conditions` WHERE `SourceTypeOrReferenceId` = 15 AND ((`SourceGroup` = 41923 AND `SourceEntry` = 137859) OR (`SourceGroup` = 41313 AND `SourceEntry` = 137096)
    OR (`SourceGroup` = 40962 AND `SourceEntry` = 136544) OR (`SourceGroup` = 40964 AND `SourceEntry` = 136549));
INSERT INTO `conditions` (`SourceTypeOrReferenceId`, `SourceGroup`, `SourceEntry`, `SourceId`, `ElseGroup`, `ConditionTypeOrReference`, `ConditionTarget`, `ConditionValue1`, `ConditionValue2`, `ConditionValue3`, `ConditionStringValue1`, `NegativeCondition`, `ErrorType`, `ErrorTextId`, `ScriptName`, `Comment`) VALUES
(15, 41923, 137859, 0, 0, 47, 0, 94489, 8, 0, '', 0, 0, 0, '', 'Jorel Windsinger - show option while quest 94489 is in progress'),
(15, 41923, 137859, 0, 0, 48, 0, 468803, 0, 0, '', 0, 0, 0, '', 'Jorel Windsinger - show option if he was not found yet'),
(15, 41313, 137096, 0, 0, 47, 0, 92640, 8, 0, '', 0, 0, 0, '', 'Valennia Stormfist - show option while Desperate Times is in progress'),
(15, 41313, 137096, 0, 0, 48, 0, 465840, 0, 0, '', 0, 0, 0, '', 'Valennia Stormfist - show option if she was not spoken to yet'),
(15, 40962, 136544, 0, 0, 47, 0, 92640, 8, 0, '', 0, 0, 0, '', 'Ayessa Dawnsinger - show option while Desperate Times is in progress'),
(15, 40962, 136544, 0, 0, 48, 0, 465809, 0, 0, '', 0, 0, 0, '', 'Ayessa Dawnsinger - show option if the Windshapers were not recruited yet'),
(15, 40964, 136549, 0, 0, 47, 0, 92640, 8, 0, '', 0, 0, 0, '', 'Elaadrin Evengale - show option while Desperate Times is in progress'),
(15, 40964, 136549, 0, 0, 48, 0, 465810, 0, 0, '', 0, 0, 0, '', 'Elaadrin Evengale - show option if the High Order was not recruited yet');
