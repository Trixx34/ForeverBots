-- Classic 1.60: retail TrinityCore game_event_pool rows. Pools 5699-5707 are VMaNGOS pools now (Stranglethorn Vale silver / gold /
-- iron veins), so the retail Noblegarden (event 9) link spawned those veins only during Noblegarden; the other rows name pools that
-- do not exist. No vanilla event drives a pool yet (vanilla game events are not imported).
DELETE FROM `game_event_pool` WHERE `eventEntry` = 9 AND `pool_entry` BETWEEN 5699 AND 5707;
DELETE FROM `game_event_pool` WHERE `pool_entry` NOT IN (SELECT `entry` FROM `pool_template`);
