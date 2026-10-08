-- Classic 1.60: Undead Paladin (race 5, class 2) is a valid character in the Classic client (CharBaseInfo) but not in retail.
-- Start at Deathknell like the other Undead classes, action bar and items like the Human Paladin.
DELETE FROM `playercreateinfo` WHERE `race` = 5 AND `class` = 2;
INSERT INTO `playercreateinfo` (`race`, `class`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `npe_map`, `npe_position_x`,
    `npe_position_y`, `npe_position_z`, `npe_orientation`, `npe_transport_guid`, `intro_movie_id`, `intro_scene_id`, `npe_intro_scene_id`)
SELECT 5, 2, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `npe_map`, `npe_position_x`, `npe_position_y`, `npe_position_z`,
    `npe_orientation`, `npe_transport_guid`, `intro_movie_id`, `intro_scene_id`, `npe_intro_scene_id`
FROM `playercreateinfo` WHERE `race` = 5 AND `class` = 1;

DELETE FROM `playercreateinfo_action` WHERE `race` = 5 AND `class` = 2;
INSERT INTO `playercreateinfo_action` (`race`, `class`, `button`, `action`, `type`)
SELECT 5, 2, `button`, `action`, `type` FROM `playercreateinfo_action` WHERE `race` = 1 AND `class` = 2;

DELETE FROM `playercreateinfo_item` WHERE `race` = 5 AND `class` = 2;
INSERT INTO `playercreateinfo_item` (`race`, `class`, `itemid`, `amount`)
SELECT 5, 2, `itemid`, `amount` FROM `playercreateinfo_item` WHERE `race` = 1 AND `class` = 2;
