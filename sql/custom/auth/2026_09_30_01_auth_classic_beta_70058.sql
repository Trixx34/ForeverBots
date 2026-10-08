-- WoW Classic beta client (wow_classic_beta, WowB.exe) 1.60.1.70058
DELETE FROM `build_info` WHERE `build`=70058;
INSERT INTO `build_info` (`build`,`majorVersion`,`minorVersion`,`bugfixVersion`,`hotfixVersion`) VALUES
(70058,1,60,1,NULL);

-- No auth key is known for Win-x64-WoWB 70058 yet; see Network.SkipBuildAuthKeyCheck in worldserver.conf

UPDATE `realmlist` SET `gamebuild`=70058 WHERE `id` IN (70,71,72,73);
