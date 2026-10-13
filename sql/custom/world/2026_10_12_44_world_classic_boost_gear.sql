-- Classic 1.60 level-60 character boost (BoostType 6): a green starter set per class, mailed by
-- WorldSession::HandleBattlePayDistributionAssignToTarget (TokenHandler.cpp).
-- RandomPropertyID = the ItemBonusList ID of the random suffix ("... of the Tiger"): in this core the vanilla random
-- properties are bonus lists (ItemBonus.db2 type 2 = stat + allocation, type 5 = suffix name), picked from
-- item_random_bonus_list_template via item_template_addon.RandomBonusListTemplateId. 0 = roll from the item's pool (or none).
-- Suffix bonus lists used: 12718 Eagle (Int/Sta), 12721 Monkey (Agi/Sta), 12722 Bear (Str/Sta), 12724 Falcon (Agi/Int),
-- 12727 Tiger (Str/Agi), 12756 Arcane Wrath, 12758 Frozen Wrath, 12761 Shadow Wrath (spell damage of one school).
-- Stat amounts below are for the item's level (RandPropPoints, uncommon) as the client and server compute them.

DROP TABLE IF EXISTS `shop_boost_item`;
CREATE TABLE `shop_boost_item` (
  `BoostType` int unsigned NOT NULL DEFAULT 6 COMMENT '6 = level 60',
  `ClassID` tinyint unsigned NOT NULL DEFAULT 0 COMMENT '0 = all classes',
  `ItemID` int unsigned NOT NULL,
  `Count` int unsigned NOT NULL DEFAULT 1,
  `RandomPropertyID` int unsigned NOT NULL DEFAULT 0 COMMENT 'ItemBonusList ID of the random suffix, 0 = none / random',
  PRIMARY KEY (`BoostType`,`ClassID`,`ItemID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Items mailed with a character boost';

-- Warrior (ClassID 1)
--   Head        10379 Commander's Helm of the Tiger, ilvl 61 req 56: +18 Agility, +18 Strength
--   Shoulders   10383 Commander's Pauldrons of the Tiger, ilvl 61 req 56: +13 Agility, +13 Strength
--   Chest       10378 Commander's Armor of the Tiger, ilvl 63 req 58: +19 Agility, +19 Strength
--   Wrists      10377 Commander's Vambraces of the Tiger, ilvl 59 req 54: +9 Agility, +9 Strength
--   Hands       10380 Commander's Gauntlets of the Tiger, ilvl 60 req 55: +13 Agility, +13 Strength
--   Waist       10381 Commander's Girdle of the Tiger, ilvl 59 req 54: +13 Agility, +13 Strength
--   Legs        10382 Commander's Leggings of the Tiger, ilvl 62 req 57: +18 Agility, +18 Strength
--   Feet        10376 Commander's Boots of the Tiger, ilvl 60 req 55: +13 Agility, +13 Strength
--   Back        10258 Adventurer's Cape of the Tiger, ilvl 60 req 55: +10 Agility, +10 Strength
--   Neck        12036 Granite Necklace of the Tiger, ilvl 62 req 57: +10 Agility, +10 Strength
--   Finger      12016 Jungle Ring of the Tiger, ilvl 61 req 56: +10 Agility, +10 Strength
--   Finger      12015 Swamp Ring of the Tiger, ilvl 57 req 52: +9 Agility, +9 Strength
--   Two-Hand    15273 Death Striker of the Tiger, ilvl 63 req 58: +19 Agility, +19 Strength
--   Bag          4500 Traveler's Backpack x4, ilvl 55 req 0: 16 slots
INSERT INTO `shop_boost_item` (`BoostType`,`ClassID`,`ItemID`,`Count`,`RandomPropertyID`) VALUES
(6,1,10379,1,12727),
(6,1,10383,1,12727),
(6,1,10378,1,12727),
(6,1,10377,1,12727),
(6,1,10380,1,12727),
(6,1,10381,1,12727),
(6,1,10382,1,12727),
(6,1,10376,1,12727),
(6,1,10258,1,12727),
(6,1,12036,1,12727),
(6,1,12016,1,12727),
(6,1,12015,1,12727),
(6,1,15273,1,12727),
(6,1,4500,4,0);

-- Paladin (ClassID 2)
--   Head        10379 Commander's Helm of the Bear, ilvl 61 req 56: +18 Stamina, +18 Strength
--   Shoulders   10383 Commander's Pauldrons of the Bear, ilvl 61 req 56: +13 Stamina, +13 Strength
--   Chest       10378 Commander's Armor of the Bear, ilvl 63 req 58: +19 Stamina, +19 Strength
--   Wrists      10377 Commander's Vambraces of the Bear, ilvl 59 req 54: +9 Stamina, +9 Strength
--   Hands       10380 Commander's Gauntlets of the Bear, ilvl 60 req 55: +13 Stamina, +13 Strength
--   Waist       10381 Commander's Girdle of the Bear, ilvl 59 req 54: +13 Stamina, +13 Strength
--   Legs        10382 Commander's Leggings of the Bear, ilvl 62 req 57: +18 Stamina, +18 Strength
--   Feet        10376 Commander's Boots of the Bear, ilvl 60 req 55: +13 Stamina, +13 Strength
--   Back        15437 Supreme Cape of the Bear, ilvl 61 req 56: +10 Stamina, +10 Strength
--   Neck        12027 Vermilion Necklace of the Bear, ilvl 63 req 58: +10 Stamina, +10 Strength
--   Finger      12005 Granite Ring of the Bear, ilvl 62 req 57: +10 Stamina, +10 Strength
--   Finger      12016 Jungle Ring of the Bear, ilvl 61 req 56: +10 Stamina, +10 Strength
--   Two-Hand    15267 Brutehammer of the Bear, ilvl 63 req 58: +19 Stamina, +19 Strength
--   Bag          4500 Traveler's Backpack x4, ilvl 55 req 0: 16 slots
INSERT INTO `shop_boost_item` (`BoostType`,`ClassID`,`ItemID`,`Count`,`RandomPropertyID`) VALUES
(6,2,10379,1,12722),
(6,2,10383,1,12722),
(6,2,10378,1,12722),
(6,2,10377,1,12722),
(6,2,10380,1,12722),
(6,2,10381,1,12722),
(6,2,10382,1,12722),
(6,2,10376,1,12722),
(6,2,15437,1,12722),
(6,2,12027,1,12722),
(6,2,12005,1,12722),
(6,2,12016,1,12722),
(6,2,15267,1,12722),
(6,2,4500,4,0);

-- Hunter (ClassID 3)
--   Head        15670 Magnificent Helmet of the Falcon, ilvl 61 req 56: +18 Agility, +18 Intellect
--   Shoulders   15677 Magnificent Shoulders of the Falcon, ilvl 61 req 56: +13 Agility, +13 Intellect
--   Chest       15669 Magnificent Breastplate of the Falcon, ilvl 62 req 57: +18 Agility, +18 Intellect
--   Wrists      15668 Magnificent Bracers of the Falcon, ilvl 58 req 53: +9 Agility, +9 Intellect
--   Hands       15672 Magnificent Gauntlets of the Falcon, ilvl 60 req 55: +13 Agility, +13 Intellect
--   Waist       15673 Magnificent Belt of the Falcon, ilvl 58 req 53: +13 Agility, +13 Intellect
--   Legs        15676 Magnificent Leggings of the Falcon, ilvl 61 req 56: +18 Agility, +18 Intellect
--   Feet        15674 Magnificent Greaves of the Falcon, ilvl 60 req 55: +13 Agility, +13 Intellect
--   Back        10267 Masterwork Cape of the Falcon, ilvl 61 req 56: +10 Agility, +10 Intellect
--   Neck        12027 Vermilion Necklace of the Falcon, ilvl 63 req 58: +10 Agility, +10 Intellect
--   Finger      11992 Vermilion Band of the Falcon, ilvl 63 req 58: +10 Agility, +10 Intellect
--   Finger      12005 Granite Ring of the Falcon, ilvl 62 req 57: +10 Agility, +10 Intellect
--   Ranged      15296 Hawkeye Bow of the Falcon, ilvl 63 req 58: +19 Agility, +19 Intellect
--   Two-Hand    15273 Death Striker of the Monkey, ilvl 63 req 58: +19 Agility, +19 Stamina
--   Bag          4500 Traveler's Backpack x3, ilvl 55 req 0: 16 slots
--   Bag          8217 Quickdraw Quiver, ilvl 45 req 40: 16 slots
--   Ammo        11285 Jagged Arrow x1000, ilvl 45 req 40
INSERT INTO `shop_boost_item` (`BoostType`,`ClassID`,`ItemID`,`Count`,`RandomPropertyID`) VALUES
(6,3,15670,1,12724),
(6,3,15677,1,12724),
(6,3,15669,1,12724),
(6,3,15668,1,12724),
(6,3,15672,1,12724),
(6,3,15673,1,12724),
(6,3,15676,1,12724),
(6,3,15674,1,12724),
(6,3,10267,1,12724),
(6,3,12027,1,12724),
(6,3,11992,1,12724),
(6,3,12005,1,12724),
(6,3,15296,1,12724),
(6,3,15273,1,12721),
(6,3,4500,3,0),
(6,3,8217,1,0),
(6,3,11285,1000,0);

-- Rogue (ClassID 4)
--   Head        10226 Nightshade Helmet of the Monkey, ilvl 61 req 56: +18 Agility, +18 Stamina
--   Shoulders   10228 Nightshade Spaulders of the Monkey, ilvl 60 req 55: +13 Agility, +13 Stamina
--   Chest       10220 Nightshade Tunic of the Monkey, ilvl 63 req 58: +19 Agility, +19 Stamina
--   Wrists      10223 Nightshade Armguards of the Monkey, ilvl 58 req 53: +9 Agility, +9 Stamina
--   Hands       10225 Nightshade Gloves of the Monkey, ilvl 59 req 54: +13 Agility, +13 Stamina
--   Waist       10221 Nightshade Girdle of the Monkey, ilvl 58 req 53: +13 Agility, +13 Stamina
--   Legs        10227 Nightshade Leggings of the Monkey, ilvl 62 req 57: +18 Agility, +18 Stamina
--   Feet        10222 Nightshade Boots of the Monkey, ilvl 59 req 54: +13 Agility, +13 Stamina
--   Back        10224 Nightshade Cloak of the Monkey, ilvl 57 req 52: +9 Agility, +9 Stamina
--   Neck        12036 Granite Necklace of the Monkey, ilvl 62 req 57: +10 Agility, +10 Stamina
--   Finger      11992 Vermilion Band of the Monkey, ilvl 63 req 58: +10 Agility, +10 Stamina
--   Finger      12005 Granite Ring of the Monkey, ilvl 62 req 57: +10 Agility, +10 Stamina
--   Main Hand   15220 Battlefell Sabre of the Monkey, ilvl 62 req 57: +7 Agility, +7 Stamina
--   One-Hand    15246 Demon Blade of the Monkey, ilvl 62 req 57: +7 Agility, +7 Stamina
--   Bag          4500 Traveler's Backpack x4, ilvl 55 req 0: 16 slots
INSERT INTO `shop_boost_item` (`BoostType`,`ClassID`,`ItemID`,`Count`,`RandomPropertyID`) VALUES
(6,4,10226,1,12721),
(6,4,10228,1,12721),
(6,4,10220,1,12721),
(6,4,10223,1,12721),
(6,4,10225,1,12721),
(6,4,10221,1,12721),
(6,4,10227,1,12721),
(6,4,10222,1,12721),
(6,4,10224,1,12721),
(6,4,12036,1,12721),
(6,4,11992,1,12721),
(6,4,12005,1,12721),
(6,4,15220,1,12721),
(6,4,15246,1,12721),
(6,4,4500,4,0);

-- Priest (ClassID 5)
--   Head        14312 Celestial Crown of the Eagle, ilvl 61 req 56: +18 Intellect, +18 Stamina
--   Shoulders   14316 Celestial Pauldrons of the Eagle, ilvl 59 req 54: +13 Intellect, +13 Stamina
--   Chest       14317 Celestial Silk Robes of the Eagle, ilvl 61 req 56: +18 Intellect, +18 Stamina
--   Wrists      14311 Celestial Bindings of the Eagle, ilvl 55 req 50: +9 Intellect, +9 Stamina
--   Hands       14314 Celestial Handwraps of the Eagle, ilvl 58 req 53: +13 Intellect, +13 Stamina
--   Waist       14309 Celestial Belt of the Eagle, ilvl 56 req 51: +12 Intellect, +12 Stamina
--   Legs        14315 Celestial Kilt of the Eagle, ilvl 59 req 54: +17 Intellect, +17 Stamina
--   Feet        14310 Celestial Slippers of the Eagle, ilvl 58 req 53: +13 Intellect, +13 Stamina
--   Back        14313 Celestial Cape of the Eagle, ilvl 54 req 49: +9 Intellect, +9 Stamina
--   Neck        12027 Vermilion Necklace of the Eagle, ilvl 63 req 58: +10 Intellect, +10 Stamina
--   Finger      11992 Vermilion Band of the Eagle, ilvl 63 req 58: +10 Intellect, +10 Stamina
--   Finger      12005 Granite Ring of the Eagle, ilvl 62 req 57: +10 Intellect, +10 Stamina
--   Two-Hand    15278 Solstice Staff of Shadow Wrath, ilvl 60 req 55: +37 Shadow Damage
--   Ranged      15282 Dragon Finger of the Eagle, ilvl 60 req 55: +7 Intellect, +7 Stamina
--   Bag          4500 Traveler's Backpack x4, ilvl 55 req 0: 16 slots
INSERT INTO `shop_boost_item` (`BoostType`,`ClassID`,`ItemID`,`Count`,`RandomPropertyID`) VALUES
(6,5,14312,1,12718),
(6,5,14316,1,12718),
(6,5,14317,1,12718),
(6,5,14311,1,12718),
(6,5,14314,1,12718),
(6,5,14309,1,12718),
(6,5,14315,1,12718),
(6,5,14310,1,12718),
(6,5,14313,1,12718),
(6,5,12027,1,12718),
(6,5,11992,1,12718),
(6,5,12005,1,12718),
(6,5,15278,1,12761),
(6,5,15282,1,12718),
(6,5,4500,4,0);

-- Shaman (ClassID 7)
--   Head        15670 Magnificent Helmet of the Tiger, ilvl 61 req 56: +18 Agility, +18 Strength
--   Shoulders   15677 Magnificent Shoulders of the Tiger, ilvl 61 req 56: +13 Agility, +13 Strength
--   Chest       15669 Magnificent Breastplate of the Tiger, ilvl 62 req 57: +18 Agility, +18 Strength (suffix not in the item's own random pool)
--   Wrists      15668 Magnificent Bracers of the Tiger, ilvl 58 req 53: +9 Agility, +9 Strength
--   Hands       15672 Magnificent Gauntlets of the Tiger, ilvl 60 req 55: +13 Agility, +13 Strength
--   Waist       15673 Magnificent Belt of the Tiger, ilvl 58 req 53: +13 Agility, +13 Strength
--   Legs        15676 Magnificent Leggings of the Tiger, ilvl 61 req 56: +18 Agility, +18 Strength (suffix not in the item's own random pool)
--   Feet        15674 Magnificent Greaves of the Tiger, ilvl 60 req 55: +13 Agility, +13 Strength (suffix not in the item's own random pool)
--   Back        10258 Adventurer's Cape of the Tiger, ilvl 60 req 55: +10 Agility, +10 Strength
--   Neck        12036 Granite Necklace of the Tiger, ilvl 62 req 57: +10 Agility, +10 Strength
--   Finger      12016 Jungle Ring of the Tiger, ilvl 61 req 56: +10 Agility, +10 Strength
--   Finger      12015 Swamp Ring of the Tiger, ilvl 57 req 52: +9 Agility, +9 Strength
--   Two-Hand    15278 Solstice Staff of the Tiger, ilvl 60 req 55: +17 Agility, +17 Strength (suffix not in the item's own random pool)
--   Bag          4500 Traveler's Backpack x4, ilvl 55 req 0: 16 slots
INSERT INTO `shop_boost_item` (`BoostType`,`ClassID`,`ItemID`,`Count`,`RandomPropertyID`) VALUES
(6,7,15670,1,12727),
(6,7,15677,1,12727),
(6,7,15669,1,12727),
(6,7,15668,1,12727),
(6,7,15672,1,12727),
(6,7,15673,1,12727),
(6,7,15676,1,12727),
(6,7,15674,1,12727),
(6,7,10258,1,12727),
(6,7,12036,1,12727),
(6,7,12016,1,12727),
(6,7,12015,1,12727),
(6,7,15278,1,12727),
(6,7,4500,4,0);

-- Mage (ClassID 8)
--   Head        10219 Elegant Circlet of the Eagle, ilvl 60 req 55: +17 Intellect, +17 Stamina
--   Shoulders   10210 Elegant Mantle of the Eagle, ilvl 60 req 55: +13 Intellect, +13 Stamina
--   Chest       10215 Elegant Robes of the Eagle, ilvl 62 req 57: +18 Intellect, +18 Stamina
--   Wrists      10213 Elegant Bracers of the Eagle, ilvl 58 req 53: +9 Intellect, +9 Stamina
--   Hands       10214 Elegant Gloves of the Eagle, ilvl 59 req 54: +13 Intellect, +13 Stamina
--   Waist       10216 Elegant Belt of the Eagle, ilvl 58 req 53: +13 Intellect, +13 Stamina
--   Legs        10217 Elegant Leggings of the Eagle, ilvl 61 req 56: +18 Intellect, +18 Stamina
--   Feet        10211 Elegant Boots of the Eagle, ilvl 59 req 54: +13 Intellect, +13 Stamina
--   Back        10212 Elegant Cloak of the Eagle, ilvl 57 req 52: +9 Intellect, +9 Stamina
--   Neck        12027 Vermilion Necklace of the Eagle, ilvl 63 req 58: +10 Intellect, +10 Stamina
--   Finger      11992 Vermilion Band of the Eagle, ilvl 63 req 58: +10 Intellect, +10 Stamina
--   Finger      12005 Granite Ring of the Eagle, ilvl 62 req 57: +10 Intellect, +10 Stamina
--   Two-Hand    15278 Solstice Staff of Frozen Wrath, ilvl 60 req 55: +37 Frost Damage
--   Ranged      15282 Dragon Finger of the Eagle, ilvl 60 req 55: +7 Intellect, +7 Stamina
--   Bag          4500 Traveler's Backpack x4, ilvl 55 req 0: 16 slots
INSERT INTO `shop_boost_item` (`BoostType`,`ClassID`,`ItemID`,`Count`,`RandomPropertyID`) VALUES
(6,8,10219,1,12718),
(6,8,10210,1,12718),
(6,8,10215,1,12718),
(6,8,10213,1,12718),
(6,8,10214,1,12718),
(6,8,10216,1,12718),
(6,8,10217,1,12718),
(6,8,10211,1,12718),
(6,8,10212,1,12718),
(6,8,12027,1,12718),
(6,8,11992,1,12718),
(6,8,12005,1,12718),
(6,8,15278,1,12758),
(6,8,15282,1,12718),
(6,8,4500,4,0);

-- Warlock (ClassID 9)
--   Head        14322 Resplendent Circlet of the Eagle, ilvl 62 req 57: +18 Intellect, +18 Stamina
--   Shoulders   14325 Resplendent Epaulets of the Eagle, ilvl 61 req 56: +13 Intellect, +13 Stamina
--   Chest       14326 Resplendent Robes of the Eagle, ilvl 63 req 58: +19 Intellect, +19 Stamina
--   Wrists      14320 Resplendent Bracelets of the Eagle, ilvl 57 req 52: +9 Intellect, +9 Stamina
--   Hands       14323 Resplendent Gauntlets of the Eagle, ilvl 60 req 55: +13 Intellect, +13 Stamina
--   Waist       14327 Resplendent Belt of the Eagle, ilvl 58 req 53: +13 Intellect, +13 Stamina
--   Legs        14324 Resplendent Sarong of the Eagle, ilvl 60 req 55: +17 Intellect, +17 Stamina
--   Feet        14319 Resplendent Boots of the Eagle, ilvl 59 req 54: +13 Intellect, +13 Stamina
--   Back        14321 Resplendent Cloak of the Eagle, ilvl 55 req 50: +9 Intellect, +9 Stamina
--   Neck        12027 Vermilion Necklace of the Eagle, ilvl 63 req 58: +10 Intellect, +10 Stamina
--   Finger      11992 Vermilion Band of the Eagle, ilvl 63 req 58: +10 Intellect, +10 Stamina
--   Finger      12005 Granite Ring of the Eagle, ilvl 62 req 57: +10 Intellect, +10 Stamina
--   Two-Hand    15278 Solstice Staff of Shadow Wrath, ilvl 60 req 55: +37 Shadow Damage
--   Ranged      15282 Dragon Finger of the Eagle, ilvl 60 req 55: +7 Intellect, +7 Stamina
--   Bag          4500 Traveler's Backpack x4, ilvl 55 req 0: 16 slots
INSERT INTO `shop_boost_item` (`BoostType`,`ClassID`,`ItemID`,`Count`,`RandomPropertyID`) VALUES
(6,9,14322,1,12718),
(6,9,14325,1,12718),
(6,9,14326,1,12718),
(6,9,14320,1,12718),
(6,9,14323,1,12718),
(6,9,14327,1,12718),
(6,9,14324,1,12718),
(6,9,14319,1,12718),
(6,9,14321,1,12718),
(6,9,12027,1,12718),
(6,9,11992,1,12718),
(6,9,12005,1,12718),
(6,9,15278,1,12761),
(6,9,15282,1,12718),
(6,9,4500,4,0);

-- Druid (ClassID 11)
--   Head        15193 Grand Crown of the Eagle, ilvl 62 req 57: +18 Intellect, +18 Stamina
--   Shoulders   15693 Grand Shoulders of the Eagle, ilvl 59 req 54: +13 Intellect, +13 Stamina
--   Chest       15195 Grand Breastplate of the Eagle, ilvl 62 req 57: +18 Intellect, +18 Stamina
--   Wrists      15188 Grand Armguards of the Eagle, ilvl 57 req 52: +9 Intellect, +9 Stamina
--   Hands       15192 Grand Gauntlets of the Eagle, ilvl 60 req 55: +13 Intellect, +13 Stamina
--   Waist       15191 Grand Belt of the Eagle, ilvl 57 req 52: +12 Intellect, +12 Stamina
--   Legs        15194 Grand Legguards of the Eagle, ilvl 61 req 56: +18 Intellect, +18 Stamina
--   Feet        15189 Grand Boots of the Eagle, ilvl 60 req 55: +13 Intellect, +13 Stamina
--   Back        15190 Grand Cloak of the Eagle, ilvl 55 req 50: +9 Intellect, +9 Stamina
--   Neck        12027 Vermilion Necklace of the Eagle, ilvl 63 req 58: +10 Intellect, +10 Stamina
--   Finger      11992 Vermilion Band of the Eagle, ilvl 63 req 58: +10 Intellect, +10 Stamina
--   Finger      12005 Granite Ring of the Eagle, ilvl 62 req 57: +10 Intellect, +10 Stamina
--   Two-Hand    15278 Solstice Staff of Arcane Wrath, ilvl 60 req 55: +37 Arcane Damage
--   Bag          4500 Traveler's Backpack x4, ilvl 55 req 0: 16 slots
INSERT INTO `shop_boost_item` (`BoostType`,`ClassID`,`ItemID`,`Count`,`RandomPropertyID`) VALUES
(6,11,15193,1,12718),
(6,11,15693,1,12718),
(6,11,15195,1,12718),
(6,11,15188,1,12718),
(6,11,15192,1,12718),
(6,11,15191,1,12718),
(6,11,15194,1,12718),
(6,11,15189,1,12718),
(6,11,15190,1,12718),
(6,11,12027,1,12718),
(6,11,11992,1,12718),
(6,11,12005,1,12718),
(6,11,15278,1,12756),
(6,11,4500,4,0);
