-- Classic 1.60: Immolate's new script effect puts the hidden Immolate aura 1282590 on the target, which every Conflagrate rank
-- (17962, 18930-18932 and the new 1293817, 1293818) requires. Safe to run again.
DELETE FROM `spell_script_names` WHERE `ScriptName` = 'classic_spell_warl_immolate';
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(348, 'classic_spell_warl_immolate'),
(707, 'classic_spell_warl_immolate'),
(1094, 'classic_spell_warl_immolate'),
(2941, 'classic_spell_warl_immolate'),
(11665, 'classic_spell_warl_immolate'),
(11667, 'classic_spell_warl_immolate'),
(11668, 'classic_spell_warl_immolate'),
(25309, 'classic_spell_warl_immolate');
