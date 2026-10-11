-- Classic 1.60: Legacy reward track (RenownRewards group 48): rewards at 15 / 25 / 40 / 55 Legacy Points, handed out as quests by
-- Innkeeper Wiley in Ratchet (world 2026_10_10_05). Legacy Points = quantity of the Legacy renown currency 3485
-- (Player::UpdateClassicLegacyUnlock). Server only (no hotfix_data): our own PlayerCondition IDs, used by the quest conditions.
DELETE FROM `player_condition` WHERE `ID` BETWEEN 9900001 AND 9900004;
-- Unused fields are -1 like in the client data: the server reads 0 there as a requirement (Gender 0 = male only,
-- ChrSpecializationIndex 0, MaxExpansionLevel 0 = no account above expansion 0).
INSERT INTO `player_condition` (`ID`,`FailureDescription`,`CurrencyID1`,`CurrencyCount1`,`Gender`,`NativeGender`,`MinExpansionLevel`,`MaxExpansionLevel`,`MinExpansionTier`,`MaxExpansionTier`,`ChrSpecializationIndex`,`ChrSpecializationRole`,`PowerType`,`VerifiedBuild`) VALUES
(9900001,'',3485,15,-1,-1,-1,-1,-1,-1,-1,-1,-1,0),
(9900002,'',3485,25,-1,-1,-1,-1,-1,-1,-1,-1,-1,0),
(9900003,'',3485,40,-1,-1,-1,-1,-1,-1,-1,-1,-1,0),
(9900004,'',3485,55,-1,-1,-1,-1,-1,-1,-1,-1,-1,0);
