-- Classic 1.60: Consecration and Seal of Fury (official dungeon sniff, Forever tooltips)
-- Consecration ranks: periodic dummy (effect 2) casts the rank damage spell 1280345-1280349 at the cast spot each second
DELETE FROM `spell_script_names` WHERE `spell_id` IN (26573,20116,20922,20923,20924) AND `ScriptName` IN ('spell_pal_consecration','classic_spell_pal_consecration');
INSERT INTO `spell_script_names` (`spell_id`,`ScriptName`) VALUES
(26573,'classic_spell_pal_consecration'),
(20116,'classic_spell_pal_consecration'),
(20922,'classic_spell_pal_consecration'),
(20923,'classic_spell_pal_consecration'),
(20924,'classic_spell_pal_consecration');

-- Seal of Fury ranks: melee swings cast the rank Holy damage spell; with a shield the damage grants Light of Fury absorb
DELETE FROM `spell_script_names` WHERE `ScriptName` IN ('classic_spell_pal_seal_of_fury','classic_spell_pal_seal_of_fury_damage','classic_spell_pal_judgement_of_fury');
INSERT INTO `spell_script_names` (`spell_id`,`ScriptName`) VALUES
(1311649,'classic_spell_pal_seal_of_fury'),
(1311656,'classic_spell_pal_seal_of_fury'),
(20163,'classic_spell_pal_seal_of_fury'),
(20419,'classic_spell_pal_seal_of_fury'),
(20421,'classic_spell_pal_seal_of_fury'),
(20422,'classic_spell_pal_seal_of_fury'),
(20423,'classic_spell_pal_seal_of_fury'),
(1311647,'classic_spell_pal_seal_of_fury_damage'),
(1311654,'classic_spell_pal_seal_of_fury_damage'),
(20231,'classic_spell_pal_seal_of_fury_damage'),
(20415,'classic_spell_pal_seal_of_fury_damage'),
(20416,'classic_spell_pal_seal_of_fury_damage'),
(20417,'classic_spell_pal_seal_of_fury_damage'),
(20418,'classic_spell_pal_seal_of_fury_damage'),
(1311650,'classic_spell_pal_judgement_of_fury'),
(1311655,'classic_spell_pal_judgement_of_fury'),
(20183,'classic_spell_pal_judgement_of_fury'),
(20411,'classic_spell_pal_judgement_of_fury'),
(20412,'classic_spell_pal_judgement_of_fury'),
(20413,'classic_spell_pal_judgement_of_fury'),
(20414,'classic_spell_pal_judgement_of_fury');

-- Seal of Fury procs on landed melee auto attacks, same as Seal of Righteousness (20154)
DELETE FROM `spell_proc` WHERE `SpellId` IN (1311649,1311656,20163,20419,20421,20422,20423);
INSERT INTO `spell_proc` (`SpellId`,`SchoolMask`,`SpellFamilyName`,`SpellFamilyMask0`,`SpellFamilyMask1`,`SpellFamilyMask2`,`SpellFamilyMask3`,`ProcFlags`,`ProcFlags2`,`SpellTypeMask`,`SpellPhaseMask`,`HitMask`,`AttributesMask`,`DisableEffectsMask`,`ProcsPerMinute`,`Chance`,`Cooldown`,`Charges`) VALUES
(1311649,0,0,0,0,0,0,4,0,0,0,3,0,0,0,0,0,0),
(1311656,0,0,0,0,0,0,4,0,0,0,3,0,0,0,0,0,0),
(20163,0,0,0,0,0,0,4,0,0,0,3,0,0,0,0,0,0),
(20419,0,0,0,0,0,0,4,0,0,0,3,0,0,0,0,0,0),
(20421,0,0,0,0,0,0,4,0,0,0,3,0,0,0,0,0,0),
(20422,0,0,0,0,0,0,4,0,0,0,3,0,0,0,0,0,0),
(20423,0,0,0,0,0,0,4,0,0,0,3,0,0,0,0,0,0);
