-- Classic 1.60: Ruins of Lordaeron dungeon (map 2999) loot fixes on top of the sniff import (sniff of 2026-10-11, group loot).
-- Mirror of Rath'mael 271213: rolled for at 100.4 min, right when Rath'mael 250657 died; the corpse was looted by someone else, so
-- the creature loot import never saw it.
DELETE FROM `creature_loot_template` WHERE `Entry` = 250657 AND `Item` = 271213;
INSERT INTO `creature_loot_template` (`Entry`,`ItemType`,`Item`,`Chance`,`QuestRequired`,`LootMode`,`GroupId`,`MinCount`,`MaxCount`,`Comment`) VALUES
(250657,0,271213,100,0,1,0,1,1,'Rath''mael - Mirror of Rath''mael (sniff: group loot roll at his death)');
UPDATE `creature_template_difficulty` SET `LootID` = 250657 WHERE `Entry` = 250657 AND `DifficultyID` = 0 AND `LootID` = 0;

-- Sword of the Fallen 286977 (blue, bind on equip): 1 drop in the 7 Ghouls looted by the sniffer (14%), far more than a trash
-- bind on equip drop; 2% until more kills are seen.
UPDATE `creature_loot_template` SET `Chance` = 2, `Comment` = 'Ghoul - Sword of the Fallen (bind on equip; sniff 1 of 7, lowered)' WHERE `Entry` = 250627 AND `Item` = 286977;
