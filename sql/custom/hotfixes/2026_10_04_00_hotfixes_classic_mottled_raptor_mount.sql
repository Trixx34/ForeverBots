-- Classic 1.60: Whistle of the Mottled Raptor (item 216492) teaches spell 436288 (mount aura, creature 7704 = display 6469),
-- but Mount.db2 has no row for it, so it never showed in the mount journal. Mount 3900 + display 9900 sent as hotfixes.
-- (Reins of the Swift Spectral Tiger, spell 1214283, has no model in the 70205 client: not added.) Safe to run again.
DELETE FROM `mount` WHERE `ID` = 3900;
INSERT INTO `mount` (`Name`,`SourceText`,`Description`,`ID`,`MountTypeID`,`Flags`,`SourceTypeEnum`,`SourceSpellID`,`PlayerConditionID`,
`MountFlyRideHeight`,`UiModelSceneID`,`MountSpecialRiderAnimKitID`,`MountSpecialSpellVisualKitID`,`VerifiedBuild`) VALUES
('Mottled Raptor','','A swift raptor with a mottled hide.',3900,230,0,0,436288,0,0,4,0,0,70205);

DELETE FROM `mount_x_display` WHERE `ID` = 9900;
INSERT INTO `mount_x_display` (`ID`,`CreatureDisplayInfoID`,`PlayerConditionID`,`Unknown1100`,`MountID`,`VerifiedBuild`) VALUES
(9900,6469,0,0,3900,70205);

DELETE FROM `hotfix_data` WHERE (`TableHash` = 2524150337 AND `RecordId` = 3900) OR (`TableHash` = 2030087241 AND `RecordId` = 9900);
INSERT INTO `hotfix_data` (`Id`,`UniqueId`,`TableHash`,`RecordId`,`Status`,`VerifiedBuild`) VALUES
(984000,1281442368,2524150337,3900,1,70205),
(984001,1281442369,2030087241,9900,1,70205);
