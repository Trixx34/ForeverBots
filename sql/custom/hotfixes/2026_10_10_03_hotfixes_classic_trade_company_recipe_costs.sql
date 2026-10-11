-- Classic 1.60: Merchant Favor (currency 3402) prices of the trade company recipe vendors at the Crossroads (Durelle 248196,
-- Gormak 248197, Azabek 248198, Beneris 248199, Fizzlefuse 248200, Pawani 248201, Jimbek 248202). The vendors use these
-- ItemExtendedCost IDs (official sniffs 2026-10-10), but neither the client data (70338) nor any hotfix of the sniffs has them, and
-- the server drops vendor items with an unknown cost. Known rows of the same vendors (client ItemExtendedCost):
--   Durelle    11839 45 | 11841 120 | 11842 180 | 11843 240      Gormak 11844 30 | 11846 60 | 11847 90
--   Fizzlefuse 11862 270 | 11863 360
-- the Gormak ladder is half of the Durelle one and the Fizzlefuse expert tier is 1.5 times, so the missing ones are filled the same way
-- (estimates until the official prices are known). Tiers: Apprentice no condition, Journeyman PlayerCondition 163380 (Friendly),
-- Expert 163433 (Honored) with the trade company (factions 2586 / 2587). Server only (no hotfix_data).
DELETE FROM `item_extended_cost` WHERE `ID` IN (11840,11845,11848,11849,11850,11851,11852,11855,11856,11857,11858,11859,11860,11861,
  11864,11865,11866,11867,11869,11870,11871,11872);
INSERT INTO `item_extended_cost` (`ID`,`CurrencyID1`,`CurrencyCount1`,`VerifiedBuild`) VALUES
(11840,3402,90,0),                                          -- Durelle      apprentice 2
(11845,3402,45,0),                                          -- Gormak      apprentice 2
(11848,3402,120,0),                                         -- Gormak      expert 2
(11849,3402,45,0),(11850,3402,90,0),(11851,3402,120,0),(11852,3402,180,0),     -- Azabek
(11855,3402,45,0),(11856,3402,120,0),(11857,3402,180,0),(11858,3402,240,0),    -- Beneris
(11859,3402,60,0),(11860,3402,135,0),(11861,3402,180,0),                       -- Fizzlefuse
(11864,3402,45,0),(11865,3402,90,0),(11866,3402,120,0),(11867,3402,180,0),     -- Pawani
(11869,3402,45,0),(11870,3402,90,0),(11871,3402,120,0),(11872,3402,180,0);     -- Jimbek
