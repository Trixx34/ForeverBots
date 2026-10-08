-- Classic 1.60: Motivated (Executor's Motivator) on a Tirisfal Deathguard credits the undead quest Discipline (99134). Safe to run again.
DELETE FROM `spell_script_names` WHERE `spell_id` = 1319421 AND `ScriptName` = 'classic_spell_executors_motivator';
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(1319421, 'classic_spell_executors_motivator');
