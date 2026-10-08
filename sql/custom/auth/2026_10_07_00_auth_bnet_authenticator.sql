-- Battle.net account authenticator (TOTP, RFC 6238, the code of any authenticator app). Set up from the support site account page;
-- bnetserver asks for the code at login (client AUTHENTICATOR login state) and secured accounts get the +4 backpack slots in game.
-- Base32 secret, NULL = no authenticator. Safe to run again.
SET @col := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'battlenet_accounts' AND COLUMN_NAME = 'authenticator_secret');
SET @sql := IF(@col = 0, 'ALTER TABLE `battlenet_accounts` ADD COLUMN `authenticator_secret` VARCHAR(64) NULL DEFAULT NULL AFTER `LoginTicketExpiry`', 'SELECT 1');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
