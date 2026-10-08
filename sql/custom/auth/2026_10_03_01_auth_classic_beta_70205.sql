-- WoW Classic beta client (wow_classic_beta, WowB.exe) 1.60.1.70205
DELETE FROM `build_info` WHERE `build`=70205;
INSERT INTO `build_info` (`build`,`majorVersion`,`minorVersion`,`bugfixVersion`,`hotfixVersion`) VALUES
(70205,1,60,1,NULL);

-- No auth key is known for Win-x64-WoWB 70205 yet; see Network.SkipBuildAuthKeyCheck in worldserver.conf

UPDATE `realmlist` SET `gamebuild`=70205 WHERE `id` IN (70,71,72,73);
