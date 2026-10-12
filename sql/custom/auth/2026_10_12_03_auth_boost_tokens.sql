-- Classic 1.60 character boost tokens (bought in the in-game Store, or given by a GM): each unused one is a boost button on the
-- character screen of every realm; using it on a character raises it to the boost level.

CREATE TABLE IF NOT EXISTS `account_boost_token` (
  `ID` int unsigned NOT NULL AUTO_INCREMENT,
  `AccountID` int unsigned NOT NULL,
  `BoostType` int unsigned NOT NULL DEFAULT 6 COMMENT 'CharacterServiceInfo boost type (6 = level 60)',
  `Source` varchar(64) NOT NULL DEFAULT '' COMMENT 'shop product / gm',
  `Created` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `UsedRealm` int unsigned NOT NULL DEFAULT 0,
  `UsedGuid` bigint unsigned NOT NULL DEFAULT 0,
  `UsedTime` timestamp NULL DEFAULT NULL,
  PRIMARY KEY (`ID`),
  KEY `idx_account` (`AccountID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Character boost tokens';
