-- Quest 99144 Seeking Refuge: quest POI from the official 1.60.1.70291 beta (sniff 2026-10-09: three blobs, the old data had two)
DELETE FROM `quest_poi` WHERE `QuestID`=99144;
INSERT INTO `quest_poi` (`QuestID`,`BlobIndex`,`Idx1`,`ObjectiveIndex`,`QuestObjectiveID`,`QuestObjectID`,`MapID`,`UiMapID`,`Priority`,`Flags`,`WorldEffectID`,`PlayerConditionID`,`NavigationPlayerConditionID`,`SpawnTrackingID`,`AlwaysAllowMergingBlobs`,`VerifiedBuild`) VALUES
(99144,0,0,-1,0,0,0,1420,0,0,0,0,0,2851536,0,70291),
(99144,0,1,30,0,0,0,1420,0,2,0,0,0,0,0,70291),
(99144,0,2,32,0,0,0,1420,0,0,0,0,0,3356286,0,70291);

DELETE FROM `quest_poi_points` WHERE `QuestID`=99144;
INSERT INTO `quest_poi_points` (`QuestID`,`Idx1`,`Idx2`,`X`,`Y`,`Z`,`VerifiedBuild`) VALUES
(99144,0,0,2251,312,35,70291),
(99144,1,0,2369,1271,0,70291),
(99144,2,0,2439,1588,73,70291);
