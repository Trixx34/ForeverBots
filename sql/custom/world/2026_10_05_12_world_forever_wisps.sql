-- Classic 1.60 (WoW Forever): Zephras Isle Wisps (273458) fly. Official beta sniffs: DisableGravity movement flag on every
-- sighting, about 11 yards above the ground (median), drifting on long straight splines at a constant height. Without the
-- Floating static flag they fell to the ground on spawn. Floating keeps their spawn height; random movement for a flyer
-- keeps the height too. Safe to run again.
UPDATE `creature_template_difficulty` SET `StaticFlags1` = `StaticFlags1` | 0x20000000 WHERE `Entry` = 273458;
UPDATE `creature` SET `MovementType` = 1, `wander_distance` = 25 WHERE `id` = 273458 AND `MovementType` <> 2;
