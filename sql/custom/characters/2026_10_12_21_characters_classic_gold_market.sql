-- Classic 1.60 in-game Store, Gold Market tab: gold offered by players for shop points. Buyers see only the amount and price;
-- the seller is kept for GMs (support site). The seller's gold is taken when listing; on sale the seller's account gets the
-- price minus Classic.Shop.GoldMarketFee percent. SellerAccount 0 = listed by the server (tests).

CREATE TABLE IF NOT EXISTS `shop_gold_listing` (
  `ID` int unsigned NOT NULL AUTO_INCREMENT,
  `SellerAccount` int unsigned NOT NULL DEFAULT 0,
  `SellerGuid` bigint unsigned NOT NULL DEFAULT 0,
  `Gold` int unsigned NOT NULL COMMENT 'gold (not copper)',
  `Price` int unsigned NOT NULL COMMENT 'shop points',
  `Created` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`ID`),
  KEY `idx_seller` (`SellerAccount`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Gold Market listings';
