-- Classic 1.60 in-game Store: what a product does, as a list of actions (replaces shop_product_item).
-- Action  Param1                     Param2
--  1 Item       item entry               count (mailed)
--  2 Gold       copper                   -
--  3 Spell      spell id (mount, ...)    -
--  4 Service    1 rename, 2 customize,   -   (the character gets the flag, applied on the character screen)
--               3 race change, 4 faction change
--  5 Profession skill line id            skill value (0 = 300); learns every recipe of the profession
--  6 Level      level                    -
--  7 Title      CharTitles id            -
--  8 Reputation faction id               standing value (e.g. 42000 = exalted)
-- Products with any action other than Item act on the character in game, so they can only be bought in game.

DROP TABLE IF EXISTS `shop_product_action`;
CREATE TABLE `shop_product_action` (
  `ProductID` int unsigned NOT NULL,
  `Index` tinyint unsigned NOT NULL DEFAULT 0,
  `Action` tinyint unsigned NOT NULL,
  `Param1` int NOT NULL DEFAULT 0,
  `Param2` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ProductID`,`Index`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='What a shop product does';

DROP TABLE IF EXISTS `shop_product_item`;

DELETE FROM `shop_group` WHERE `ID` IN (1,2,3);
INSERT INTO `shop_group` (`ID`,`Name`,`IconFileDataID`,`Ordering`) VALUES
(1,'General',134414,1),
(2,'Services',136243,2),
(3,'Professions',136240,3);

DELETE FROM `shop_product` WHERE `ID` BETWEEN 1 AND 20;
INSERT INTO `shop_product` (`ID`,`GroupID`,`Name`,`Description`,`IconFileDataID`,`Price`,`NormalPrice`,`Ordering`,`Enabled`) VALUES
(1,1,'Hearthstone','A spare Hearthstone, sent by mail.',134414,1,0,1,1),
(2,1,'100 Gold','100 gold for this character.',133784,10,0,2,1),
(10,2,'Name Change','Change this character''s name on the character screen.',136243,10,0,1,1),
(11,2,'Appearance Change','Change this character''s appearance on the character screen.',136243,10,0,2,1),
(12,2,'Race Change','Change this character''s race on the character screen.',136243,20,0,3,1),
(13,2,'Faction Change','Change this character''s faction on the character screen.',136243,30,0,4,1),
(14,2,'Level 60','Raise this character to level 60. Visit your class trainer for new spells.',136243,50,0,5,1),
(20,3,'Max Alchemy','Alchemy at 300 with every recipe.',136240,25,0,1,1);

DELETE FROM `shop_product_action` WHERE `ProductID` BETWEEN 1 AND 20;
INSERT INTO `shop_product_action` (`ProductID`,`Index`,`Action`,`Param1`,`Param2`) VALUES
(1,0,1,6948,1),
(2,0,2,1000000,0),
(10,0,4,1,0),
(11,0,4,2,0),
(12,0,4,3,0),
(13,0,4,4,0),
(14,0,6,60,0),
(20,0,5,171,300);
