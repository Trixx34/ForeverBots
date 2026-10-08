-- Characters that left the in-game Discord channel with /leave: they are no longer joined to it at login (DiscordChannel.cpp,
-- worldserver.conf Discord.Channel.Enable). /join Discord removes the row again. Safe to run again.
CREATE TABLE IF NOT EXISTS `character_discord_optout` (
  `guid` bigint unsigned NOT NULL COMMENT 'characters.guid',
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='left the Discord channel';
