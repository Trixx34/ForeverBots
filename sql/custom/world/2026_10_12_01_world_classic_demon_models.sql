-- Classic 1.60: warlock demons used retail displays (Imp 64799 is a character model display the 1.60 client cannot draw:
-- "Missing ChrModel for raceID 0, gender 2, class 4", invisible pet without portrait) and unit class 4 (rogue).
-- Official Forever creates of other warlocks pets (sniffs, 46 creates): Imp 148118 class 8, Voidwalker 1132 class 2,
-- Succubus 4162 class 2. Felhunter not sniffed: vanilla display 850, class 2 like the other mana demons.
DELETE FROM `creature_template_model` WHERE `CreatureID` IN (416,417,1860,1863);
INSERT INTO `creature_template_model` (`CreatureID`,`Idx`,`CreatureDisplayID`,`DisplayScale`,`Probability`,`VerifiedBuild`) VALUES
(416,0,148118,1,1,0),
(417,0,850,1,1,0),
(1860,0,1132,1,1,0),
(1863,0,4162,1,1,0);

UPDATE `creature_template` SET `unit_class` = 8 WHERE `entry` = 416;
UPDATE `creature_template` SET `unit_class` = 2 WHERE `entry` IN (417,1860,1863);

-- 148118 uses the same model as the vanilla Imp display 4449
DELETE FROM `creature_model_info` WHERE `DisplayID` = 148118;
INSERT INTO `creature_model_info` (`DisplayID`,`BoundingRadius`,`CombatReach`,`DisplayID_Other_Gender`,`VerifiedBuild`) VALUES
(148118,0.386562,0.75,0,0);
