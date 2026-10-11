-- Classic 1.60: retail TrinityCore spell scripts bound to vanilla spell ids that change their behaviour.
-- Minor Healthstone 6262: retail script heals a percent of max health; the Classic spell heals its own flat amount.
-- 12328 is Death Wish in Classic, not retail Sweeping Strikes.
-- Flamestrike 2120: retail Hot Streak / Ignite marker (no Hot Streak in Classic).
-- Vampiric Embrace 15286 / 15290: retail self-buff scripts; Classic it is a debuff on the enemy (classic_spell_pri_vampiric_embrace),
-- and 15290 heals the whole party including the priest for the same amount.
DELETE FROM `spell_script_names` WHERE (`spell_id`, `ScriptName`) IN ((6262,'spell_warl_healthstone_heal'),(12328,'spell_warr_sweeping_strikes'),
(2120,'spell_mage_hot_streak_ignite_marker'),(15286,'spell_pri_vampiric_embrace'),(15290,'spell_pri_vampiric_embrace_target'));
DELETE FROM `spell_script_names` WHERE `ScriptName` = 'classic_spell_pri_vampiric_embrace';
INSERT INTO `spell_script_names` (`spell_id`,`ScriptName`) VALUES
(15286,'classic_spell_pri_vampiric_embrace');
-- Vampiric Embrace procs when the debuffed enemy takes Shadow spell or periodic damage (vanilla procFlags 0xA0000); the script
-- checks the damage came from the priest who cast it
DELETE FROM `spell_proc` WHERE `SpellId` = 15286;
INSERT INTO `spell_proc` (`SpellId`,`SchoolMask`,`SpellFamilyName`,`SpellFamilyMask0`,`SpellFamilyMask1`,`SpellFamilyMask2`,`SpellFamilyMask3`,`ProcFlags`,`ProcFlags2`,`SpellTypeMask`,`SpellPhaseMask`,`HitMask`,`AttributesMask`,`DisableEffectsMask`,`ProcsPerMinute`,`Chance`,`Cooldown`,`Charges`) VALUES
(15286,32,0,0,0,0,0,655360,0,1,2,0,0,0,0,100,0,0);
