-- The hunter class quests below had no class restriction in quest_template_addon, so every class was offered them:
--   92482 The Way of the Hunter (level 2), 94013 / 94792 / 94863 / 94864 / 94978 / 94979 Taming the Beast (level 10),
--   94050 / 94793 Training the Beast (level 10).
-- AllowableClasses is a class mask: 4 = Hunter. Rows that are missing are created; existing rows only change that one column.

INSERT INTO `quest_template_addon` (`ID`, `AllowableClasses`) VALUES
(92482, 4),
(94013, 4),
(94050, 4),
(94792, 4),
(94793, 4),
(94863, 4),
(94864, 4),
(94978, 4),
(94979, 4)
ON DUPLICATE KEY UPDATE `AllowableClasses` = 4;
