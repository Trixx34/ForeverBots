-- Classic 1.60: warlock demons start with their first vanilla spell only (VMaNGOS petcreateinfo_spell): Imp Firebolt 3110,
-- Felhunter Devour Magic 19505, Voidwalker Torment 3716, Succubus Lash of Pain 7814. The Voidwalker also had Sacrifice, Suffering
-- and the retail 112042 for free (they come from the Grimoire of Demonology), Felhunter and Succubus had nothing.
DELETE FROM `creature_template_spell` WHERE `CreatureID` IN (416,417,1860,1863);
INSERT INTO `creature_template_spell` (`CreatureID`,`Index`,`Spell`,`VerifiedBuild`) VALUES
(416,0,3110,0),(417,0,19505,0),(1860,0,3716,0),(1863,0,7814,0);
