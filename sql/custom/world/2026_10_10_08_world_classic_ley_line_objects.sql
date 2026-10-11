-- Classic 1.60: Ley Line (602735, 613248, spell focus 2246, Read Ley Line) and Elemental Convergence (616992, spell focus 2271,
-- Skysight). Every official sniff (Alliance and Horde, maps 0 / 1 / 2991) creates them with faction 35, GO flag 0x20 (NODESPAWN)
-- and a WorldEffectID in the gameobject create block: 32159 for the ley lines, 32160 for the convergence. Both world effects
-- use QuestFeedbackEffect 1437 (the minimap icon), shown by the client under PlayerCondition 151473 / 151475.
DELETE FROM `gameobject_template_addon` WHERE `entry` IN (602735,613248,616992);
INSERT INTO `gameobject_template_addon` (`entry`,`faction`,`flags`,`mingold`,`maxgold`,`artkit0`,`artkit1`,`artkit2`,`artkit3`,`artkit4`,`WorldEffectID`,`AIAnimKitID`) VALUES
(602735,35,32,0,0,0,0,0,0,0,32159,0),
(613248,35,32,0,0,0,0,0,0,0,32159,0),
(616992,35,32,0,0,0,0,0,0,0,32160,0);
