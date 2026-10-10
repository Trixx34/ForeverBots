-- Quest 90902 "Rediscovering the Light": an Injured Deathguard (259377) is healed once, stands up and leaves.
-- Before: every Holy Light (635) hit on the same Deathguard gave credit, so one NPC could fill the whole
-- objective (5), and a healed Deathguard stayed kneeling at 50% health.
-- After: the Deathguard is in event phase 1 while injured (set on AI init, i.e. every spawn); the 50% health
-- reset, the 60 s 50% health timer and the Holy Light trigger only run in phase 1. One Holy Light hit runs one
-- linked chain: credit -> line -> full health -> stand up (creature_template_addon StandState 8) -> drop the
-- injured aura 436207 -> despawn after 4 s (own respawn time) -> phase 2, where none of the above can fire again.
DELETE FROM `smart_scripts` WHERE `entryorguid` = 259377 AND `source_type` = 0;
INSERT INTO `smart_scripts` (`entryorguid`, `source_type`, `id`, `link`, `event_type`, `event_phase_mask`, `event_chance`, `event_flags`,
  `event_param1`, `event_param2`, `event_param3`, `event_param4`, `event_param5`,
  `action_type`, `action_param1`, `action_param2`, `action_param3`, `action_param4`, `action_param5`, `action_param6`,
  `target_type`, `target_param1`, `target_param2`, `target_param3`, `target_param4`, `target_x`, `target_y`, `target_z`, `target_o`, `comment`) VALUES
(259377, 0, 0, 0, 37, 0, 100, 0, 0, 0, 0, 0, 0, 102, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 'Injured Deathguard - On AI init - No health regeneration'),
(259377, 0, 1, 0, 37, 0, 100, 0, 0, 0, 0, 0, 0, 22, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 'Injured Deathguard - On AI init - Set event phase 1 (injured)'),
(259377, 0, 2, 0, 25, 1, 100, 0, 0, 0, 0, 0, 0, 142, 50, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 'Injured Deathguard - On reset (phase 1) - Set health 50%'),
(259377, 0, 3, 0, 1, 1, 100, 0, 60000, 60000, 60000, 60000, 0, 142, 50, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 'Injured Deathguard - Out of combat every minute (phase 1) - Set health 50% again'),
(259377, 0, 4, 5, 8, 1, 100, 0, 635, 0, 0, 0, 0, 33, 259377, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 'Injured Deathguard - On Holy Light hit (phase 1) - Quest credit to the healer'),
(259377, 0, 5, 6, 61, 0, 100, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 'Injured Deathguard - Linked - Say a line'),
(259377, 0, 6, 7, 61, 0, 100, 0, 0, 0, 0, 0, 0, 142, 100, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 'Injured Deathguard - Linked - Set health 100%'),
(259377, 0, 7, 8, 61, 0, 100, 0, 0, 0, 0, 0, 0, 91, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 'Injured Deathguard - Linked - Stand up'),
(259377, 0, 8, 9, 61, 0, 100, 0, 0, 0, 0, 0, 0, 28, 436207, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 'Injured Deathguard - Linked - Remove the injured aura'),
(259377, 0, 9, 10, 61, 0, 100, 0, 0, 0, 0, 0, 0, 41, 4000, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 'Injured Deathguard - Linked - Despawn in 4 s'),
(259377, 0, 10, 0, 61, 0, 100, 0, 0, 0, 0, 0, 0, 22, 2, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 'Injured Deathguard - Linked - Set event phase 2 (healed)');
