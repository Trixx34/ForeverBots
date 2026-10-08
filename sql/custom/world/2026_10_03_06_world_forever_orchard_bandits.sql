-- Re-run after the sniff files (2026_10_01_03/04, 2026_10_02_00/06, 2026_10_03_00) were regenerated: they rewrite rows this file adjusts (last: 2026-10-05, sniff of 2026-10-04 20:05).
-- Classic 1.60 (WoW Forever), Zephras Isle orchard: Ferauu the Bludgeon (252863) and his Bandit Henchmen (252875) are summoned by
-- the Malfunctioning Cyclone Constructs event (classic_npc_malfunctioning_cyclone_construct, official beta sniff 70170). The sniff
-- import placed them as fixed spawns at both ends of their walk: removed. The construct wanders by its post between the events.
-- Safe to run again.
DELETE FROM `creature_addon` WHERE `guid` IN (SELECT `guid` FROM `creature` WHERE `id` IN (252863, 252875) AND `map` = 2991);
DELETE FROM `creature` WHERE `id` IN (252863, 252875) AND `map` = 2991;

UPDATE `creature_template` SET `AIName` = '', `ScriptName` = 'classic_npc_malfunctioning_cyclone_construct' WHERE `entry` = 250929;
DELETE FROM `smart_scripts` WHERE `entryorguid` = 250929 AND `source_type` = 0;
DELETE FROM `creature_addon` WHERE `guid` = 21100144;
UPDATE `creature` SET `MovementType` = 1, `wander_distance` = 15 WHERE `guid` = 21100144;
