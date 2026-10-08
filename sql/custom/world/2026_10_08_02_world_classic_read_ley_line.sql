-- Classic 1.60: Read Ley Line (Skyborne racial) at a ley line also gives the 15 min Energized (1259691). Safe to run again.
DELETE FROM `spell_script_names` WHERE `spell_id` = 1259705 AND `ScriptName` = 'classic_spell_skyborne_read_ley_line';
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(1259705, 'classic_spell_skyborne_read_ley_line');
