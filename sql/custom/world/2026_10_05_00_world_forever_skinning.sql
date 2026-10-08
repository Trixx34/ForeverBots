-- Re-run after the sniff files (2026_10_01_03/04) were regenerated: they rewrite these creature_template_difficulty rows (last: 2026-10-05, sniff of 2026-10-04 20:05).
-- Classic 1.60 (WoW Forever): skinning for the Zephras Isle / Gustberry beasts (none had skin loot: "skinning is not working").
-- All are level 1-11: VMaNGOS 1.12 table 118 like every vanilla beast of that range (60% Ruined Leather Scraps, 40% Light Leather);
-- official beta 70205 gave those for Ornery Galestrider (8-9) and Vuldren Alpha (10). Insects, mounts, pets and story
-- creatures stay unskinnable. Safe to run again.
UPDATE `creature_template_difficulty` SET `SkinLootID` = CASE `Entry`
    WHEN 250868 THEN 118   -- Vuldren (6-6)
    WHEN 250873 THEN 118   -- Juvenile Vuldren (1-1)
    WHEN 250874 THEN 118   -- Vuldren Alpha (10-10)
    WHEN 250926 THEN 118   -- Scrawny Ursera (3-4)
    WHEN 250928 THEN 118   -- Highlands Ursera (7-8)
    WHEN 250937 THEN 118   -- Ursera Scavenger (4-4)
    WHEN 251115 THEN 118   -- Ursanah (5-5)
    WHEN 251245 THEN 118   -- Prideclaw (5-6)
    WHEN 251261 THEN 118   -- Hippogryph Matriarch (7-7)
    WHEN 251284 THEN 118   -- Hippogryph Protector (6-7)
    WHEN 251291 THEN 118   -- Hippogryph Youth (6-6)
    WHEN 251661 THEN 118   -- Galestrider (5-6)
    WHEN 251707 THEN 118   -- Ornery Galestrider (8-9)
    WHEN 253282 THEN 118   -- Shriekling Fledgling (8-9)
    WHEN 253283 THEN 118   -- Shriekling Matriarch (9-9)
    WHEN 254588 THEN 118   -- Windsong Crawler (8-9)
    WHEN 254589 THEN 118   -- Vulgara the Insatiable (8-8)
    WHEN 256092 THEN 118   -- Shadowgale Shriekling (10-11)
    WHEN 256108 THEN 118   -- Shadowgale Shrieker (10-10)
    WHEN 258443 THEN 118   -- Urendra (10-10)
    WHEN 271712 THEN 118   -- Cloudrunner (6-6)
    ELSE `SkinLootID` END
WHERE `DifficultyID` = 0 AND `Entry` IN (250868,250873,250874,250926,250928,250937,251115,251245,251261,251284,251291,251661,251707,253282,253283,254588,254589,256092,256108,258443,271712);
