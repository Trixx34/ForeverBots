-- Battle.net (BattleTag) friends for the Classic 1.60 client, kept in auth so every realm shares them (game/Services/FriendsService.cpp).
-- A friendship is two rows (one per account). Safe to run again.
CREATE TABLE IF NOT EXISTS `battlenet_friends` (
  `account_id` INT UNSIGNED NOT NULL COMMENT 'battlenet_accounts.id',
  `friend_account_id` INT UNSIGNED NOT NULL COMMENT 'battlenet_accounts.id',
  `created` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'unix time',
  `note` VARCHAR(127) NOT NULL DEFAULT '',
  PRIMARY KEY (`account_id`, `friend_account_id`),
  KEY `idx_friend` (`friend_account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Battle.net friends';

CREATE TABLE IF NOT EXISTS `battlenet_friend_invitations` (
  `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `inviter_account_id` INT UNSIGNED NOT NULL COMMENT 'battlenet_accounts.id',
  `invitee_account_id` INT UNSIGNED NOT NULL COMMENT 'battlenet_accounts.id',
  `created` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'unix time',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_pair` (`inviter_account_id`, `invitee_account_id`),
  KEY `idx_invitee` (`invitee_account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Pending Battle.net friend invitations';
