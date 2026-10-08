-- Classic 1.60: retail Instance Portal visuals for vanilla instances (restored from pre-import backup)
DELETE FROM `gameobject` WHERE `guid` IN (301206,301207,400644,400646,400649,400711,400645,400647,400648,400651);
INSERT INTO `gameobject` (`guid`, `id`, `map`, `zoneId`, `areaId`, `spawnDifficulties`, `phaseUseFlags`, `PhaseId`, `PhaseGroup`, `terrainSwapMap`, `position_x`, `position_y`, `position_z`, `orientation`, `rotation0`, `rotation1`, `rotation2`, `rotation3`, `spawntimesecs`, `animprogress`, `state`, `ScriptName`, `StringId`, `VerifiedBuild`) VALUES (301206,211640,0,85,5511,'0',0,0,0,-1,2918.08,-800.555,160.332,3.53649,0,0,-0.98057,0.19617,120,255,1,'',NULL,22996),(301207,211640,0,85,5511,'0',0,0,0,-1,2866.04,-822.257,160.332,0.406721,0,0,0.201962,0.979393,120,255,1,'',NULL,22996),(400644,211498,0,130,236,'0',0,0,0,-1,-231.566,1570.55,76.9718,4.3565,0,0,-0.821104,0.570779,120,255,1,'',NULL,60822),(400645,211498,33,209,209,'1,2,19',0,0,0,-1,-231.071,2104.98,76.8921,1.15795,0,0,0.547166,0.837024,7200,255,1,'',NULL,60822),(400646,214516,0,40,20,'0',0,0,0,-1,-11207.9,1679.66,24.0505,4.61857,0,0,-0.739488,0.67317,120,255,1,'',NULL,60822),(400647,211498,36,1581,1581,'1,2,24',0,0,0,-1,-14.8142,-388.649,63.7429,1.59414,0,0,0.715313,0.698804,7200,255,1,'',NULL,60822),(400648,214516,36,1581,1581,'1,2,24',0,0,0,-1,-109.615,-1003.85,34.3488,1.29086,0,0,0.601541,0.798842,7200,255,1,'',NULL,60822),(400649,212899,0,28,2297,'0',0,0,0,-1,1278.41,-2550.98,87.8072,3.61914,0,0,-0.971629,0.23651,120,255,1,'',NULL,60822),(400651,211498,229,1583,1583,'1',0,0,0,-1,63.2303,-325.034,53.9191,0,0,0,0,1,7200,255,1,'',NULL,60822),(400711,214516,0,33,1740,'0',0,0,0,-1,-11916.2,-1216.49,92.2873,1.56024,0,0,0.703365,0.710829,120,255,1,'',NULL,61122);
UPDATE `gameobject` SET `spawnDifficulties`='1' WHERE `guid` IN (301206,301207,400644,400646,400649,400711,400645,400647,400648,400651) AND `map` NOT IN (0,1);
-- Classic 1.60 has no GameObjectDisplayInfo 11469 (retail difficulty portal): use Classic swirl display 672
-- (world/generic/activedoodads/instanceportal/instanceportal.m2) as a plain generic object
DELETE FROM `gameobject_template` WHERE `entry` IN (1100000,1100001);
DROP TEMPORARY TABLE IF EXISTS `tmp_classic_portal`;
CREATE TEMPORARY TABLE `tmp_classic_portal` AS SELECT * FROM `gameobject_template` WHERE `entry` IN (214516,211498);
UPDATE `tmp_classic_portal` SET `entry`=IF(`entry`=214516,1100000,1100001), `type`=5, `displayId`=672, `name`='Instance Portal',
    `Data0`=0, `Data1`=0, `Data2`=0, `Data7`=0, `Data8`=0, `VerifiedBuild`=0;
INSERT INTO `gameobject_template` SELECT * FROM `tmp_classic_portal`;
DROP TEMPORARY TABLE `tmp_classic_portal`;
DELETE FROM `gameobject_template_addon` WHERE `entry` IN (1100000,1100001);
INSERT INTO `gameobject_template_addon` (`entry`,`faction`,`flags`) VALUES (1100000,35,16),(1100001,35,16);
UPDATE `gameobject` SET `id`=1100000 WHERE `guid` IN (301206,301207,400644,400646,400649,400711,400645,400647,400648,400651) AND `id`=214516;
UPDATE `gameobject` SET `id`=1100001 WHERE `guid` IN (301206,301207,400644,400646,400649,400711,400645,400647,400648,400651) AND `id` IN (211498,211640,212899);
