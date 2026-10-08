-- Classic 1.60: one realm per ruleset, each served by its own worldserver (worldserver_pvp.conf, _rp, _hc: RealmID, WorldServerPort,
-- CharacterDatabaseInfo characters_pvp / _rp / _hc). Realm 70 (Normal) comes from 2026_09_28_00_auth_classic_realm.sql.
-- contentSetId = the rulesets season (136 PvP, 138 Roleplay, 140 Hardcore); icon/timezone like the retail realm types.
INSERT IGNORE INTO `realmlist` (`id`,`name`,`address`,`localAddress`,`localSubnetMask`,`port`,`icon`,`flag`,`timezone`,`allowedSecurityLevel`,`population`,`gamebuild`,`Region`,`Battlegroup`,`contentSetId`) VALUES
(71,'Forever PvP','127.0.0.1','127.0.0.1','255.255.255.0',8095,1,0,1,0,0,70124,70,1,136),
(72,'Forever RP','127.0.0.1','127.0.0.1','255.255.255.0',8105,6,0,1,0,0,70124,70,1,138),
(73,'Forever Hardcore','127.0.0.1','127.0.0.1','255.255.255.0',8115,0,0,1,0,0,70124,70,1,140);
