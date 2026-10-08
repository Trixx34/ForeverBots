-- Re-run after the sniff files (2026_10_01_03/04, 2026_10_02_00/06, 2026_10_03_00) were regenerated: they rewrite rows this file adjusts (last: 2026-10-05, sniff of 2026-10-04 20:05).
-- Classic 1.60: Halaan Hawk-Eye (257554), "The Anchors of Zephras" (94414, objective "View the Anchor Pylon"): the gossip option
-- "Halaan, please lend me your gift." shows only while the quest is in the log, and choosing it makes the player cast Halaans Gift
-- (1272014) on themselves, which views the pylon and credits the objective (ymir sniff of the official beta: gossip select menu 41842
-- option 137720 -> player casts 1272014 -> criteria update and quest credit).
DELETE FROM `conditions` WHERE `SourceTypeOrReferenceId` = 15 AND `SourceGroup` = 41842 AND `SourceEntry` = 0;
INSERT INTO `conditions` (`SourceTypeOrReferenceId`,`SourceGroup`,`SourceEntry`,`SourceId`,`ElseGroup`,`ConditionTypeOrReference`,`ConditionTarget`,
`ConditionValue1`,`ConditionValue2`,`ConditionValue3`,`NegativeCondition`,`ErrorType`,`ErrorTextId`,`ScriptName`,`Comment`) VALUES
(15,41842,0,0,0,9,0,94414,0,0,0,0,0,'','Halaan Hawk-Eye - Show gossip option only while The Anchors of Zephras is taken');

UPDATE `creature_template` SET `AIName` = 'SmartAI' WHERE `entry` = 257554;
DELETE FROM `smart_scripts` WHERE `entryorguid` = 257554 AND `source_type` = 0;
INSERT INTO `smart_scripts` (`entryorguid`,`source_type`,`id`,`link`,`event_type`,`event_phase_mask`,`event_chance`,`event_flags`,`event_param1`,
`event_param2`,`action_type`,`action_param1`,`action_param2`,`target_type`,`comment`) VALUES
(257554,0,0,1,62,0,100,0,41842,0,85,1272014,0,7,'Halaan Hawk-Eye - On Gossip Option 0 Selected - Invoker Casts Halaan''s Gift'),
(257554,0,1,0,61,0,100,0,0,0,72,0,0,7,'Halaan Hawk-Eye - Linked - Close Gossip');
