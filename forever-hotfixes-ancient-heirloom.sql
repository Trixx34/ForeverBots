-- Item 264908 'Ancient Heirloom' (reward of quest 92460 'Coming of Age', races 95/96). Missing from the TDB hotfixes baseline.
-- RECONSTRUCTED FROM WOWHEAD (not from client DB2): common quality, item level 1, binds when picked up, Finger (ring, inventory type 11),
-- armor/misc subclass, no stats, no spells, icon inv_jewelry_ring_118 (FileDataID 629694, from the community listfile), flavour text below.
-- No existing ring uses that icon and rings have no item_modified_appearance row, so no display id is needed.
-- Apply to the forever_hotfixes database. Idempotent (REPLACE).
-- The server reads item/item_sparse from hotfixes; hotfix_data rows (Status 1 = valid) make the client receive the records.
-- Needs a worldserver restart to load. Unused stat slots are -1 like other items; remaining columns are cloned from ring 1076 / 1156.
START TRANSACTION;

DROP TEMPORARY TABLE IF EXISTS tmp_heirloom_item;
CREATE TEMPORARY TABLE tmp_heirloom_item AS SELECT * FROM `item` WHERE `ID` = 1156;
UPDATE tmp_heirloom_item SET `ID` = 264908, `ClassID` = 4, `SubclassID` = 0, `InventoryType` = 11, `Material` = 1, `IconFileDataID` = 629694,
  `ContentTuningID` = 0, `VerifiedBuild` = 0;
REPLACE INTO `item` SELECT * FROM tmp_heirloom_item;
DROP TEMPORARY TABLE tmp_heirloom_item;

DROP TEMPORARY TABLE IF EXISTS tmp_heirloom_sparse;
CREATE TEMPORARY TABLE tmp_heirloom_sparse AS SELECT * FROM `item_sparse` WHERE `ID` = 1076;
UPDATE tmp_heirloom_sparse SET `ID` = 264908, `Display` = 'Ancient Heirloom',
  `Description` = 'Deep dents and scratches mar the dull surface of the intricate Kaldorei patterns. Faded but legible script on the inside of the band reads ''With love always, KG''.',
  `ItemLevel` = 1, `OverallQualityID` = 1, `Bonding` = 1, `InventoryType` = 11, `Material` = 1, `RequiredLevel` = 0, `SellPrice` = 0, `BuyPrice` = 0,
  `Flags1` = 0, `Flags2` = 0, `Flags3` = 0, `Flags4` = 0, `AllowableRace1` = -1, `AllowableRace2` = -1, `AllowableClass` = -1,
  `StatPercentEditor1` = 0, `StatPercentEditor2` = 0, `StatModifierBonusStat1` = -1, `StatModifierBonusStat2` = -1, `VerifiedBuild` = 0;
REPLACE INTO `item_sparse` SELECT * FROM tmp_heirloom_sparse;
DROP TEMPORARY TABLE tmp_heirloom_sparse;

-- TableHash: item = 1344507586, item_sparse = 2442913102 (same as existing hotfix_data rows for those tables)
REPLACE INTO `hotfix_data` (`Id`, `UniqueId`, `TableHash`, `RecordId`, `Status`, `VerifiedBuild`) VALUES
(1264908, 1264908, 1344507586, 264908, 1, 0),
(1264909, 1264909, 2442913102, 264908, 1, 0);
COMMIT;
