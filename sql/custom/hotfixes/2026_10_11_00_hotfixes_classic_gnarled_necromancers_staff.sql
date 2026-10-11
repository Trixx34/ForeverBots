-- Classic 1.60: Gnarled Necromancer's Staff 251534 (Ruins of Lordaeron quest reward). Tooltip: "Equip: Increases damage and healing done
-- by magical spells and effects by up to 26." and "Equip: Increases movement speed by 2% in Silverpine Forest and Hillsbrad Foothills."
-- The item effects are in an encrypted section; the spells match the tooltip: Increase Spell Dam 26 (18049: aura 13 and 135, 26) and
-- Forsaken Weapons (1309369: aura 129, 2%, RequiredAreasID 9227 = Silverpine Forest 130 + Hillsbrad Foothills 267; re-applied on
-- entering those zones by Player::UpdateAreaDependentAuras). Server only (no hotfix_data): our own IDs.
DELETE FROM `item_effect` WHERE `ID` IN (9900002,9900003);
INSERT INTO `item_effect` (`ID`,`LegacySlotIndex`,`TriggerType`,`Charges`,`CoolDownMSec`,`CategoryCoolDownMSec`,`SpellCategoryID`,`SpellID`,`ChrSpecializationID`,`PlayerConditionID`,`VerifiedBuild`) VALUES
(9900002,0,1,0,-1,-1,0,18049,0,0,0),
(9900003,1,1,0,-1,-1,0,1309369,0,0,0);
DELETE FROM `item_x_item_effect` WHERE `ID` IN (9900002,9900003);
INSERT INTO `item_x_item_effect` (`ID`,`ItemEffectID`,`ItemID`,`VerifiedBuild`) VALUES
(9900002,9900002,251534,0),
(9900003,9900003,251534,0);
