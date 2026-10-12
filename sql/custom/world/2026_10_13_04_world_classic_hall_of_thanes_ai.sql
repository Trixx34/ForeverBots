-- Classic 1.60: The Hall of Thanes (map 3065) creature AI and boss lines from the official sniff of 2026-10-12
-- (gen_hall_of_thanes.py). Timings: first cast after the attack start and repeat gap as seen (median +-25%); fights were short, so
-- a spell cast once per fight gets a repeat chosen from the spell (noted per row). Not in the sniff: Plunder Charge (22911) and
-- Durgen Intimidating Shout 29544 (listed by Wowhead), the health at which looters flee (15% used). Safe to run again.

-- creatures added by sniff_import_dungeon.py that need more than the generic import
-- Poisoned Blades (1308473, procs 1308474 seen 22 times) is on every Dark Iron Looter next to the scale aura (the generic aura
-- import reads only the first aura of an update)
UPDATE `creature_template_addon` SET `auras` = '1264628 1308473' WHERE `entry` = 263396;
-- the Fiery Assistant is a summon (never a placed spawn): its level as seen
REPLACE INTO `creature_classic_level` (`entry`,`level_min`,`level_max`) VALUES (263467,14,15);
-- Ancient Switch: opens the two Iron Doors back to the entrance (instance script)
UPDATE `gameobject_template` SET `ScriptName` = 'go_hall_of_thanes_ancient_switch' WHERE `entry` = 673473;

