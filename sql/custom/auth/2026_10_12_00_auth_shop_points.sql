-- Classic 1.60 in-game Store: shop points per game account (the web site adds them) and a log of purchases.

CREATE TABLE IF NOT EXISTS `account_shop_points` (
  `AccountID` int unsigned NOT NULL,
  `Points` int unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`AccountID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Shop points per game account';

CREATE TABLE IF NOT EXISTS `shop_purchase_log` (
  `ID` bigint unsigned NOT NULL AUTO_INCREMENT,
  `AccountID` int unsigned NOT NULL,
  `ProductID` int unsigned NOT NULL,
  `Price` int unsigned NOT NULL,
  `CharacterGUID` bigint unsigned NOT NULL,
  `Time` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`ID`),
  KEY `idx_account` (`AccountID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Shop purchases';
