-- Re-run after the sniff files (2026_10_01_03/04, 2026_10_02_00/06, 2026_10_03_00) were regenerated: they rewrite rows this file adjusts (last: 2026-10-05, sniff of 2026-10-04 20:05).
-- Classic 1.60: the Elemental Convergence (spell focus 616992) links trap 616510, which was missing: on the official beta a player
-- near a convergence casts Elemental Convergence (1271953) on themselves (ymir sniff), and "The Gift of Skysight" (92598) credits
-- Skysight cast with that aura (criteria 110206, modifier tree 426349: player has aura 1271953). Traps are never queried by the
-- client, so the template is built from the focus (same 25 yd reach): server-only, unlimited charges, the player casts it.
DELETE FROM `gameobject_template` WHERE `entry` = 616510;
INSERT INTO `gameobject_template` (`entry`,`type`,`displayId`,`name`,`IconName`,`castBarCaption`,`unk1`,`size`,`Data0`,`Data1`,`Data2`,`Data3`,
`Data4`,`Data5`,`Data6`,`Data7`,`Data8`,`Data9`,`Data10`,`Data11`,`Data12`,`Data13`,`Data14`,`Data15`,`Data16`,`Data17`,`Data18`,`Data19`,
`Data20`,`Data21`,`Data22`,`Data23`,`Data24`,`Data25`,`Data26`,`Data27`,`Data28`,`Data29`,`Data30`,`Data31`,`Data32`,`Data33`,`Data34`,
`ContentTuningId`,`RequiredLevel`,`AIName`,`ScriptName`,`VerifiedBuild`) VALUES
(616510,6,0,'Elemental Convergence Trap','','','',1,
 0,0,25,1271953,0,5,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,'','',0);
