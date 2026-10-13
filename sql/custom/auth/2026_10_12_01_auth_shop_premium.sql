-- Classic 1.60 in-game Store: premium accounts (may sell gold on the Gold Market; set by the web shop after a point purchase, or
-- by a GM), Gold Market bans, and shop settings the GM part of the support site changes (read by the worldserver at sale time).
-- (plain statements: the DB updater does not run DELIMITER / stored procedures) account_shop_points is rebuilt, points kept.

DROP TABLE IF EXISTS `account_shop_points_new`;
CREATE TABLE `account_shop_points_new` (
  `AccountID` int unsigned NOT NULL,
  `Points` int unsigned NOT NULL DEFAULT 0,
  `Premium` tinyint unsigned NOT NULL DEFAULT 0 COMMENT 'may sell gold on the Gold Market',
  `GoldMarketBanned` tinyint unsigned NOT NULL DEFAULT 0,
  `GoldMarketBanReason` varchar(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`AccountID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Shop points per game account';

INSERT INTO `account_shop_points_new` (`AccountID`,`Points`) SELECT `AccountID`,`Points` FROM `account_shop_points`;
DROP TABLE `account_shop_points`;
RENAME TABLE `account_shop_points_new` TO `account_shop_points`;

CREATE TABLE IF NOT EXISTS `shop_config` (
  `Name` varchar(64) NOT NULL,
  `Value` varchar(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`Name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Shop settings (support site GM part)';

INSERT IGNORE INTO `shop_config` (`Name`,`Value`) VALUES
('GoldMarketFee','10');
