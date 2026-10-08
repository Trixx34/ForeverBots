-- Classic 1.60 (WoW Forever): profession books Mining for Dummies (247840), Wild Harvest (247841) and Pelt Collecting for Beginners
-- (247846) cast 1245608 / 1245609 / 1245610 (two dummy effects: skill +2, cap 15, learns the profession if a slot is free)
DELETE FROM `spell_script_names` WHERE `spell_id` IN (1245608, 1245609, 1245610);
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(1245608, 'classic_spell_profession_book'),
(1245609, 'classic_spell_profession_book'),
(1245610, 'classic_spell_profession_book');
