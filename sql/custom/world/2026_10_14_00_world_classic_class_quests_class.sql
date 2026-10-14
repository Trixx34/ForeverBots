-- Class quests (filed under a class in the quest log) that had no class restriction in quest_template_addon, so every class
-- was offered them. AllowableClasses is a class mask; each block sets the mask of the class the quest belongs to.
-- Rows that are missing are created; existing rows only change that one column. Only quests with a classic level are listed
-- (the others cannot be reached in this game). Masks: Warrior 1, Paladin 2, Hunter 4, Rogue 8, Priest 16, Shaman 64,
-- Mage 128, Warlock 256, Druid 1024.

-- Warrior (1)
INSERT INTO `quest_template_addon` (`ID`, `AllowableClasses`) VALUES
(92479, 1),  -- level 1: A Scribbled Letter
(92532, 1),  -- level 2: The Warrior's Path
(94003, 1)  -- level 10: The Skybreaker Bulwark
ON DUPLICATE KEY UPDATE `AllowableClasses` = 1;

-- Paladin (2)
INSERT INTO `quest_template_addon` (`ID`, `AllowableClasses`) VALUES
(1659, 2),  -- level 30: <UNUSED>
(1660, 2),  -- level 30: <UNUSED>
(91285, 2),  -- level 11: Murlocs at the Gates
(91294, 2),  -- level 11: Touring the Grounds
(91316, 2),  -- level 11: Making Repairs
(91317, 2),  -- level 12: The Tarnished
(94427, 2),  -- level 12: A Lesson in Divinity
(94434, 2),  -- level 12: A Lesson in Divinity
(94435, 2),  -- level 12: A Lesson in Divinity
(94436, 2),  -- level 12: A Lesson in Divinity
(94438, 2),  -- level 12: A Lesson in Divinity
(94440, 2),  -- level 12: A Lesson in Divinity
(94441, 2)  -- level 12: A Lesson in Divinity
ON DUPLICATE KEY UPDATE `AllowableClasses` = 2;

-- Hunter (4)
INSERT INTO `quest_template_addon` (`ID`, `AllowableClasses`) VALUES
(92482, 4),  -- level 2: The Way of the Hunter
(94013, 4),  -- level 10: Taming the Beast
(94050, 4),  -- level 10: Training the Beast
(94792, 4),  -- level 10: Taming the Beast
(94793, 4),  -- level 10: Training the Beast
(94863, 4),  -- level 10: Taming the Beast
(94864, 4),  -- level 10: Taming the Beast
(94978, 4),  -- level 10: Taming the Beast
(94979, 4)  -- level 10: Taming the Beast
ON DUPLICATE KEY UPDATE `AllowableClasses` = 4;

-- Rogue (8)
INSERT INTO `quest_template_addon` (`ID`, `AllowableClasses`) VALUES
(8249, 8),  -- level 60: Junkboxes Needed
(92483, 8)  -- level 2: At Home in the Shadows
ON DUPLICATE KEY UPDATE `AllowableClasses` = 8;

-- Priest (16)
INSERT INTO `quest_template_addon` (`ID`, `AllowableClasses`) VALUES
(94774, 16),  -- level 10: Divine Grace
(94817, 16),  -- level 10: Confounding Flash
(94818, 16),  -- level 20: Contingency Plan
(98574, 16)  -- level 1: Hallowed Memorandum
ON DUPLICATE KEY UPDATE `AllowableClasses` = 16;

-- Shaman (64)
INSERT INTO `quest_template_addon` (`ID`, `AllowableClasses`) VALUES
(987, 64),  -- level 30: <UNUSED>
(8259, 64),  -- level 60: A More Fitting Reward
(79358, 64),  -- level 40: Storm, Earth, and Fire
(79361, 64),  -- level 40: Storm, Earth, and Fire
(92484, 64),  -- level 2: Embracing the Elements
(94449, 64),  -- level 10: Call of Fire
(94465, 64),  -- level 11: Call of Fire
(94466, 64),  -- level 14: Call of Fire
(94467, 64),  -- level 14: Call of Fire
(94468, 64),  -- level 14: Call of Fire
(94494, 64),  -- level 20: Call of Water
(94495, 64),  -- level 22: Call of Water
(94497, 64),  -- level 22: Call of Water
(94499, 64),  -- level 22: Call of Water
(94500, 64),  -- level 23: Call of Water
(94501, 64),  -- level 23: Call of Water
(94502, 64),  -- level 23: Call of Water
(94503, 64),  -- level 23: Call of Water
(94505, 64),  -- level 23: Call of Water
(97243, 64),  -- level 10: Call of Fire
(97244, 64),  -- level 10: Call of Fire
(97245, 64),  -- level 10: Call of Fire
(97257, 64)  -- level 10: Call of Fire
ON DUPLICATE KEY UPDATE `AllowableClasses` = 64;

-- Mage (128)
INSERT INTO `quest_template_addon` (`ID`, `AllowableClasses`) VALUES
(3111, 128)  -- level 1: Glyphic Rune
ON DUPLICATE KEY UPDATE `AllowableClasses` = 128;

-- Druid (1024)
INSERT INTO `quest_template_addon` (`ID`, `AllowableClasses`) VALUES
(92485, 1024),  -- level 2: A Student of Nature
(94006, 1024),  -- level 10: The Great Ursera Spirit
(94638, 1024),  -- level 10: Strength and Mercy
(94911, 1024),  -- level 10: Child of Nature
(94913, 1024),  -- level 10: Moonglade
(98340, 1024),  -- level 20: The Great Cat Spirit
(98341, 1024),  -- level 20: The Great Windborne Cat Spirit
(98342, 1024),  -- level 20: The Great Cat Spirit
(98362, 1024),  -- level 20: To Thunder Bluff
(98404, 1024),  -- level 20: The Great Windborne Cat Spirit
(98405, 1024),  -- level 20: The Great Cat Spirit
(98738, 1024),  -- level 20: Blessings of the Great Windborne Cat Spirit
(98739, 1024)  -- level 20: Blessings of the Great Cat Spirit
ON DUPLICATE KEY UPDATE `AllowableClasses` = 1024;
