-- Classic 1.60: Negolash event (quest 619 Enticing Negolash at the Ruined Lifeboat, Stranglethorn Vale)
-- Ported from VMaNGOS quest_end_scripts 619, generic_scripts 1494, creature_movement_template 1494, creature_template 1494.
-- Problems fixed:
--  * creature_template_difficulty.LootID was 0, so Negolash never dropped Smotts Cutlass (3935) for quest 618/8554 Facing Negolash
--  * no gold
--  * he was summoned next to the boat and attacked the quest completer at once; vanilla: he rises from the sea,
--    walks to the feast (Mmmh...I SMELL FOOD! / AH, A FEAST!) and despawns after 2 minutes out of combat
--  * the feast gameobjects (Barbequed Buzzard Wings, Stranglevine Wine, Baked Bread) are spawntimesecs -120
--    (not spawned by default) and nothing ever showed them
--  * Knockdown: vanilla uses the passive proc aura 7095 (Knockdown Proc, 15 percent on melee) instead of a timer

-- Loot and gold (VMaNGOS loot_id 1494, gold 143..713)
UPDATE `creature_template_difficulty` SET `LootID`=1494, `GoldMin`=143, `GoldMax`=713 WHERE `Entry`=1494 AND `DifficultyID`=0;

-- Knockdown Proc aura
DELETE FROM `creature_template_addon` WHERE `entry`=1494;
INSERT INTO `creature_template_addon` (`entry`,`SheathState`,`auras`) VALUES
(1494,1,'7095');

-- Second line of text (group 0 already exists: broadcast 731)
DELETE FROM `creature_text` WHERE `CreatureID`=1494 AND `GroupID`=1;
INSERT INTO `creature_text` (`CreatureID`,`GroupID`,`ID`,`Text`,`Type`,`Language`,`Probability`,`Emote`,`Duration`,`Sound`,`SoundPlayType`,`BroadcastTextId`,`TextRange`,`comment`) VALUES
(1494,1,0,'AH, A FEAST!  WHO LEFT THIS HERE...?',14,0,100,0,0,0,0,763,0,'Negolash - feast reached');

-- Walk from the sea to the feast (VMaNGOS creature_movement_template 1494, points 1..6, last point only turns)
DELETE FROM `waypoint_path` WHERE `PathId`=149400;
INSERT INTO `waypoint_path` (`PathId`,`MoveType`,`Flags`,`Velocity`,`Comment`) VALUES
(149400,0,0,NULL,'Negolash - sea to feast');
DELETE FROM `waypoint_path_node` WHERE `PathId`=149400;
INSERT INTO `waypoint_path_node` (`PathId`,`NodeId`,`PositionX`,`PositionY`,`PositionZ`,`Orientation`,`Delay`) VALUES
(149400,1,-14598.6,76.0563,-11.249,NULL,0),
(149400,2,-14603.4,87.1779,-10.2403,NULL,0),
(149400,3,-14612.9,104.961,-8.73655,NULL,0),
(149400,4,-14625.2,125.225,-5.16936,NULL,0),
(149400,5,-14647.2,142.49,0.734623,1.42129,1000);

-- Negolash SmartAI (old 5-8s Knockdown timer replaced by the proc aura above)
DELETE FROM `smart_scripts` WHERE `entryorguid`=1494 AND `source_type`=0;
INSERT INTO `smart_scripts` (`entryorguid`,`source_type`,`id`,`link`,`event_type`,`event_phase_mask`,`event_chance`,`event_flags`,`event_param1`,`event_param2`,`event_param3`,`event_param4`,`event_param5`,`action_type`,`action_param1`,`action_param2`,`action_param3`,`action_param4`,`action_param5`,`action_param6`,`target_type`,`target_param1`,`target_param2`,`target_param3`,`target_x`,`target_y`,`target_z`,`target_o`,`comment`) VALUES
(1494,0,0,1,54,0,100,0,0,0,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,'Negolash - On Just Summoned - Say Line 0'),
(1494,0,1,0,61,0,100,0,0,0,0,0,0,53,0,149400,0,0,0,0,1,0,0,0,0,0,0,0,'Negolash - On Just Summoned - Start Waypoint Path 149400'),
(1494,0,2,0,40,0,100,0,5,149400,0,0,0,1,1,0,0,0,0,0,1,0,0,0,0,0,0,0,'Negolash - On Waypoint 5 Reached - Say Line 1');

