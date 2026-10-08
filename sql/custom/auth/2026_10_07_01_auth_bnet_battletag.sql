-- Battle.net account BattleTag ("Name#1234"), sent to the client at Battle.net login (LogonRecord.battle_tag) and in the account info.
-- The client only offers Battle.net friends (and shows the BattleTag in the Social window) when it has one. NULL = none. Safe to run again.
SET @col := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'battlenet_accounts' AND COLUMN_NAME = 'battle_tag');
SET @sql := IF(@col = 0, 'ALTER TABLE `battlenet_accounts` ADD COLUMN `battle_tag` VARCHAR(32) NULL DEFAULT NULL AFTER `email`, ADD UNIQUE KEY `uk_battle_tag` (`battle_tag`)', 'SELECT 1');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
