-- Classic 1.60: Skysight 1259686 (Skyborne Windshapers racial). Next to an Elemental Convergence (616992, spell focus 2271) the
-- official server gives the 15 min Elemental Blessing 1270893 instead of the 30 sec one (1259688); same script class as Read Ley Line.
DELETE FROM `spell_script_names` WHERE `spell_id` = 1259686 AND `ScriptName` = 'classic_spell_skyborne_skysight';
INSERT INTO `spell_script_names` (`spell_id`,`ScriptName`) VALUES
(1259686, 'classic_spell_skyborne_skysight');
