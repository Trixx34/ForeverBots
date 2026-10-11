-- Classic 1.60: WorldEffectID of gameobjects (minimap marks, WorldEffect -> QuestFeedbackEffect), read from the gameobject
-- create block of every official sniff (each entry had the same value on every spawn). 34197: chests and equipment boxes,
-- 23878: quest objects (tomes, scrolls, construct parts), 31002: Bloodstained Satchel. Rows that exist keep their faction and flags;
-- new rows take the sniffed faction (flags left 0: the sniffed ones are quest / interaction state flags).
INSERT INTO `gameobject_template_addon` (`entry`,`faction`,`flags`,`WorldEffectID`) VALUES
(2843,94,0,34197),
(2849,94,0,34197),
(2855,94,0,34197),
(106318,94,0,34197),
(106319,94,0,34197),
(164662,94,0,34197),
(626718,94,0,34197),
(626752,94,0,34197),
(405879,0,0,23878),
(407566,0,0,23878),
(409496,0,0,23878),
(409692,0,0,23878),
(409700,0,0,23878),
(409711,0,0,23878),
(581820,0,0,23878),
(616466,0,0,23878),
(618329,0,0,23878),
(581822,0,0,31002)
ON DUPLICATE KEY UPDATE `WorldEffectID` = VALUES(`WorldEffectID`);
