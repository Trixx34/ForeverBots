-- Classic 1.60 Social window, Allies tab (RecentAllies.cpp): players a character grouped with, one row per ally and interaction type,
-- kept 30 days. Safe to run again.
CREATE TABLE IF NOT EXISTS `character_recent_allies` (
  `guid` bigint unsigned NOT NULL COMMENT 'characters.guid',
  `ally` bigint unsigned NOT NULL COMMENT 'characters.guid of the other player',
  `type` tinyint unsigned NOT NULL COMMENT 'interaction type sent to the client (0x0B = grouped)',
  `time` bigint unsigned NOT NULL COMMENT 'unix time of the latest such interaction',
  `value` int unsigned NOT NULL DEFAULT 0 COMMENT 'interaction value (grouped: zone id)',
  PRIMARY KEY (`guid`, `ally`, `type`),
  KEY `idx_guid_time` (`guid`, `time`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Allies tab of the Social window';
