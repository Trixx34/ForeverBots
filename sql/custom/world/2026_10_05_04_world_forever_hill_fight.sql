-- Re-run after the sniff files (2026_10_01_03/04, 2026_10_02_00) were regenerated: they rewrite the auras and AI this file adjusts (last: 2026-10-05, sniff of 2026-10-04 20:05).
-- Classic 1.60 (WoW Forever): the AlAketh assault on the Peacekeepers hill (official beta sniffs 70205: 74 of 94 attackers 253782 ran
-- from 2400..2470 x 470..500 up to the Peacekeepers 253781 at 2410..2470 x 410..430 and fought them). The sniff import had made
-- them and the Valanaar Peacekeepers (253474) lie on the ground: template auras taken from corpses and an out of combat
-- self cast of 1288546 every second. Safe to run again.
UPDATE `creature_template_addon` SET `auras` = NULL WHERE `entry` IN (253474, 253782);

DELETE FROM `smart_scripts` WHERE `entryorguid` = 253474 AND `source_type` = 0;
UPDATE `creature_template` SET `AIName` = '' WHERE `entry` = 253474;

-- attackers: Shock in combat (sniff), out of combat they charge the nearest Peacekeeper of the hill
UPDATE `creature_template` SET `AIName` = 'SmartAI', `ScriptName` = '' WHERE `entry` = 253782;
DELETE FROM `smart_scripts` WHERE `entryorguid` = 253782 AND `source_type` = 0;
INSERT INTO `smart_scripts` (`entryorguid`,`source_type`,`id`,`link`,`event_type`,`event_phase_mask`,`event_chance`,`event_flags`,`event_param1`,`event_param2`,`event_param3`,`event_param4`,`action_type`,`action_param1`,`target_type`,`target_param1`,`target_param2`,`comment`) VALUES
(253782,0,0,0,0,0,100,0,1200,2100,7300,12100,11,1259652,2,0,0,'Al''Aketh Attacker - In Combat - Cast Shock on Victim'),
(253782,0,1,0,1,0,100,0,2000,4000,3000,5000,49,0,19,253781,120,'Al''Aketh Attacker - Out of Combat - Attack the nearest Peacekeeper of the hill');
UPDATE `creature` SET `spawntimesecs` = 30, `wander_distance` = 0, `MovementType` = 0 WHERE `map` = 2991 AND `id` = 253782;
