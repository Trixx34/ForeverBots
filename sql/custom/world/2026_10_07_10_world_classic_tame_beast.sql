-- Classic 1.60: Tame Beast (1515) and the Skyborne Taming Rods are 20 second channels; when one runs out the beast is tamed
-- The retail script stays for its cast checks. Safe to run again.
DELETE FROM `spell_script_names` WHERE `spell_id` = 1515 AND `ScriptName` = 'classic_spell_hun_tame_beast_channel';
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(1515, 'classic_spell_hun_tame_beast_channel');

-- Taming Rods of the Skyborne hunter quests: when the 20 second channel runs out, the rod's tame spell charms the beast and completes
-- the quest (1280003 -> 1280004 for 94978, 1280046 -> 1280044 for 94979, 1271103 -> 1271102 for 94013)
-- Human hunter rods (94792, 94863, 94864): 1277794 -> 1277851, 1278028 -> 1278060, 1278029 -> 1278061
DELETE FROM `spell_script_names` WHERE `spell_id` IN (1280003, 1280046, 1271103, 1277794, 1278028, 1278029);
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(1280003, 'classic_spell_hun_taming_rod'),
(1280046, 'classic_spell_hun_taming_rod'),
(1271103, 'classic_spell_hun_taming_rod'),
(1277794, 'classic_spell_hun_taming_rod'),
(1278028, 'classic_spell_hun_taming_rod'),
(1278029, 'classic_spell_hun_taming_rod');
