-- Classic 1.60 (client 70291): achievement 64283 "Played in the WoW: Forever Beta" and its 16 Legacy Points (TraitCurrencySource 38875,
-- "Play in the WoW: Forever Beta"). The official beta gives the achievement at login (sniff 70291); the server Achievement.db2 and
-- TraitCurrencySource.db2 (older client data) lack both records.
-- Achievement: server only (no hotfix_data), the 70291 client has its own record. Values from the 70291 Achievement.db2, except the
-- criteria tree (236079, not in the server data): the server grants it after the loading screen (CharacterHandler.cpp).
DELETE FROM `achievement` WHERE `ID` = 64283;
INSERT INTO `achievement` (`Description`,`Title`,`Reward`,`ID`,`InstanceID`,`Faction`,`Supercedes`,`Category`,`MinimumCriteria`,`Points`,`Flags`,`UiOrder`,`IconFileID`,`RewardItemID`,`CriteriaTree`,`SharesCriteria`,`CovenantID`,`HiddenBeforeDisplaySeason`,`LegacyAfterTimeEvent`,`VerifiedBuild`) VALUES
('','Played in the WoW: Forever Beta','',64283,-1,-1,0,15596,0,0,132096,2,134140,0,0,0,0,0,0,70291);

-- Legacy Point source: SuperDistrictSetID 60 sent as 0 like the other Legacy sources (2026_09_30_00), so the client counts it
DELETE FROM `trait_currency_source` WHERE `ID` = 38875;
INSERT INTO `trait_currency_source` (`Requirement`,`ID`,`TraitCurrencyID`,`Amount`,`QuestID`,`AchievementID`,`PlayerLevel`,`TraitNodeEntryID`,`SuperDistrictSetID`,`OrderIndex`,`VerifiedBuild`) VALUES
('Play in the WoW: Forever Beta',38875,4225,16,0,64283,0,0,0,0,0);
DELETE FROM `hotfix_data` WHERE `Id` = 982130;
INSERT INTO `hotfix_data` (`Id`,`UniqueId`,`TableHash`,`RecordId`,`Status`,`VerifiedBuild`) VALUES
(982130,1280376962,2493836603,38875,1,0);
