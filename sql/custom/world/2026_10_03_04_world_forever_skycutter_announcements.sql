-- Re-run after the sniff files (2026_10_01_03/04, 2026_10_02_00/06, 2026_10_03_00) were regenerated: they rewrite rows this file adjusts (last: 2026-10-05, sniff of 2026-10-04 20:05).
-- Classic 1.60 (WoW Forever): dockmaster announcements when a Skycutter docks (classic_event_skycutter_arrival; arrival events of
-- the dock nodes of taxi paths 11398 / 11457, official beta sniff 70170). Safe to run again.
DELETE FROM `event_script_names` WHERE `Id` IN (105988, 103315);
INSERT INTO `event_script_names` (`Id`, `ScriptName`) VALUES
(105988, 'classic_event_skycutter_arrival'),     -- Zephras Isle dock (Dalaran Skycutter)
(103315, 'classic_event_skycutter_arrival');     -- Dalaran / Lordamere Lake dock
