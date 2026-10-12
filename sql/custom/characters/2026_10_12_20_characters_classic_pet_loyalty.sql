-- Classic 1.60: hunter pet loyalty (vanilla rules, Pet::ModifyLoyalty). Pets without a row start Rebellious. Safe to run again.
CREATE TABLE IF NOT EXISTS `character_pet_loyalty` (
  `id` int unsigned NOT NULL COMMENT 'character_pet.id',
  `loyalty` tinyint unsigned NOT NULL DEFAULT '1' COMMENT 'loyalty level 1 Rebellious .. 6 Best Friend',
  `loyalty_points` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Classic hunter pet loyalty';
