-- Classic 1.60 character boost (character screen boost button, auth.account_boost_token): items mailed to the boosted character.
-- ClassID 0 = every class. Empty by default: fill it with the starter gear sets you want.

CREATE TABLE IF NOT EXISTS `shop_boost_item` (
  `BoostType` int unsigned NOT NULL DEFAULT 6 COMMENT '6 = level 60',
  `ClassID` tinyint unsigned NOT NULL DEFAULT 0 COMMENT '0 = all classes',
  `ItemID` int unsigned NOT NULL,
  `Count` int unsigned NOT NULL DEFAULT 1,
  PRIMARY KEY (`BoostType`,`ClassID`,`ItemID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Items mailed with a character boost';
