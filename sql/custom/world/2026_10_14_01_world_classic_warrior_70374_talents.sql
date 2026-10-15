-- Classic 1.60 (client 70374): warrior talent Gore Drinker 1323967 - Bloodrage, Berserker Rage, Death Wish, Bloodthirst and the Enrage buff
-- give its healing buff (classic_spell_scripts.cpp). Lingering Rage 1323964 is handled in Player::AtExitCombat. Safe to run again.
DELETE FROM `spell_script_names` WHERE `ScriptName` IN ('classic_spell_warr_gore_drinker', 'classic_spell_warr_gore_drinker_enrage');
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(2687, 'classic_spell_warr_gore_drinker'),
(18499, 'classic_spell_warr_gore_drinker'),
(12328, 'classic_spell_warr_gore_drinker'),
(23881, 'classic_spell_warr_gore_drinker'),
(23892, 'classic_spell_warr_gore_drinker'),
(23893, 'classic_spell_warr_gore_drinker'),
(23894, 'classic_spell_warr_gore_drinker'),
(12880, 'classic_spell_warr_gore_drinker_enrage');