UPDATE `creature_template` SET `AIName` = 'SmartAI', `ScriptName` = '' WHERE `entry` IN (261306,261311,261316,261319,263389,263390,263396,263397,263438,263466,263467,263470,263980);
DELETE FROM `smart_scripts` WHERE `source_type` = 0 AND `entryorguid` IN (261306,261311,261316,261319,263389,263390,263396,263397,263438,263466,263467,263470,263980);
INSERT INTO `smart_scripts` (`entryorguid`,`source_type`,`id`,`link`,`Difficulties`,`event_type`,`event_phase_mask`,`event_chance`,`event_flags`,`event_param1`,`event_param2`,`event_param3`,`event_param4`,`event_param5`,`event_param_string`,`action_type`,`action_param1`,`action_param2`,`action_param3`,`action_param4`,`action_param5`,`action_param6`,`action_param7`,`action_param_string`,`target_type`,`target_param1`,`target_param2`,`target_param3`,`target_param4`,`target_param_string`,`target_x`,`target_y`,`target_z`,`target_o`,`comment`) VALUES
(261306,0,0,0,'',0,0,100,0,2100,3500,25000,30000,0,'',11,1292602,0,0,0,0,0,0,'',2,0,0,0,0,'',0,0,0,0,'Faldrim Anvilmar - In Combat - Cast 1292602 (Anvilmar''s Curse on the tank) - sniff: 1 casts, first after (2100, 3500) ms, repeat not seen (25000-30000) ms'),
(261306,0,1,0,'',0,0,100,0,4900,8100,10000,15000,0,'',11,13860,0,0,0,0,0,0,'',2,0,0,0,0,'',0,0,0,0,'Faldrim Anvilmar - In Combat - Cast 13860 (Mind Blast) - sniff: 1 casts, first after (4900, 8100) ms, repeat not seen (10000-15000) ms'),
(261306,0,2,0,'',4,0,100,0,0,0,0,0,0,'',1,0,0,0,0,0,0,0,'',1,0,0,0,0,'',0,0,0,0,'Faldrim Anvilmar - On Aggro - Say Line 0'),
(261306,0,3,0,'',6,0,100,0,0,0,0,0,0,'',1,1,0,0,0,0,0,0,'',1,0,0,0,0,'',0,0,0,0,'Faldrim Anvilmar - On Death - Say Line 1'),
(261311,0,0,0,'',0,0,100,0,7200,12000,10000,15000,0,'',11,21055,0,0,0,0,0,0,'',2,0,0,0,0,'',0,0,0,0,'Plunder - In Combat - Cast 21055 (Crush Armor) - sniff: 1 casts, first after (7200, 12000) ms, repeat not seen (10000-15000) ms'),
(261311,0,1,0,'',0,0,100,0,8100,13500,10000,15000,0,'',11,11130,0,0,0,0,0,0,'',1,0,0,0,0,'',0,0,0,0,'Plunder - In Combat - Cast 11130 (Knock Away (no unit target: area around him)) - sniff: 1 casts, first after (8100, 13500) ms, repeat not seen (10000-15000) ms'),
(261311,0,2,0,'',4,0,100,0,0,0,0,0,0,'',1,0,0,0,0,0,0,0,'',1,0,0,0,0,'',0,0,0,0,'Plunder - On Aggro - Say Line 0'),
(261316,0,0,0,'',0,0,100,0,3600,5900,10000,15000,0,'',11,11970,0,0,0,0,0,0,'',1,0,0,0,0,'',0,0,0,0,'Magmatus - In Combat - Cast 11970 (Fire Nova (area around him)) - sniff: 1 casts, first after (3600, 5900) ms, repeat not seen (10000-15000) ms'),
(261316,0,1,0,'',0,0,100,0,8100,13600,10000,15000,0,'',11,1293228,0,0,0,0,0,0,'',5,0,0,0,0,'',0,0,0,0,'Magmatus - In Combat - Cast 1293228 (Combust (3 s cast, a random player)) - sniff: 1 casts, first after (8100, 13600) ms, repeat not seen (10000-15000) ms'),
(261316,0,2,0,'',4,0,100,0,0,0,0,0,0,'',1,0,0,0,0,0,0,0,'',1,0,0,0,0,'',0,0,0,0,'Magmatus - On Aggro - Say Line 0'),
(261319,0,0,0,'',0,0,100,0,8300,13800,15000,18000,0,'',11,11977,0,0,0,0,0,0,'',2,0,0,0,0,'',0,0,0,0,'Durgen Dirgehammer - In Combat - Cast 11977 (Rend) - sniff: 1 casts, first after (8300, 13800) ms, repeat not seen (15000-18000) ms'),
(261319,0,1,0,'',0,0,100,0,9200,15300,10000,15000,0,'',11,25710,0,0,0,0,0,0,'',2,0,0,0,0,'',0,0,0,0,'Durgen Dirgehammer - In Combat - Cast 25710 (Heroic Strike) - sniff: 1 casts, first after (9200, 15300) ms, repeat not seen (10000-15000) ms'),
(261319,0,2,0,'',0,0,100,0,11900,19900,20000,25000,0,'',11,19134,0,0,0,0,0,0,'',2,0,0,0,0,'',0,0,0,0,'Durgen Dirgehammer - In Combat - Cast 19134 (Intimidating Shout) - sniff: 1 casts, first after (11900, 19900) ms, repeat not seen (20000-25000) ms'),
(261319,0,3,0,'',4,0,100,0,0,0,0,0,0,'',1,0,0,0,0,0,0,0,'',1,0,0,0,0,'',0,0,0,0,'Durgen Dirgehammer - On Aggro - Say Line 0'),
(261319,0,4,0,'',6,0,100,0,0,0,0,0,0,'',1,1,0,0,0,0,0,0,'',1,0,0,0,0,'',0,0,0,0,'Durgen Dirgehammer - On Death - Say Line 1'),
(263389,0,0,0,'',0,0,100,0,4900,8100,10000,15000,0,'',11,11971,0,0,0,0,0,0,'',2,0,0,0,0,'',0,0,0,0,'Enraged Apparition - In Combat - Cast 11971 (Sunder Armor) - sniff: 11 casts, first after (4900, 8100) ms, repeat not seen (10000-15000) ms'),
(263390,0,0,0,'',0,0,100,0,6500,10800,10000,15000,0,'',11,11639,0,0,0,0,0,0,'',2,0,0,0,0,'',0,0,0,0,'Tormented Soul - In Combat - Cast 11639 (Shadow Word: Pain) - sniff: 6 casts, first after (6500, 10800) ms, repeat not seen (10000-15000) ms'),
(263396,0,0,0,'',0,0,100,0,5200,8600,4700,7800,0,'',11,14873,0,0,0,0,0,0,'',2,0,0,0,0,'',0,0,0,0,'Dark Iron Looter - In Combat - Cast 14873 (Sinister Strike) - sniff: 28 casts, first after (5200, 8600) ms, repeat (4700, 7800) ms'),
(263396,0,1,0,'',2,0,100,1,0,15,0,0,0,'',25,1,0,0,0,0,0,0,'',1,0,0,0,0,'',0,0,0,0,'Dark Iron Looter - Between 0-15% Health - Flee For Assist (sniff: attempts to run away in fear, 8 times; the health it starts at is not in the sniff)'),
(263397,0,0,0,'',0,0,100,0,2800,4600,1800,3000,0,'',11,1294260,0,0,0,0,0,0,'',2,0,0,0,0,'',0,0,0,0,'Dark Iron Engineer - In Combat - Cast 1294260 (Throw Dynamite (2 s cast, at the target position, cast again and again)) - sniff: 39 casts, first after (2800, 4600) ms, repeat (1800, 3000) ms'),
(263438,0,0,0,'',0,0,100,0,0,0,2700,4600,0,'',11,20793,0,0,0,0,0,0,'',2,0,0,0,0,'',0,0,0,0,'Dark Iron Summoner - In Combat - Cast 20793 (Fireball (3 s cast, cast again and again)) - sniff: 14 casts, first after (0, 0) ms, repeat (2700, 4600) ms'),
(263438,0,1,0,'',0,0,100,0,8100,13500,30000,35000,0,'',11,1291218,0,0,0,0,0,0,'',1,0,0,0,0,'',0,0,0,0,'Dark Iron Summoner - In Combat - Cast 1291218 (Summon Fiery Assistant (4 s cast)) - sniff: 2 casts, first after (8100, 13500) ms, repeat not seen (30000-35000) ms'),
(263466,0,0,0,'',0,0,100,0,5900,9900,11000,18300,0,'',11,5568,0,0,0,0,0,0,'',1,0,0,0,0,'',0,0,0,0,'Lesser Stone Golem - In Combat - Cast 5568 (Trample (area around it)) - sniff: 8 casts, first after (5900, 9900) ms, repeat (11000, 18300) ms'),
(263467,0,0,0,'',54,0,100,0,0,0,0,0,0,'',11,1291225,2,0,0,0,0,0,'',1,0,0,0,0,'',0,0,0,0,'Fiery Assistant - Just Summoned - Cast 1291225 (Spawn Cosmetics)'),
(263467,0,1,0,'',54,0,100,0,0,0,0,0,0,'',49,0,0,0,0,0,0,0,'',21,0,0,0,0,'',0,0,0,0,'Fiery Assistant - Just Summoned - Attack (sniff: attacks at once)'),
(263470,0,0,0,'',0,0,100,0,2700,4500,10000,15000,0,'',11,3427,0,0,0,0,0,0,'',2,0,0,0,0,'',0,0,0,0,'Bloodhound Runt - In Combat - Cast 3427 (Infected Wound) - sniff: 3 casts, first after (2700, 4500) ms, repeat not seen (10000-15000) ms'),
(263980,0,0,0,'',25,0,100,0,0,0,0,0,0,'',8,0,0,0,0,0,0,0,'',1,0,0,0,0,'',0,0,0,0,'Seaforium Blasting Powder - On Reset - Passive (a keg: does not fight back)'),
(263980,0,1,0,'',25,0,100,0,0,0,0,0,0,'',103,1,0,0,0,0,0,0,'',1,0,0,0,0,'',0,0,0,0,'Seaforium Blasting Powder - On Reset - Rooted'),
(263980,0,2,0,'',6,0,100,0,0,0,0,0,0,'',11,1291346,2,0,0,0,0,0,'',1,0,0,0,0,'',0,0,0,0,'Seaforium Blasting Powder - On Death - Cast 1291346 (explodes: sniff, at the moment it was killed)');

