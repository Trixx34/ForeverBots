-- Classic 1.60: data for the website armory (contrib/support_site), written by the console command ".armory export"
-- (scripts/Custom/Classic/world/classic_armory_commands.cpp) from the servers item templates (DB2 + hotfixes).
-- stats = "statType:value,..." (ItemModType), effects = "triggerType:spellId:spellName|...", icon = file name from the listfile.
CREATE TABLE IF NOT EXISTS `armory_item` (
  `id` int unsigned NOT NULL,
  `name` varchar(255) NOT NULL DEFAULT '',
  `quality` tinyint unsigned NOT NULL DEFAULT '0',
  `itemLevel` smallint unsigned NOT NULL DEFAULT '0',
  `requiredLevel` smallint NOT NULL DEFAULT '0',
  `class` tinyint unsigned NOT NULL DEFAULT '0',
  `subclass` tinyint unsigned NOT NULL DEFAULT '0',
  `inventoryType` tinyint unsigned NOT NULL DEFAULT '0',
  `bonding` tinyint unsigned NOT NULL DEFAULT '0',
  `armor` int unsigned NOT NULL DEFAULT '0',
  `dmgMin` float NOT NULL DEFAULT '0',
  `dmgMax` float NOT NULL DEFAULT '0',
  `delay` int unsigned NOT NULL DEFAULT '0',
  `stats` varchar(255) NOT NULL DEFAULT '',
  `effects` varchar(2048) NOT NULL DEFAULT '',
  `description` varchar(1024) NOT NULL DEFAULT '',
  `iconFileDataId` int NOT NULL DEFAULT '0',
  `sellPrice` int unsigned NOT NULL DEFAULT '0',
  `allowableClass` int NOT NULL DEFAULT '-1',
  `itemSet` smallint unsigned NOT NULL DEFAULT '0',
  `requiredSkill` smallint unsigned NOT NULL DEFAULT '0',
  `requiredSkillRank` smallint unsigned NOT NULL DEFAULT '0',
  `flags` int unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`id`),
  KEY `idx_name` (`name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- names and icons the armory shows: kind = race, class, skill, achievement (extra = skill category / achievement points)
CREATE TABLE IF NOT EXISTS `armory_name` (
  `kind` varchar(16) NOT NULL,
  `id` int unsigned NOT NULL,
  `name` varchar(255) NOT NULL DEFAULT '',
  `iconFileDataId` int NOT NULL DEFAULT '0',
  `extra` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`kind`,`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- icon file names (interface/icons/<name>.blp) for the file data ids above, from the community listfile (contrib/support_site/armory_icons.py)
CREATE TABLE IF NOT EXISTS `armory_icon` (
  `fileDataId` int NOT NULL,
  `name` varchar(128) NOT NULL,
  PRIMARY KEY (`fileDataId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
