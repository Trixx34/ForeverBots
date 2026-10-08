-- Classic 1.60: vanilla transports (VMaNGOS set) plus the two Skyborne airships to Zephras Isle (map 2991).
-- Post-vanilla transports are removed: their maps and taxi paths do not exist in the Classic client.
--
-- Skyborne airships: taxi paths from the Classic client TaxiPathNode.db2 (60 s stops, map-changing nodes)
--   11398  Eastern Kingdoms (Lordamere Lake dock 544.8, 424.2, 107.0) <-> Zephras Isle (1848.3, 569.1, 642.7)
--   11457  Kalimdor (Mulgore dock -802.0, 366.5, 188.0) <-> Zephras Isle (1952.5, 1019.1, 659.9)
-- Templates are custom (900000/900001, copied from the vanilla zeppelin 175080) using the new Classic airship models
-- (GameObjectDisplayInfo 126793/126794, WMO 93.8 x 31.6 x 58.7).

DELETE FROM `transports` WHERE `entry` NOT IN (20808, 164871, 175080, 176231, 176244, 176310, 176495, 177233, 181056, 900000, 900001);

-- airship templates
DELETE FROM `gameobject_template` WHERE `entry` IN (900000, 900001);
DELETE FROM `gameobject_template_addon` WHERE `entry` IN (900000, 900001);
DROP TEMPORARY TABLE IF EXISTS `tmp_classic_airship`;
CREATE TEMPORARY TABLE `tmp_classic_airship` AS SELECT * FROM `gameobject_template` WHERE `entry` = 175080;
UPDATE `tmp_classic_airship` SET `entry` = 900000, `displayId` = 126793, `name` = 'Skyborne Airship (Eastern Kingdoms - Zephras Isle)', `Data0` = 11398, `VerifiedBuild` = 0;
INSERT INTO `gameobject_template` SELECT * FROM `tmp_classic_airship`;
UPDATE `tmp_classic_airship` SET `entry` = 900001, `displayId` = 126794, `name` = 'Skyborne Airship (Kalimdor - Zephras Isle)', `Data0` = 11457;
INSERT INTO `gameobject_template` SELECT * FROM `tmp_classic_airship`;
DROP TEMPORARY TABLE `tmp_classic_airship`;
INSERT INTO `gameobject_template_addon` (`entry`, `faction`, `flags`) VALUES (900000, 0, 40), (900001, 0, 40);

-- vanilla transports missing from the TrinityCore list, and the airships
DELETE FROM `transports` WHERE `entry` IN (176244, 176310, 176495, 164871, 177233, 181056, 900000, 900001);
INSERT INTO `transports` (`guid`, `entry`, `name`, `phaseUseFlags`, `phaseid`, `phasegroup`, `ScriptName`) VALUES
(50, 176310, 'Menethil Harbor, Wetlands and Auberdine, Darkshore ("The Bravery")', 0, 0, 0, ''),
(51, 176244, 'Auberdine, Darkshore and Rut''theran Village, Teldrassil ("The Moonspray")', 0, 0, 0, ''),
(52, 176495, 'Undercity, Tirisfal Glades and Grom''gol Base Camp, Stranglethorn Vale ("The Purple Princess")', 0, 0, 0, ''),
(53, 164871, 'Orgrimmar, Durotar and Undercity, Tirisfal Glades ("The Thundercaller")', 0, 0, 0, ''),
(54, 177233, 'Feathermoon Stronghold and Forgotten Coast, Feralas ("Feathermoon Ferry")', 0, 0, 0, ''),
(55, 181056, 'Naxxramas', 0, 0, 0, ''),
(56, 900000, 'Lordamere Lake, Eastern Kingdoms and Zephras Isle ("Skyborne Airship")', 0, 0, 0, ''),
(57, 900001, 'Mulgore, Kalimdor and Zephras Isle ("Skyborne Airship")', 0, 0, 0, '');
