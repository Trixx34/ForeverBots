-- Safe to run again (the database updater re-runs files whose content changed).
-- WoW Classic beta 1.60.1.70009 DB2 layouts: hotfix table columns matching DB2LoadInfo.h / HotfixDatabase.cpp

-- AreaTable.db2 (9995B797)
SET @sql := IF((SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'area_table' AND COLUMN_NAME = 'ExplorationLevel') = 0, 'ALTER TABLE `area_table` ADD COLUMN `ExplorationLevel` tinyint NOT NULL DEFAULT ''0'' AFTER `UwZoneMusic`', 'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

-- Cfg_Regions.db2 (66694D4D)
SET @sql := IF((SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'cfg_regions' AND COLUMN_NAME = 'Name') = 0, 'ALTER TABLE `cfg_regions` ADD COLUMN `Name` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci AFTER `Tag`', 'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

DROP TABLE IF EXISTS `cfg_regions_locale`;
CREATE TABLE `cfg_regions_locale` (
  `ID` int unsigned NOT NULL DEFAULT '0',
  `locale` varchar(4) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci NOT NULL,
  `Name_lang` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `VerifiedBuild` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`ID`,`locale`,`VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
/*!50500 PARTITION BY LIST  COLUMNS(locale)
(PARTITION deDE VALUES IN ('deDE') ENGINE = InnoDB,
 PARTITION esES VALUES IN ('esES') ENGINE = InnoDB,
 PARTITION esMX VALUES IN ('esMX') ENGINE = InnoDB,
 PARTITION frFR VALUES IN ('frFR') ENGINE = InnoDB,
 PARTITION itIT VALUES IN ('itIT') ENGINE = InnoDB,
 PARTITION koKR VALUES IN ('koKR') ENGINE = InnoDB,
 PARTITION ptBR VALUES IN ('ptBR') ENGINE = InnoDB,
 PARTITION ruRU VALUES IN ('ruRU') ENGINE = InnoDB,
 PARTITION zhCN VALUES IN ('zhCN') ENGINE = InnoDB,
 PARTITION zhTW VALUES IN ('zhTW') ENGINE = InnoDB) */;

-- CharacterLoadout.db2 (713CE8BB)
SET @sql := IF((SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'character_loadout' AND COLUMN_NAME = 'Field_1_60_1_69876_003') = 0, 'ALTER TABLE `character_loadout` ADD COLUMN `Field_1_60_1_69876_003` int NOT NULL DEFAULT ''0'' AFTER `ItemContext`', 'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

-- CurrencyTypes.db2 (EBEAF439)
SET @sql := IF((SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'currency_types' AND COLUMN_NAME = 'MaxQtyCurveID') = 0, 'ALTER TABLE `currency_types` ADD COLUMN `MaxQtyCurveID` int NOT NULL DEFAULT ''0'' AFTER `AccountTransferPercentage`', 'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

-- Faction.db2 (6D443C38)
SET @sql := IF((SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'faction' AND COLUMN_NAME = 'RenownThresholdCurveID') = 0, 'ALTER TABLE `faction` ADD COLUMN `RenownThresholdCurveID` int NOT NULL DEFAULT ''0'' AFTER `RenownCurrencyID`', 'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

-- GlobalCurve.db2 (1DC57BDD)
SET @sql := IF((SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'global_curve' AND COLUMN_NAME = 'Subtype') = 0, 'ALTER TABLE `global_curve` ADD COLUMN `Subtype` int NOT NULL DEFAULT ''0'' AFTER `Type`', 'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

-- Item.db2 (9A2A4834) - Unknown1200 is AmmunitionType in this layout (same u8 column, kept name)
SET @sql := IF((SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'item' AND COLUMN_NAME = 'ItemPetFoodID') = 0, 'ALTER TABLE `item` ADD COLUMN `ItemPetFoodID` int NOT NULL DEFAULT ''0'' AFTER `SheatheType`', 'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

-- ItemSparse.db2 (6FCC3191)
SET @sql := IF((SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'item_sparse' AND COLUMN_NAME = 'AmmunitionType') = 0, 'ALTER TABLE `item_sparse` ADD COLUMN `AmmunitionType` tinyint unsigned NOT NULL DEFAULT ''0'' AFTER `OverallQualityID`', 'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

-- Map.db2 (D43AFAC3)
SET @sql := IF((SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'map' AND COLUMN_NAME = 'OceanLiquidTypeID') = 0, 'ALTER TABLE `map` ADD COLUMN `OceanLiquidTypeID` int NOT NULL DEFAULT ''0'' AFTER `WdtFileDataID`', 'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

-- NameGen.db2 (584300FA)
SET @sql := IF((SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'name_gen' AND COLUMN_NAME = 'NameType') = 0, 'ALTER TABLE `name_gen` ADD COLUMN `NameType` tinyint unsigned NOT NULL DEFAULT ''0'' AFTER `Sex`', 'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

-- PlayerDataElementAccount.db2 / PlayerDataElementCharacter.db2 (C513161B)
SET @sql := IF((SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'player_data_element_account' AND COLUMN_NAME = 'Field_12_1_5_69594_004') = 0, 'ALTER TABLE `player_data_element_account` ADD COLUMN `Field_12_1_5_69594_004` int NOT NULL DEFAULT ''0'' AFTER `Unknown1125`', 'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;
SET @sql := IF((SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'player_data_element_character' AND COLUMN_NAME = 'Field_12_1_5_69594_004') = 0, 'ALTER TABLE `player_data_element_character` ADD COLUMN `Field_12_1_5_69594_004` int NOT NULL DEFAULT ''0'' AFTER `Unknown1125`', 'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

-- SkillLineAbility.db2 (224F7EA0)
SET @sql := IF((SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'skill_line_ability' AND COLUMN_NAME = 'Field_5_5_4_67090_0141') = 0, 'ALTER TABLE `skill_line_ability` ADD COLUMN `Field_5_5_4_67090_0141` int NOT NULL DEFAULT ''0'' AFTER `SkillupSkillLineID`', 'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;
SET @sql := IF((SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'skill_line_ability' AND COLUMN_NAME = 'Field_5_5_4_67090_0142') = 0, 'ALTER TABLE `skill_line_ability` ADD COLUMN `Field_5_5_4_67090_0142` int NOT NULL DEFAULT ''0'' AFTER `Field_5_5_4_67090_0141`', 'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

-- SpellItemEnchantment.db2 (952B72B2) - most columns widened to 32 bit, ConditionID maps to Field_12_1_5_69594_011
ALTER TABLE `spell_item_enchantment`
  MODIFY `Charges` int unsigned NOT NULL DEFAULT '0',
  MODIFY `Effect1` int unsigned NOT NULL DEFAULT '0',
  MODIFY `Effect2` int unsigned NOT NULL DEFAULT '0',
  MODIFY `Effect3` int unsigned NOT NULL DEFAULT '0',
  MODIFY `EffectPointsMin1` int NOT NULL DEFAULT '0',
  MODIFY `EffectPointsMin2` int NOT NULL DEFAULT '0',
  MODIFY `EffectPointsMin3` int NOT NULL DEFAULT '0',
  MODIFY `ScalingClass` int NOT NULL DEFAULT '0',
  MODIFY `ScalingClassRestricted` int NOT NULL DEFAULT '0',
  MODIFY `ConditionID` int unsigned NOT NULL DEFAULT '0',
  MODIFY `RequiredSkillID` int unsigned NOT NULL DEFAULT '0',
  MODIFY `RequiredSkillRank` int unsigned NOT NULL DEFAULT '0',
  MODIFY `MinLevel` int unsigned NOT NULL DEFAULT '0',
  MODIFY `MaxLevel` int unsigned NOT NULL DEFAULT '0';
SET @sql := IF((SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'spell_item_enchantment' AND COLUMN_NAME = 'Field_12_1_5_69594_021') = 0, 'ALTER TABLE `spell_item_enchantment` ADD COLUMN `Field_12_1_5_69594_021` int NOT NULL DEFAULT ''0'' AFTER `TransmogCost`', 'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

-- SpellProcsPerMinuteMod.db2 (A89F22A1)
SET @sql := IF((SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'spell_procs_per_minute_mod' AND COLUMN_NAME = 'Field_12_1_5_69594_003') = 0, 'ALTER TABLE `spell_procs_per_minute_mod` ADD COLUMN `Field_12_1_5_69594_003` int NOT NULL DEFAULT ''0'' AFTER `Coeff`', 'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

-- SpellVisualMissile.db2 (EC765EB2)
SET @sql := IF((SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'spell_visual_missile' AND COLUMN_NAME = 'Field_12_1_5_69594_018') = 0, 'ALTER TABLE `spell_visual_missile` ADD COLUMN `Field_12_1_5_69594_018` int NOT NULL DEFAULT ''0'' AFTER `Unused1100`', 'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;
SET @sql := IF((SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'spell_visual_missile' AND COLUMN_NAME = 'Field_12_1_5_69594_019') = 0, 'ALTER TABLE `spell_visual_missile` ADD COLUMN `Field_12_1_5_69594_019` int NOT NULL DEFAULT ''0'' AFTER `Field_12_1_5_69594_018`', 'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;
SET @sql := IF((SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'spell_visual_missile' AND COLUMN_NAME = 'Field_12_1_5_69594_020') = 0, 'ALTER TABLE `spell_visual_missile` ADD COLUMN `Field_12_1_5_69594_020` int NOT NULL DEFAULT ''0'' AFTER `Field_12_1_5_69594_019`', 'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;
SET @sql := IF((SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'spell_visual_missile' AND COLUMN_NAME = 'Field_12_1_5_69594_021') = 0, 'ALTER TABLE `spell_visual_missile` ADD COLUMN `Field_12_1_5_69594_021` int NOT NULL DEFAULT ''0'' AFTER `Field_12_1_5_69594_020`', 'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

-- TraitCurrency.db2 (A8B0874B)
SET @sql := IF((SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'trait_currency' AND COLUMN_NAME = 'SourcedMax') = 0, 'ALTER TABLE `trait_currency` ADD COLUMN `SourcedMax` int NOT NULL DEFAULT ''0'' AFTER `PlayerDataElementCharacterID`', 'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

-- TraitCurrencySource.db2 (4C49B6AA)
SET @sql := IF((SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'trait_currency_source' AND COLUMN_NAME = 'SuperDistrictSetID') = 0, 'ALTER TABLE `trait_currency_source` ADD COLUMN `SuperDistrictSetID` int NOT NULL DEFAULT ''0'' AFTER `TraitNodeEntryID`', 'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;
