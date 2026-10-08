-- Re-run after the sniff files (2026_10_01_03/04, 2026_10_02_00/06, 2026_10_03_00) were regenerated: they rewrite rows this file adjusts (last: 2026-10-05, sniff of 2026-10-04 20:05).
-- Classic 1.60: graveyards of Zephras Isle (map 2991, zone 16593); without them a death there sent the player to the Barrens.
-- IDs from the official betas cemetery list for the zone (ymir sniffs). Positions = where SMSG_DEATH_RELEASE_LOC sent the player
-- after dying on Zephras Isle (4 graveyards seen; the client has no graveyard positions). 11031 (starting village) and 11033 (Spirit
-- Healer) are certain; which of 10912 / 11032 is which of the other two spots is not in the data (the server only picks the nearest
-- graveyard, so it doesnt matter in game). The fifth ID in the list, 11432, has not been seen yet.
DELETE FROM `world_safe_locs` WHERE `ID` IN (10912,11031,11032,11033);
INSERT INTO `world_safe_locs` (`ID`,`MapID`,`LocX`,`LocY`,`LocZ`,`Facing`,`Comment`) VALUES
(11031,2991,4122.5,1939.0,974.6,4.71,'Zephras Isle - starting village'),
(10912,2991,2598.8,1982.4,781.1,3.14,'Zephras Isle - graveyard (sniffed release point)'),
(11032,2991,2419.8,1167.7,677.5,1.57,'Zephras Isle - graveyard (sniffed release point)'),
(11033,2991,3284.6,1150.8,759.4,3.01,'Zephras Isle - Spirit Healer');
DELETE FROM `graveyard_zone` WHERE `ID` IN (10912,11031,11032,11033);
INSERT INTO `graveyard_zone` (`ID`,`GhostZone`,`Comment`) VALUES
(11031,16593,'Zephras Isle - starting village'),
(10912,16593,'Zephras Isle - Hanaa Nightwind'),
(11032,16593,'Zephras Isle - Shen''dar Village'),
(11033,16593,'Zephras Isle - Spirit Healer');

-- .tele zephras: the starting village
DELETE FROM `game_tele` WHERE `name` = 'Zephras' OR `id` = 2314;
INSERT INTO `game_tele` (`id`,`position_x`,`position_y`,`position_z`,`orientation`,`map`,`name`) VALUES
(2314,4088.6,1848.9,976.3,0,2991,'Zephras');
