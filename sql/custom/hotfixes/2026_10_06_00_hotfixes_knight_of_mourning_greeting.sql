-- Classic 1.60 (WoW Forever): Knight of Mourning greeting (gossip menu 45038, npc_text 10045038) as the official beta sends it,
-- from the build 70235 sniff of 2026-10-06. Own hotfix_data id outside the sniff import range (990000-1989999). Safe to run again.
DELETE FROM `broadcast_text` WHERE `ID` = 326872;
INSERT INTO `broadcast_text` (`Text`,`Text1`,`ID`,`LanguageID`,`ConditionID`,`EmotesID`,`Flags`,`ChatBubbleDurationMs`,`VoiceOverPriorityID`,`SoundKitID1`,`SoundKitID2`,`EmoteID1`,`EmoteID2`,`EmoteID3`,`EmoteDelay1`,`EmoteDelay2`,`EmoteDelay3`,`VerifiedBuild`) VALUES
('My oath binds me to this place. What brings you here, to this light-forsaken place?','',326872,0,0,0,256,0,4,0,0,0,0,0,0,0,0,70235);
DELETE FROM `hotfix_data` WHERE `Id` = 2000001;
INSERT INTO `hotfix_data` (`Id`,`UniqueId`,`TableHash`,`RecordId`,`Status`,`VerifiedBuild`) VALUES
(2000001,1397720001,35137211,326872,1,70235);

