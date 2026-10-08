-- Safe to run again (the database updater re-runs files whose content changed).
-- Classic 1.60: characters can have a surname (up to 48 characters, shown after the name: "Name Surname")
SET @sql := IF((SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'characters' AND COLUMN_NAME = 'surname') = 0, 'ALTER TABLE `characters` ADD COLUMN `surname` varchar(48) NOT NULL DEFAULT '''' AFTER `name`', 'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;
