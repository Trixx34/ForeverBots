-- Alt bots remembered across worldserver restarts (BotAlts, Bot.Alt.Persist).
-- Apply to the forever_characters database (idempotent).
-- Without this table the worldserver logs one warning and simply does not remember alts (no crash).
CREATE TABLE IF NOT EXISTS `bot_alt` (
  `guid` bigint unsigned NOT NULL COMMENT 'characters.guid of the alt bot',
  `account_id` int unsigned NOT NULL COMMENT 'owner account (same as the character account)',
  `added_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
