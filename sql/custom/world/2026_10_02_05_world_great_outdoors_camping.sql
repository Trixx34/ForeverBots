-- Re-run after the sniff files (2026_10_01_03/04, 2026_10_02_00/06, 2026_10_03_00) were regenerated: they rewrite rows this file adjusts (last: 2026-10-05, sniff of 2026-10-04 20:05).
-- Classic 1.60 (WoW Forever) "The Great Outdoors" and "Camping 101":
-- sitting near a campfire for a minute gives Boosted Rest (1229451) through the rest aura 1289723 (classic_spell_campfire_rest;
-- the sit itself is handled in WorldSession::HandleStandStateChangeOpcode)
-- the Camp Tents buff Boosted Rest (1229451) raises rested experience to 5% of a level (classic_spell_boosted_rest)
DELETE FROM `spell_script_names` WHERE `spell_id` IN (1289723,1229451);
INSERT INTO `spell_script_names` (`spell_id`,`ScriptName`) VALUES
(1289723,'classic_spell_campfire_rest'),
(1229451,'classic_spell_boosted_rest');

-- the profession follow-ups of the camping trainers (Zephras Isle 263664, Tirisfal Glades Eleanor Shackleton 265812) are only
-- offered to players who know that profession, after "The Great Outdoors" (96101 / 96607); "Camping 101: Cooking" teaches cooking,
-- so it stays open to everyone. Sniffs: right after The Great Outdoors the trainer offers Cooking plus the professions the
-- character has (Skinning; Herbalism), more appear as professions are learned.
INSERT INTO `quest_template_addon` (`ID`,`PrevQuestID`,`RequiredSkillID`,`RequiredSkillPoints`) VALUES
(97923,96101,186,1),    -- Camping 101: Mining
(97965,96101,129,1),    -- Camping 101: First Aid
(97967,96101,356,1),    -- Camping 101: Fishing
(97969,96101,165,1),    -- Camping 101: Leatherworking
(97971,96101,393,1),    -- Camping 101: Skinning
(97963,96101,171,1),    -- Camping 101: Alchemy
(97964,96101,164,1),    -- Camping 101: Blacksmithing
(98284,96101,333,1),    -- Camping 101: Enchanting
(97968,96101,182,1),    -- Camping 101: Herbalism
(97970,96101,186,1),    -- Camping 101: Mining
(97972,96101,197,1),    -- Camping 101: Tailoring
(97957,96607,182,1)     -- Camping 101: Herbalism (Tirisfal Glades)
ON DUPLICATE KEY UPDATE `PrevQuestID` = VALUES(`PrevQuestID`), `RequiredSkillID` = VALUES(`RequiredSkillID`),
`RequiredSkillPoints` = VALUES(`RequiredSkillPoints`);
