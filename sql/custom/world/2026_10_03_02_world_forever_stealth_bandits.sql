-- Re-run after the sniff files (2026_10_01_03/04, 2026_10_02_00/06, 2026_10_03_00) were regenerated: they rewrite rows this file adjusts (last: 2026-10-05, sniff of 2026-10-04 20:05).
-- Classic 1.60 (WoW Forever): Hungry Bandits (252802) are stealthed (official beta sniff 70170: they cast Sneak 22766 = stealth +
-- slower movement, and carry Sneak 7939, a dummy aura of the client). As addon auras they are applied at spawn and again after
-- evading. Safe to run again.
INSERT INTO `creature_template_addon` (`entry`, `auras`) VALUES (252802, '22766 7939')
ON DUPLICATE KEY UPDATE `auras` = '22766 7939';
