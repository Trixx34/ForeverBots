-- WoW Classic beta client (wow_classic_beta, WowB.exe) 1.60.1.70334
DELETE FROM `build_info` WHERE `build`=70334;
INSERT INTO `build_info` (`build`,`majorVersion`,`minorVersion`,`bugfixVersion`,`hotfixVersion`) VALUES
(70334,1,60,1,NULL);

-- No auth key is known for Win-x64-WoWB 70334 yet; see Network.SkipBuildAuthKeyCheck in worldserver.conf

UPDATE `realmlist` SET `gamebuild`=70334 WHERE `id` IN (70,71,72,73);
