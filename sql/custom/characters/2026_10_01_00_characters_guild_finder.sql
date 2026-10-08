-- Safe to run again (the database updater re-runs files whose content changed).
-- Classic 1.60 Guild Finder: one recruitment posting per guild, and the players applications
CREATE TABLE IF NOT EXISTS `guild_finder_posting` (
  `guildId` bigint unsigned NOT NULL,
  `poster` bigint unsigned NOT NULL DEFAULT '0',
  `name` varchar(100) NOT NULL DEFAULT '',
  `description` varchar(2048) NOT NULL DEFAULT '',
  `specs` bigint unsigned NOT NULL DEFAULT '0',
  `flags` int NOT NULL DEFAULT '0',
  `minItemLevel` int NOT NULL DEFAULT '0',
  `avatar` int unsigned NOT NULL DEFAULT '0',
  `crossFaction` tinyint unsigned NOT NULL DEFAULT '0',
  `updated` bigint NOT NULL DEFAULT '0',
  PRIMARY KEY (`guildId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `guild_finder_application` (
  `guildId` bigint unsigned NOT NULL,
  `player` bigint unsigned NOT NULL,
  `comment` varchar(2048) NOT NULL DEFAULT '',
  `specs` bigint unsigned NOT NULL DEFAULT '0',
  `status` tinyint unsigned NOT NULL DEFAULT '1',
  `created` bigint NOT NULL DEFAULT '0',
  PRIMARY KEY (`guildId`,`player`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
