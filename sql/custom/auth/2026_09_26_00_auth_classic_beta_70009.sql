-- WoW Classic beta client (wow_classic_beta, WowB.exe) 1.60.1.70009
DELETE FROM `build_info` WHERE `build`=70009;
INSERT INTO `build_info` (`build`,`majorVersion`,`minorVersion`,`bugfixVersion`,`hotfixVersion`) VALUES
(70009,1,60,1,NULL);

-- No auth key is known for Win-x64-WoWB 70009 yet; see Network.SkipBuildAuthKeyCheck in worldserver.conf

UPDATE `realmlist` SET `gamebuild`=70009 WHERE `id`=1;
