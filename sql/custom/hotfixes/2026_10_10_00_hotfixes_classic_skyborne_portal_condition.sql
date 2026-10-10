-- Classic 1.60: PlayerCondition 152835 of the Skyborne portals (Skyborne Portal to Stormwind 631299 in Dalaran, Skyborne Portal to
-- Dalaran 654168 in Stormwind, gameobject Data5). The client has the record in an encrypted section, the server data lacks it, so
-- everyone could use the portals. Official sniffs: the Stormwind portal was not usable for a Horde character (dynamic flag NO_INTERACT,
-- sniff 70170) and usable for an Alliance one (sniff 70291). Server only (no hotfix_data): CurrentPvpFaction 2 = Alliance.
DELETE FROM `player_condition` WHERE `ID` = 152835;
INSERT INTO `player_condition` (`ID`,`FailureDescription`,`CurrentPvpFaction`,`VerifiedBuild`) VALUES
(152835,'',2,70291);
