-- WoW Classic beta client (wow_classic_beta, WowB.exe) 1.60.1.70235
DELETE FROM `build_info` WHERE `build`=70235;
INSERT INTO `build_info` (`build`,`majorVersion`,`minorVersion`,`bugfixVersion`,`hotfixVersion`) VALUES
(70235,1,60,1,NULL);

-- No auth key is known for Win-x64-WoWB 70235 yet; see Network.SkipBuildAuthKeyCheck in worldserver.conf

UPDATE `realmlist` SET `gamebuild`=70235 WHERE `id` IN (70,71,72,73);
