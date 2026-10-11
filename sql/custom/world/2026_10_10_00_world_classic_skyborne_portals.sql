-- Classic 1.60: destinations of the Skyborne portals (gameobjects 631299 in Dalaran and 654168 in Stormwind cast 1284041 / 1278903,
-- which trigger the teleports 1284040 / 1278902, TARGET_DEST_DB). Without them the portals did nothing.
-- Stormwind: official sniff 70291 (NEW_WORLD after 1284040), beside the Skyborne Portal to Dalaran.
-- Dalaran: not sniffed yet; placed beside the Skyborne Portal to Stormwind the same way (4.7 yards, same angle to the portal).
-- Who may use them: PlayerCondition 152835 (hotfixes 2026_10_10_00).
-- Same for the two Dalaran city portals (not sniffed yet): Portal to The Violet Citadel 691558 (1318287) stands in the Purple Parlor
-- and Portal to The Purple Parlor 565745 (1318270) at the Violet Citadel, so each one leads 3 yards in front of the other.
-- Safe to run again.
DELETE FROM `spell_target_position` WHERE `ID` IN (1284040,1278902,1318287,1318270);
INSERT INTO `spell_target_position` (`ID`,`EffectIndex`,`OrderIndex`,`MapID`,`PositionX`,`PositionY`,`PositionZ`,`Orientation`,`VerifiedBuild`) VALUES
(1284040,0,0,0,-8999.8,856.45,29.62,2.11569,70291),
(1278902,0,0,0,455.13,444.87,110.75,1.10,0),
(1318287,0,0,0,427.19,376.23,129.08,2.4564,0),
(1318270,0,0,0,470.36,364.13,292.46,0,0);
