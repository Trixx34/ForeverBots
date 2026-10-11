-- Casters (priest 5, mage 8, warlock 9) start with the spell Shoot (5019) and a starter wand, so they have a free filler
-- when they run out of mana (bot combat strategy, BotCombat.cpp Kind::Wand). Only characters created after this is applied
-- get them; existing characters need a one-off grant (character_spell / character inventory).
-- race 0 and racemask 0 mean all races. Wand 7607 (Sable Wand, class -1, bonds on pickup, RequiredLevel 0, item_sparse in
-- forever_hotfixes) is the lowest-requirement wand in the data; swap the id here if a weaker one is wanted.
-- Idempotent: safe to run more than once.

DELETE FROM `playercreateinfo_spell_custom` WHERE `racemask` = 0 AND `classmask` = 400 AND `Spell` = 5019;
INSERT INTO `playercreateinfo_spell_custom` (`racemask`, `classmask`, `Spell`, `Note`) VALUES
(0, 400, 5019, 'Shoot (wand) for priest, mage, warlock');

DELETE FROM `playercreateinfo_item` WHERE `race` = 0 AND `class` IN (5, 8, 9) AND `itemid` = 7607;
INSERT INTO `playercreateinfo_item` (`race`, `class`, `itemid`, `amount`) VALUES
(0, 5, 7607, 1),
(0, 8, 7607, 1),
(0, 9, 7607, 1);
