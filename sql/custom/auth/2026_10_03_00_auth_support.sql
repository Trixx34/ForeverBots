-- Classic 1.60: tokens handed to the in-game browser (Support window) by SMSG_GENERATE_SSO_TOKEN_RESPONSE.
-- The client opens <sso url>?token=<token>&ref=<page>; our support site looks the token up here to know the account and character.
CREATE TABLE IF NOT EXISTS `battlenet_sso_tokens` (
  `token` varchar(64) NOT NULL,
  `battlenetAccountId` int unsigned NOT NULL,
  `accountId` int unsigned NOT NULL COMMENT 'game account',
  `realmId` int unsigned NOT NULL COMMENT 'realm (worldserver) that issued it',
  `characterGuid` bigint unsigned NOT NULL DEFAULT '0' COMMENT 'character in world when issued, 0 = character select',
  `issued` bigint NOT NULL,
  `expires` bigint NOT NULL,
  `ip` varchar(45) NOT NULL DEFAULT '',
  PRIMARY KEY (`token`),
  KEY `idx_expires` (`expires`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Support site (contrib/support_site): tickets, replies, unstuck history
CREATE TABLE IF NOT EXISTS `support_tickets` (
  `id` int unsigned NOT NULL AUTO_INCREMENT,
  `battlenetAccountId` int unsigned NOT NULL,
  `accountId` int unsigned NOT NULL,
  `realmId` int unsigned NOT NULL,
  `characterGuid` bigint unsigned NOT NULL DEFAULT '0',
  `characterName` varchar(12) NOT NULL DEFAULT '',
  `category` enum('bug','stuck','account','player','other') NOT NULL,
  `subject` varchar(120) NOT NULL,
  `message` text NOT NULL,
  `mapId` int unsigned NOT NULL DEFAULT '0',
  `zoneId` int unsigned NOT NULL DEFAULT '0',
  `posX` float NOT NULL DEFAULT '0',
  `posY` float NOT NULL DEFAULT '0',
  `posZ` float NOT NULL DEFAULT '0',
  `status` enum('open','answered','closed') NOT NULL DEFAULT 'open',
  `created` bigint NOT NULL,
  `updated` bigint NOT NULL,
  PRIMARY KEY (`id`),
  KEY `idx_account` (`battlenetAccountId`),
  KEY `idx_status` (`status`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `support_ticket_replies` (
  `id` int unsigned NOT NULL AUTO_INCREMENT,
  `ticketId` int unsigned NOT NULL,
  `accountId` int unsigned NOT NULL,
  `isStaff` tinyint unsigned NOT NULL DEFAULT '0',
  `message` text NOT NULL,
  `created` bigint NOT NULL,
  PRIMARY KEY (`id`),
  KEY `idx_ticket` (`ticketId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `support_unstuck_log` (
  `id` int unsigned NOT NULL AUTO_INCREMENT,
  `accountId` int unsigned NOT NULL,
  `realmId` int unsigned NOT NULL,
  `characterGuid` bigint unsigned NOT NULL,
  `destination` enum('inn','graveyard') NOT NULL,
  `result` varchar(255) NOT NULL DEFAULT '',
  `time` bigint NOT NULL,
  PRIMARY KEY (`id`),
  KEY `idx_character` (`realmId`,`characterGuid`,`time`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
