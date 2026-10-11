-- Forever launcher "remember me" for its auto-login: after one sign-in (password, and the authenticator code if the account has one)
-- the launcher keeps a random token; bnetserver /bnetserver/launcher/login/ trades it for a fresh login ticket that the game uses through
-- its Battle.net launcher login (-launcherlogin). Only the SHA-256 of the token is stored. Valid 30 days from the last use.
-- Safe to run again.
CREATE TABLE IF NOT EXISTS `battlenet_launcher_tokens` (
  `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `account_id` INT UNSIGNED NOT NULL COMMENT 'battlenet_accounts.id',
  `token_hash` CHAR(64) NOT NULL COMMENT 'SHA-256 of the token, hex',
  `created` INT UNSIGNED NOT NULL DEFAULT 0,
  `last_used` INT UNSIGNED NOT NULL DEFAULT 0,
  `expires` INT UNSIGNED NOT NULL DEFAULT 0,
  `ip` VARCHAR(45) NOT NULL DEFAULT '',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_token_hash` (`token_hash`),
  KEY `idx_account` (`account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Forever launcher auto-login tokens';