-- boss lines (yells) with their voice: sound kit from SMSG_PLAY_SOUND (the server has no broadcast text for them)
DELETE FROM `creature_text` WHERE `CreatureID` IN (261306,261311,261316,261319);
INSERT INTO `creature_text` (`CreatureID`,`GroupID`,`ID`,`Text`,`Type`,`Language`,`Probability`,`Emote`,`Duration`,`Sound`,`SoundPlayType`,`BroadcastTextId`,`TextRange`,`comment`) VALUES
(261306,0,0,'Defilers! Miscreants! Leave this place!',14,0,100,0,0,340212,0,0,0,'Faldrim Anvilmar - aggro (sniff; official broadcast text 310791)'),
(261306,1,0,'I return... to my... rest.',14,0,100,0,0,340226,0,0,0,'Faldrim Anvilmar - death (sniff; official broadcast text 310794)'),
(261311,0,0,'Loot acquisition protocol engaged.',14,0,100,0,0,340209,0,0,0,'Plunder - aggro (sniff; official broadcast text 310793)'),
(261316,0,0,'Flame consumes all!',14,0,100,0,0,340200,0,0,0,'Magmatus - aggro (sniff; official broadcast text 310795)'),
(261319,0,0,'Ye think you can stop the greatest heist in history?',14,0,100,0,0,340210,0,0,0,'Durgen Dirgehammer - aggro (sniff; official broadcast text 310796)'),
(261319,1,0,'I nearly... got away... with it.',14,0,100,0,0,340215,0,0,0,'Durgen Dirgehammer - death (sniff; official broadcast text 310797)');
