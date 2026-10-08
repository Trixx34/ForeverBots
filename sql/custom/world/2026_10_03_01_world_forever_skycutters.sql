-- Re-run after the sniff files (2026_10_01_03/04, 2026_10_02_00/06, 2026_10_03_00) were regenerated: they rewrite rows this file adjusts (last: 2026-10-05, sniff of 2026-10-04 20:05).
-- Classic 1.60 (WoW Forever): the airships to Zephras Isle are the official Skycutters (ymir sniff of the official beta, build 70170):
-- Dalaran Skycutter 631278 (taxi path 11398, Lordamere Lake <-> Zephras Isle) and Mulgore Skycutter 644609 (taxi path 11457,
-- Mulgore <-> Zephras Isle). They replace the stand-in "Skyborne Airship" templates 900000/900001 (2026_09_29_00), same paths and
-- models; the templates come from 2026_10_01_03. Safe to run again.
DELETE FROM `gameobject_template_addon` WHERE `entry` IN (631278, 644609);
INSERT INTO `gameobject_template_addon` (`entry`, `faction`, `flags`) VALUES (631278, 0, 40), (644609, 0, 40);

DELETE FROM `transports` WHERE `guid` IN (56, 57) OR `entry` IN (900000, 900001, 631278, 644609);
INSERT INTO `transports` (`guid`, `entry`, `name`, `phaseUseFlags`, `phaseid`, `phasegroup`, `ScriptName`) VALUES
(56, 631278, 'Lordamere Lake, Eastern Kingdoms and Zephras Isle ("Dalaran Skycutter")', 0, 0, 0, ''),
(57, 644609, 'Mulgore, Kalimdor and Zephras Isle ("Mulgore Skycutter")', 0, 0, 0, '');
