-- Classic 1.60: saved warlock demons keep their display id; move them off the retail displays the client cannot draw
-- (world 2026_10_12_01: Imp 148118, Felhunter 850, Voidwalker 1132, Succubus 4162)
UPDATE `character_pet` SET `modelid` = 148118 WHERE `entry` = 416;
UPDATE `character_pet` SET `modelid` = 850 WHERE `entry` = 417;
UPDATE `character_pet` SET `modelid` = 1132 WHERE `entry` = 1860;
UPDATE `character_pet` SET `modelid` = 4162 WHERE `entry` = 1863;