-- Ruined Lifeboat: summon Negolash in the sea (no attack, 2 min out of combat despawn) and show the feast for 120s
DELETE FROM `smart_scripts` WHERE `entryorguid`=2289 AND `source_type`=1;
INSERT INTO `smart_scripts` (`entryorguid`,`source_type`,`id`,`link`,`event_type`,`event_phase_mask`,`event_chance`,`event_flags`,`event_param1`,`event_param2`,`event_param3`,`event_param4`,`event_param5`,`action_type`,`action_param1`,`action_param2`,`action_param3`,`action_param4`,`action_param5`,`action_param6`,`target_type`,`target_param1`,`target_param2`,`target_param3`,`target_x`,`target_y`,`target_z`,`target_o`,`comment`) VALUES
(2289,1,0,0,20,0,100,0,619,0,0,0,0,12,1494,1,120000,0,0,0,8,0,0,0,-14598.6,76.0563,-11.249,0.925025,'Ruined Lifeboat - On Quest Enticing Negolash Rewarded - Summon Negolash'),
(2289,1,1,0,20,0,100,0,619,0,0,0,0,70,120,0,0,0,0,0,14,30020982,2332,0,0,0,0,0,'Ruined Lifeboat - On Quest Enticing Negolash Rewarded - Show Barbequed Buzzard Wings'),
(2289,1,2,0,20,0,100,0,619,0,0,0,0,70,120,0,0,0,0,0,14,30020983,2332,0,0,0,0,0,'Ruined Lifeboat - On Quest Enticing Negolash Rewarded - Show Barbequed Buzzard Wings'),
(2289,1,3,0,20,0,100,0,619,0,0,0,0,70,120,0,0,0,0,0,14,30020984,2332,0,0,0,0,0,'Ruined Lifeboat - On Quest Enticing Negolash Rewarded - Show Barbequed Buzzard Wings'),
(2289,1,4,0,20,0,100,0,619,0,0,0,0,70,120,0,0,0,0,0,14,30020985,2332,0,0,0,0,0,'Ruined Lifeboat - On Quest Enticing Negolash Rewarded - Show Barbequed Buzzard Wings'),
(2289,1,5,0,20,0,100,0,619,0,0,0,0,70,120,0,0,0,0,0,14,30020986,2332,0,0,0,0,0,'Ruined Lifeboat - On Quest Enticing Negolash Rewarded - Show Barbequed Buzzard Wings'),
(2289,1,6,0,20,0,100,0,619,0,0,0,0,70,120,0,0,0,0,0,14,30020987,2332,0,0,0,0,0,'Ruined Lifeboat - On Quest Enticing Negolash Rewarded - Show Barbequed Buzzard Wings'),
(2289,1,7,0,20,0,100,0,619,0,0,0,0,70,120,0,0,0,0,0,14,30020988,2332,0,0,0,0,0,'Ruined Lifeboat - On Quest Enticing Negolash Rewarded - Show Barbequed Buzzard Wings'),
(2289,1,8,0,20,0,100,0,619,0,0,0,0,70,120,0,0,0,0,0,14,30020989,2332,0,0,0,0,0,'Ruined Lifeboat - On Quest Enticing Negolash Rewarded - Show Barbequed Buzzard Wings'),
(2289,1,9,0,20,0,100,0,619,0,0,0,0,70,120,0,0,0,0,0,14,30020990,2332,0,0,0,0,0,'Ruined Lifeboat - On Quest Enticing Negolash Rewarded - Show Barbequed Buzzard Wings'),
(2289,1,10,0,20,0,100,0,619,0,0,0,0,70,120,0,0,0,0,0,14,30020991,2332,0,0,0,0,0,'Ruined Lifeboat - On Quest Enticing Negolash Rewarded - Show Barbequed Buzzard Wings'),
(2289,1,11,0,20,0,100,0,619,0,0,0,0,70,120,0,0,0,0,0,14,30020992,2332,0,0,0,0,0,'Ruined Lifeboat - On Quest Enticing Negolash Rewarded - Show Barbequed Buzzard Wings'),
(2289,1,12,0,20,0,100,0,619,0,0,0,0,70,120,0,0,0,0,0,14,30020993,2332,0,0,0,0,0,'Ruined Lifeboat - On Quest Enticing Negolash Rewarded - Show Barbequed Buzzard Wings'),
(2289,1,13,0,20,0,100,0,619,0,0,0,0,70,120,0,0,0,0,0,14,30020994,2333,0,0,0,0,0,'Ruined Lifeboat - On Quest Enticing Negolash Rewarded - Show Stranglevine Wine'),
(2289,1,14,0,20,0,100,0,619,0,0,0,0,70,120,0,0,0,0,0,14,30020995,2333,0,0,0,0,0,'Ruined Lifeboat - On Quest Enticing Negolash Rewarded - Show Stranglevine Wine'),
(2289,1,15,0,20,0,100,0,619,0,0,0,0,70,120,0,0,0,0,0,14,30020996,2333,0,0,0,0,0,'Ruined Lifeboat - On Quest Enticing Negolash Rewarded - Show Stranglevine Wine'),
(2289,1,16,0,20,0,100,0,619,0,0,0,0,70,120,0,0,0,0,0,14,30020997,2333,0,0,0,0,0,'Ruined Lifeboat - On Quest Enticing Negolash Rewarded - Show Stranglevine Wine'),
(2289,1,17,0,20,0,100,0,619,0,0,0,0,70,120,0,0,0,0,0,14,30020998,2333,0,0,0,0,0,'Ruined Lifeboat - On Quest Enticing Negolash Rewarded - Show Stranglevine Wine'),
(2289,1,18,0,20,0,100,0,619,0,0,0,0,70,120,0,0,0,0,0,14,30020999,2562,0,0,0,0,0,'Ruined Lifeboat - On Quest Enticing Negolash Rewarded - Show Baked Bread'),
(2289,1,19,0,20,0,100,0,619,0,0,0,0,70,120,0,0,0,0,0,14,30021000,2562,0,0,0,0,0,'Ruined Lifeboat - On Quest Enticing Negolash Rewarded - Show Baked Bread'),
(2289,1,20,0,20,0,100,0,619,0,0,0,0,70,120,0,0,0,0,0,14,30021001,2562,0,0,0,0,0,'Ruined Lifeboat - On Quest Enticing Negolash Rewarded - Show Baked Bread');
