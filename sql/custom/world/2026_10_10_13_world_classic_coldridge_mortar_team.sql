-- Classic 1.60: Dwarven Mortar Team event at Coldridge Valley (Angus 10610, Shorty 10611, Rifleman Wheeler 10803, Rifleman Middlecamp
-- 10804, Spotter Klemmy 10805; action list 1061000 drives it with Set Data). Nothing started it: the VMaNGOS import replaced the
-- script of Hegnar Rumbleshot 1243 (the starter in TrinityCore / AzerothCore: Set Data 1 to the five on Ammo for Rumbleshot 5541)
-- with a converted copy of the whole event (orientations lost). Hegnar starts it again when Ammo for Rumbleshot 5541 is turned in.
DELETE FROM `smart_scripts` WHERE `entryorguid` = 1243 AND `source_type` = 0;
DELETE FROM `smart_scripts` WHERE `entryorguid` = 124300 AND `source_type` = 9;
INSERT INTO `smart_scripts` (`entryorguid`,`source_type`,`id`,`link`,`Difficulties`,`event_type`,`event_phase_mask`,`event_chance`,`event_flags`,`event_param1`,`event_param2`,`event_param3`,`event_param4`,`event_param5`,`event_param_string`,`action_type`,`action_param1`,`action_param2`,`action_param3`,`action_param4`,`action_param5`,`action_param6`,`action_param7`,`action_param_string`,`target_type`,`target_param1`,`target_param2`,`target_param3`,`target_param4`,`target_param_string`,`target_x`,`target_y`,`target_z`,`target_o`,`comment`) VALUES
(1243,0,0,0,'',20,0,100,0,5541,0,0,0,0,'',80,124300,2,0,0,0,0,0,'',1,0,0,0,0,'',0,0,0,0,'Hegnar Rumbleshot - On Quest Ammo for Rumbleshot Finished - Start Mortar Team event'),
(1243,0,1,0,'',38,0,100,0,2,0,0,0,0,'',66,0,0,0,0,0,0,0,'',8,0,0,0,0,'',0,0,0,3.68522,'Hegnar Rumbleshot - On Data Set 2 0 - Set Orientation 3.68522'),
(1243,0,2,0,'',38,0,100,0,3,0,0,0,0,'',66,0,0,0,0,0,0,0,'',8,0,0,0,0,'',0,0,0,0.0349066,'Hegnar Rumbleshot - On Data Set 3 0 - Set Orientation 0.0349066'),
(124300,9,0,0,'',0,0,100,0,0,0,0,0,0,'',45,1,0,0,0,0,0,0,'',19,10610,20,0,0,'',0,0,0,0,'Hegnar Rumbleshot - Mortar Team event - Set Data 1 0 Angus'),
(124300,9,1,0,'',0,0,100,0,0,0,0,0,0,'',45,1,0,0,0,0,0,0,'',19,10611,20,0,0,'',0,0,0,0,'Hegnar Rumbleshot - Mortar Team event - Set Data 1 0 Shorty'),
(124300,9,2,0,'',0,0,100,0,0,0,0,0,0,'',45,1,0,0,0,0,0,0,'',19,10804,20,0,0,'',0,0,0,0,'Hegnar Rumbleshot - Mortar Team event - Set Data 1 0 Middlecamp'),
(124300,9,3,0,'',0,0,100,0,0,0,0,0,0,'',45,1,0,0,0,0,0,0,'',19,10805,20,0,0,'',0,0,0,0,'Hegnar Rumbleshot - Mortar Team event - Set Data 1 0 Klemmy'),
(124300,9,4,0,'',0,0,100,0,0,0,0,0,0,'',45,1,0,0,0,0,0,0,'',19,10803,20,0,0,'',0,0,0,0,'Hegnar Rumbleshot - Mortar Team event - Set Data 1 0 Wheeler');
