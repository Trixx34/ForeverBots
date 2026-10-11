-- Classic 1.60: profession certifications (items 271621-271627, spells 1289195-1289200) unlock the profession title on the account.
-- Safe to run again.
DELETE FROM `spell_script_names` WHERE `ScriptName` = 'classic_spell_profession_certification';
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(1289195, 'classic_spell_profession_certification'),
(1289196, 'classic_spell_profession_certification'),
(1289197, 'classic_spell_profession_certification'),
(1289198, 'classic_spell_profession_certification'),
(1289199, 'classic_spell_profession_certification'),
(1289200, 'classic_spell_profession_certification');
