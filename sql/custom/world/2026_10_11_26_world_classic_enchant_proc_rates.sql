-- Classic 1.60: vanilla weapon enchant proc rates (VMaNGOS spell_proc_item_enchant, its first rank covers the higher ranks).
-- Frostbrand (all ranks) 9 per minute, Icy Chill 1894 1.6 per minute (retail row: flat 2%), Unholy Weapon 1899 has no vanilla rate
-- and used the weapon speed rate (about 1 per minute, retail row: 3). Fiery Weapon, Lifestealing and Crusader already match.
DELETE FROM `spell_enchant_proc_data` WHERE `EnchantID` IN (2,12,524,1667,1668,7566);
INSERT INTO `spell_enchant_proc_data` (`EnchantID`,`Chance`,`ProcsPerMinute`,`HitMask`,`AttributesMask`) VALUES
(2,0,9,0,0),(12,0,9,0,0),(524,0,9,0,0),(1667,0,9,0,0),(1668,0,9,0,0),(7566,0,9,0,0);
UPDATE `spell_enchant_proc_data` SET `Chance` = 0, `ProcsPerMinute` = 1.6 WHERE `EnchantID` = 1894;
UPDATE `spell_enchant_proc_data` SET `Chance` = 0, `ProcsPerMinute` = 1 WHERE `EnchantID` = 1899;
