-- Classic 1.60 dual spec: class trainers sell a Dual Talent Specialization for 50 gold (level 10+, once).
-- One option per class trainer menu (the menus that offer "I wish to unlearn my talents."); GossipOptionID 95000000 + MenuID
-- is recognised by Player::IsClassicDualSpecGossipOption, which hides it for other classes, below level 10 or once bought.
DELETE FROM `gossip_menu_option` WHERE `GossipOptionID` BETWEEN 95000000 AND 95999999;
INSERT INTO `gossip_menu_option` (`MenuID`, `GossipOptionID`, `OptionID`, `OptionNpc`, `OptionText`, `OptionBroadcastTextID`, `Language`, `Flags`,
    `ActionMenuID`, `ActionPoiID`, `GossipNpcOptionID`, `BoxCoded`, `BoxMoney`, `BoxText`, `BoxBroadcastTextID`, `SpellID`, `OverrideIconID`, `VerifiedBuild`)
SELECT m.`MenuID`, 95000000 + m.`MenuID`, m.`NextOptionID`, 0, 'I wish to purchase a Dual Talent Specialization.', 0, 0, 0,
    0, 0, NULL, 0, 500000, 'Are you sure you wish to purchase a Dual Talent Specialization?', 0, NULL, NULL, 0
FROM (SELECT o.`MenuID`, MAX(o.`OptionID`) + 1 AS `NextOptionID` FROM `gossip_menu_option` o
      WHERE o.`MenuID` IN (SELECT t.`MenuID` FROM `gossip_menu_option` t WHERE t.`OptionNpc` = 11) AND o.`MenuID` < 1000000
      GROUP BY o.`MenuID`) m;
