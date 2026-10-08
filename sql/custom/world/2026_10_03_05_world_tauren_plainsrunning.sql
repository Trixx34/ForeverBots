-- Re-run after the sniff files (2026_10_01_03/04, 2026_10_02_00/06, 2026_10_03_00) were regenerated: they rewrite rows this file adjusts (last: 2026-10-05, sniff of 2026-10-04 20:05).
-- Classic 1.60: Tauren racial Plainsrunning (1259918) stacks Plainsrunning speed (1299038) while moving (classic_spell_plainsrunning).
-- Safe to run again.
DELETE FROM `spell_script_names` WHERE `spell_id` = 1259918;
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES (1259918, 'classic_spell_plainsrunning');
