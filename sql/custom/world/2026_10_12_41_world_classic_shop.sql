-- Classic 1.60 in-game Store (Shop window): tabs, products and what each product gives.
-- Prices are in shop points (auth.account_shop_points); the client shows them with its currency format ("$100" = 100 points).

DROP TABLE IF EXISTS `shop_group`;
CREATE TABLE `shop_group` (
  `ID` int unsigned NOT NULL,
  `Name` varchar(255) NOT NULL DEFAULT '',
  `IconFileDataID` int NOT NULL DEFAULT 0 COMMENT 'tab icon (FileDataID)',
  `Ordering` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`ID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Shop tabs';

DROP TABLE IF EXISTS `shop_product`;
CREATE TABLE `shop_product` (
  `ID` int unsigned NOT NULL,
  `GroupID` int unsigned NOT NULL,
  `Name` varchar(255) NOT NULL DEFAULT '',
  `Description` text NOT NULL,
  `IconFileDataID` int NOT NULL DEFAULT 0 COMMENT 'card icon (FileDataID)',
  `Price` int unsigned NOT NULL DEFAULT 0 COMMENT 'shop points',
  `NormalPrice` int unsigned NOT NULL DEFAULT 0 COMMENT 'crossed-out price for a sale, 0 = no sale',
  `Ordering` int NOT NULL DEFAULT 0,
  `Enabled` tinyint unsigned NOT NULL DEFAULT 1,
  PRIMARY KEY (`ID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Shop products';

DROP TABLE IF EXISTS `shop_product_item`;
CREATE TABLE `shop_product_item` (
  `ProductID` int unsigned NOT NULL,
  `ItemID` int unsigned NOT NULL,
  `Count` int unsigned NOT NULL DEFAULT 1,
  PRIMARY KEY (`ProductID`,`ItemID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Items a shop product mails to the character';

INSERT INTO `shop_group` (`ID`,`Name`,`IconFileDataID`,`Ordering`) VALUES
(1,'General',134414,1);

INSERT INTO `shop_product` (`ID`,`GroupID`,`Name`,`Description`,`IconFileDataID`,`Price`,`NormalPrice`,`Ordering`,`Enabled`) VALUES
(1,1,'Hearthstone','A spare Hearthstone, sent by mail.',134414,1,0,1,1);

INSERT INTO `shop_product_item` (`ProductID`,`ItemID`,`Count`) VALUES
(1,6948,1);
