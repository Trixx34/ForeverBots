-- Safe to run again (the database updater re-runs files whose content changed).
-- Classic 1.60: every realm serves one ruleset (super district), identified by its season (Cfg_SuperDistrict.ContentSetID):
-- 136 = PvP, 137 = Normal, 138 = Roleplay, 140 = Hardcore. The client joins the realm whose season matches the ruleset it picked.
SET @sql := IF((SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'realmlist' AND COLUMN_NAME = 'contentSetId') = 0, 'ALTER TABLE `realmlist` ADD COLUMN `contentSetId` int unsigned NOT NULL DEFAULT 137 AFTER `Battlegroup`', 'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;
UPDATE `realmlist` SET `contentSetId` = 137 WHERE `id` = 70;
