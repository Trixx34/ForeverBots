-- Classic 1.60: Seal of Righteousness adds Holy damage to melee swings, Judgement unleashes the active seal (it stays) and the
-- paladin's swings keep Judgement of the Crusader up (classic_spell_scripts.cpp, sniff of the official beta 2026-10-06)
-- Safe to run again.

-- Seal of Righteousness ranks: Forever rank 1 (20154), vanilla ranks 1-8
DELETE FROM `spell_script_names` WHERE `spell_id` IN (20154, 21084, 20287, 20288, 20289, 20290, 20291, 20292, 20293)
    AND `ScriptName` = 'classic_spell_pal_seal_of_righteousness';
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(20154, 'classic_spell_pal_seal_of_righteousness'),
(21084, 'classic_spell_pal_seal_of_righteousness'),
(20287, 'classic_spell_pal_seal_of_righteousness'),
(20288, 'classic_spell_pal_seal_of_righteousness'),
(20289, 'classic_spell_pal_seal_of_righteousness'),
(20290, 'classic_spell_pal_seal_of_righteousness'),
(20291, 'classic_spell_pal_seal_of_righteousness'),
(20292, 'classic_spell_pal_seal_of_righteousness'),
(20293, 'classic_spell_pal_seal_of_righteousness');

-- every landed melee swing (PROC_FLAG_DEAL_MELEE_SWING, normal and critical hits)
DELETE FROM `spell_proc` WHERE `SpellId` IN (20154, 21084, 20287, 20288, 20289, 20290, 20291, 20292, 20293);
INSERT INTO `spell_proc` (`SpellId`, `SchoolMask`, `SpellFamilyName`, `SpellFamilyMask0`, `SpellFamilyMask1`, `SpellFamilyMask2`, `SpellFamilyMask3`,
`ProcFlags`, `ProcFlags2`, `SpellTypeMask`, `SpellPhaseMask`, `HitMask`, `AttributesMask`, `DisableEffectsMask`, `ProcsPerMinute`, `Chance`, `Cooldown`, `Charges`) VALUES
(20154, 0, 0, 0, 0, 0, 0, 0x4, 0, 0, 0, 0x3, 0, 0, 0, 100, 0, 0),
(21084, 0, 0, 0, 0, 0, 0, 0x4, 0, 0, 0, 0x3, 0, 0, 0, 100, 0, 0),
(20287, 0, 0, 0, 0, 0, 0, 0x4, 0, 0, 0, 0x3, 0, 0, 0, 100, 0, 0),
(20288, 0, 0, 0, 0, 0, 0, 0x4, 0, 0, 0, 0x3, 0, 0, 0, 100, 0, 0),
(20289, 0, 0, 0, 0, 0, 0, 0x4, 0, 0, 0, 0x3, 0, 0, 0, 100, 0, 0),
(20290, 0, 0, 0, 0, 0, 0, 0x4, 0, 0, 0, 0x3, 0, 0, 0, 100, 0, 0),
(20291, 0, 0, 0, 0, 0, 0, 0x4, 0, 0, 0, 0x3, 0, 0, 0, 100, 0, 0),
(20292, 0, 0, 0, 0, 0, 0, 0x4, 0, 0, 0, 0x3, 0, 0, 0, 100, 0, 0),
(20293, 0, 0, 0, 0, 0, 0, 0x4, 0, 0, 0, 0x3, 0, 0, 0, 100, 0, 0);

-- Judgement: the Classic script instead of the retail ones (Holy Power, Judgment of Justice)
DELETE FROM `spell_script_names` WHERE `spell_id` = 20271;
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(20271, 'classic_spell_pal_judgement');

-- Judgement of the Crusader: the paladin's landed swings (25942, applied with the judgement) keep the debuff on the target (25943)
DELETE FROM `spell_script_names` WHERE `spell_id` = 25943;
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(25943, 'classic_spell_pal_judgement_of_the_crusader_refresh');
DELETE FROM `spell_proc` WHERE `SpellId` = 25942;
INSERT INTO `spell_proc` (`SpellId`, `SchoolMask`, `SpellFamilyName`, `SpellFamilyMask0`, `SpellFamilyMask1`, `SpellFamilyMask2`, `SpellFamilyMask3`,
`ProcFlags`, `ProcFlags2`, `SpellTypeMask`, `SpellPhaseMask`, `HitMask`, `AttributesMask`, `DisableEffectsMask`, `ProcsPerMinute`, `Chance`, `Cooldown`, `Charges`) VALUES
(25942, 0, 0, 0, 0, 0, 0, 0x4, 0, 0, 0, 0x3, 0, 0, 0, 100, 0, 0);

-- Holy Shock: all three trainer ranks heal or damage with their own spells (the retail script only knows rank 1)
DELETE FROM `spell_script_names` WHERE `spell_id` IN (20473, 20929, 20930);
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(20473, 'classic_spell_pal_holy_shock'),
(20929, 'classic_spell_pal_holy_shock'),
(20930, 'classic_spell_pal_holy_shock');
