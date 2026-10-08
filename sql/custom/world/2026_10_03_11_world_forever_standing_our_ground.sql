-- Re-run after the sniff files (2026_10_01_03/04, 2026_10_02_00/06, 2026_10_03_00) were regenerated: they rewrite rows this file adjusts (last: 2026-10-05, sniff of 2026-10-04 20:05).
-- Classic 1.60 (WoW Forever), Zephras Isle orchard: quest Standing Our Ground (92693). "Im ready to fight, Aamelia." (gossip 40780,
-- option 136302) starts the Ferauu event (classic_npc_aamelia_windfield + classic_npc_malfunctioning_cyclone_construct, official beta
-- sniff 70205). The sniff AI import had her casting Make Your Stand on a timer: that cast belongs to the end of the event. The option
-- shows only while "Speak with Aamelia Windfield" (objective 474454) is open. Safe to run again.
UPDATE `creature_template` SET `AIName` = '', `ScriptName` = 'classic_npc_aamelia_windfield' WHERE `entry` = 252800;
DELETE FROM `smart_scripts` WHERE `entryorguid` = 252800 AND `source_type` = 0;

DELETE FROM `conditions` WHERE `SourceTypeOrReferenceId` = 15 AND `SourceGroup` = 40780 AND `SourceEntry` = 136302;
INSERT INTO `conditions` (`SourceTypeOrReferenceId`, `SourceGroup`, `SourceEntry`, `SourceId`, `ElseGroup`, `ConditionTypeOrReference`, `ConditionTarget`, `ConditionValue1`, `ConditionValue2`, `ConditionValue3`, `ConditionStringValue1`, `NegativeCondition`, `ErrorType`, `ErrorTextId`, `ScriptName`, `Comment`) VALUES
(15, 40780, 136302, 0, 0, 47, 0, 92693, 8, 0, '', 0, 0, 0, '', 'Aamelia Windfield - show gossip option if Standing Our Ground is in progress'),
(15, 40780, 136302, 0, 0, 48, 0, 474454, 0, 0, '', 0, 0, 0, '', 'Aamelia Windfield - show gossip option if she was not spoken to yet');
