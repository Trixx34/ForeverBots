-- Classic 1.60: Glimmering Staff 249392 (Formula: Glimmering Staff 249482, enchanting) has the tooltip line
-- Equip: Increases damage and healing done by magical spells and effects by up to 34. The client gets the item effect from an
-- encrypted ItemEffect/ItemXItemEffect section (key DA0E7785727A0A65, not public), so the server had no equip spell and the
-- bonus never applied. Spell 18052 (Increase Spell Dam 34: aura 13 and aura 135, 34 each) matches the tooltip.
-- Server only (no hotfix_data): our own IDs, the client already shows the effect from its own data.
DELETE FROM `item_effect` WHERE `ID` = 9900001;
INSERT INTO `item_effect` (`ID`,`LegacySlotIndex`,`TriggerType`,`Charges`,`CoolDownMSec`,`CategoryCoolDownMSec`,`SpellCategoryID`,`SpellID`,`ChrSpecializationID`,`PlayerConditionID`,`VerifiedBuild`) VALUES
(9900001,0,1,0,-1,-1,0,18052,0,0,0);
DELETE FROM `item_x_item_effect` WHERE `ID` = 9900001;
INSERT INTO `item_x_item_effect` (`ID`,`ItemEffectID`,`ItemID`,`VerifiedBuild`) VALUES
(9900001,9900001,249392,0);
