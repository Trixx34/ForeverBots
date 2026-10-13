-- Classic 1.60: the character boost (CharacterServiceInfo 120, level 60) lets the player choose any specialization. The client row has
-- Flags 1 = RestrictToRecommendedSpecs, and Classic's one specialization per class is not "recommended", so the boost flow's spec
-- step allowed nothing ("This specialization isn't available for a Boosted character"). Same row with Flags 0.
-- Record: FlowTitle, PopupTitle, PopupDescription (strings), then 10 int32 (Flags is the 5th).

DELETE FROM `hotfix_blob` WHERE `TableHash` IN (1454018603, 1453101099) AND `RecordId` = 120;   -- 1453101099: earlier wrong hash
INSERT INTO `hotfix_blob` (`TableHash`,`RecordId`,`locale`,`Blob`,`VerifiedBuild`) VALUES
(1454018603,120,'enUS',0x4c6576656c20363020426f6f73740046726565204c6576656c20363020426f6f737400000100000006000000654c41003c00000000000000000000003c0000000800000009010000db150000,70338);

DELETE FROM `hotfix_data` WHERE `Id` IN (2000100, 2000101);   -- 2000100 went out with a wrong table hash; clients cached that push id
INSERT INTO `hotfix_data` (`Id`,`UniqueId`,`TableHash`,`RecordId`,`Status`,`VerifiedBuild`) VALUES
(2000101,2000101,1454018603,120,1,70338);
