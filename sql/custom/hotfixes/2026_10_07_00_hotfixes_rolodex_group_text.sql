-- Classic 1.60: the Social window Allies tab shows RolodexType.Description as the reason someone is a recent ally. The client data
-- (RolodexType.db2, table hash 0xDA556D7C, layout 0x22D8CB1A: Name, Description strings) has no description for 1 Party Member and
-- 2 Raid Member (Blizzard does not record them in Classic), so grouped allies showed with an empty reason. Give them one.
-- Record blob = Name, NUL, Description, NUL. Own hotfix_data ids outside the sniff import range (990000-1989999). Safe to run again.
DELETE FROM `hotfix_blob` WHERE `TableHash` = 3663031676 AND `RecordId` IN (1, 2);
INSERT INTO `hotfix_blob` (`TableHash`,`RecordId`,`locale`,`Blob`,`VerifiedBuild`) VALUES
(3663031676,1,'enUS',CONCAT('Party Member',CHAR(0),'Grouped Together',CHAR(0)),70235),
(3663031676,2,'enUS',CONCAT('Raid Member',CHAR(0),'In a Raid Together',CHAR(0)),70235);
DELETE FROM `hotfix_data` WHERE `Id` IN (2000002, 2000003);
INSERT INTO `hotfix_data` (`Id`,`UniqueId`,`TableHash`,`RecordId`,`Status`,`VerifiedBuild`) VALUES
(2000002,1397720002,3663031676,1,1,70235),
(2000003,1397720003,3663031676,2,1,70235);
