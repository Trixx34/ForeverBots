-- Optional account-level grant for the four authenticator-associated backpack slots.
-- This is separate from authenticator_secret and does not enable or require TOTP.
SET @col := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'battlenet_accounts' AND COLUMN_NAME = 'backpack_slots_bonus');
SET @sql := IF(@col = 0, 'ALTER TABLE `battlenet_accounts` ADD COLUMN `backpack_slots_bonus` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `authenticator_secret`', 'SELECT 1');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
