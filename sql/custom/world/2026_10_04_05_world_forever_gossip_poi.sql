-- Classic 1.60 (WoW Forever): guard directions (map pins of gossip options) from ymir sniffs, classic_re sniff_gossip_poi.py.
-- Safe to run again; re-run after 2026_10_01_04 was regenerated (it rewrites these options) (last: 2026-10-05, sniff of 2026-10-04 20:05).
DELETE FROM `points_of_interest` WHERE `ID` IN (168,8795,8796,8797,8798,8800,8957,8958,8959,8960,8961,8962,8965,8968,9051);
INSERT INTO `points_of_interest` (`ID`,`PositionX`,`PositionY`,`PositionZ`,`Icon`,`Flags`,`Importance`,`Name`,`WMOGroupID`,`VerifiedBuild`) VALUES
(168,-1257.800,24.143,127.718,7,99,0,'Thunder Bluff Bank',0,70205),
(8795,2094.350,1025.280,672.030,7,99,0,'Valanaar Bank',82241,70205),
(8796,2261.930,772.279,679.623,7,99,0,'Valanaar Inn',82241,70205),
(8797,2220.380,766.780,676.664,7,99,0,'Valanaar Mailbox',82241,70205),
(8798,2096.810,1010.090,672.030,7,99,0,'Valanaar Auction House',82241,70205),
(8800,1962.170,1009.230,662.981,7,99,0,'Valanaar Windshapers Dock',83353,70205),
(8957,2268.090,899.957,675.262,7,99,0,'Rogue Trainer',82241,70205),
(8958,2255.200,899.962,675.262,7,99,0,'Warrior Trainer',82241,70205),
(8959,2263.270,917.336,675.262,7,99,0,'Hunter Trainer',82241,70205),
(8960,1968.750,564.714,658.738,7,99,0,'Mage Trainer',82241,70205),
(8961,2044.840,987.713,656.718,7,99,0,'Shaman Trainer',81792,70205),
(8962,2171.120,673.098,691.445,7,99,0,'Druid Trainer',82241,70205),
(8965,2135.940,914.600,668.643,7,99,0,'Skinning Trainer',82241,70205),
(8968,2126.210,936.568,668.645,7,99,0,'Enchanting Trainer',82241,70205),
(9051,-1224.240,152.938,133.200,7,99,0,'Thunder Bluff Barbershop',0,70205);
UPDATE `gossip_menu_option` SET `ActionPoiID` = 168 WHERE `MenuID` = 721 AND `GossipOptionID` = 96468;
UPDATE `gossip_menu_option` SET `ActionPoiID` = 9051 WHERE `MenuID` = 721 AND `GossipOptionID` = 143292;
UPDATE `gossip_menu_option` SET `ActionPoiID` = 8800 WHERE `MenuID` = 40936 AND `GossipOptionID` = 139958;
UPDATE `gossip_menu_option` SET `ActionPoiID` = 8798 WHERE `MenuID` = 40936 AND `GossipOptionID` = 139960;
UPDATE `gossip_menu_option` SET `ActionPoiID` = 8797 WHERE `MenuID` = 40936 AND `GossipOptionID` = 139961;
UPDATE `gossip_menu_option` SET `ActionPoiID` = 8796 WHERE `MenuID` = 40936 AND `GossipOptionID` = 139962;
UPDATE `gossip_menu_option` SET `ActionPoiID` = 8795 WHERE `MenuID` = 40936 AND `GossipOptionID` = 139963;
UPDATE `gossip_menu_option` SET `ActionPoiID` = 8968 WHERE `MenuID` = 44220 AND `GossipOptionID` = 141564;
UPDATE `gossip_menu_option` SET `ActionPoiID` = 8965 WHERE `MenuID` = 44220 AND `GossipOptionID` = 141574;
UPDATE `gossip_menu_option` SET `ActionPoiID` = 8962 WHERE `MenuID` = 44227 AND `GossipOptionID` = 141577;
UPDATE `gossip_menu_option` SET `ActionPoiID` = 8957 WHERE `MenuID` = 44227 AND `GossipOptionID` = 141578;
UPDATE `gossip_menu_option` SET `ActionPoiID` = 8961 WHERE `MenuID` = 44227 AND `GossipOptionID` = 141579;
UPDATE `gossip_menu_option` SET `ActionPoiID` = 8960 WHERE `MenuID` = 44227 AND `GossipOptionID` = 141580;
UPDATE `gossip_menu_option` SET `ActionPoiID` = 8959 WHERE `MenuID` = 44227 AND `GossipOptionID` = 141581;
UPDATE `gossip_menu_option` SET `ActionPoiID` = 8958 WHERE `MenuID` = 44227 AND `GossipOptionID` = 141582;
