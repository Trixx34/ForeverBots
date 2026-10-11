-- Classic 1.60: stable slots bought at a stable master (vanilla rules, 2 slots; Player::BuyStableSlot). Safe to run again.
CREATE TABLE IF NOT EXISTS `character_stable_slots` (
  `guid` bigint unsigned NOT NULL COMMENT 'characters.guid',
  `slots` tinyint unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='stable slots bought';
