-- Classic 1.60 realm transfers (in-game Store "Realm Transfer"): the source worldserver writes the character dump here and removes
-- the character; the target realm's worldserver loads it (at start and every minute) and marks the row done (1) or failed (2).
-- Rules: which realm may transfer to which, for which price (support site GM part).

CREATE TABLE IF NOT EXISTS `character_transfer` (
  `ID` int unsigned NOT NULL AUTO_INCREMENT,
  `AccountID` int unsigned NOT NULL,
  `SourceRealm` int unsigned NOT NULL,
  `SourceGuid` bigint unsigned NOT NULL,
  `TargetRealm` int unsigned NOT NULL,
  `TargetGuid` bigint unsigned NOT NULL DEFAULT 0,
  `Name` varchar(12) NOT NULL,
  `Dump` longtext NOT NULL,
  `Status` tinyint unsigned NOT NULL DEFAULT 0 COMMENT '0 waiting for the target realm, 1 done, 2 failed',
  `Message` varchar(255) NOT NULL DEFAULT '',
  `Created` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `Finished` timestamp NULL DEFAULT NULL,
  PRIMARY KEY (`ID`),
  KEY `idx_target` (`TargetRealm`,`Status`),
  KEY `idx_account` (`AccountID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Realm transfers';

CREATE TABLE IF NOT EXISTS `character_transfer_rule` (
  `SourceRealm` int unsigned NOT NULL,
  `TargetRealm` int unsigned NOT NULL,
  `DeadOnly` tinyint unsigned NOT NULL DEFAULT 0 COMMENT 'only characters that died (hardcore)',
  PRIMARY KEY (`SourceRealm`,`TargetRealm`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Allowed realm transfers';

-- default rules for the 4 rulesets (70 Normal, 71 PvP, 72 RP, 73 Hardcore): nobody moves onto Hardcore; Hardcore characters may
-- leave only once dead. Only the transfers listed here are allowed.
INSERT IGNORE INTO `character_transfer_rule` (`SourceRealm`,`TargetRealm`,`DeadOnly`) VALUES
(70,71,0),(70,72,0),
(71,70,0),(71,72,0),
(72,70,0),(72,71,0),
(73,70,1),(73,71,1),(73,72,1);
