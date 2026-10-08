-- Classic 1.60 (WoW Forever): .tele locations for the new Forever instance maps (points from the NightVibes GM Panel export)
DELETE FROM `game_tele` WHERE `id` BETWEEN 2315 AND 2318;
DELETE FROM `game_tele` WHERE `name` IN ('HallOfThanes', 'RuinsOfLordaeron', 'ExcavationSiteWetlands', 'CityOfDalaran');
INSERT INTO `game_tele` (`id`, `position_x`, `position_y`, `position_z`, `orientation`, `map`, `name`) VALUES
(2315, 201.4, -220.2, 489.183, 2.0221, 3065, 'HallOfThanes'),
(2316, 1478.19, 33.813, -61.609, 1.3615, 2999, 'RuinsOfLordaeron'),
(2317, -3734.93, -2124.53, 145.666, 2.5958, 2998, 'ExcavationSiteWetlands'),
(2318, 481.933, 493.733, 81.272, 3.7067, 2959, 'CityOfDalaran');
-- the other new maps: a dry ground point near the middle of the terrain (classic_re/maptele.py, heights from the server map tiles)
DELETE FROM `game_tele` WHERE `id` BETWEEN 2319 AND 2339;
INSERT INTO `game_tele` (`id`, `position_x`, `position_y`, `position_z`, `orientation`, `map`, `name`) VALUES
(2319, -7966.67, -3466.67, 284.47, 0, 2720, 'SearingBasin'),
(2320, 1866.67, -2933.33, 65.54, 0, 2784, 'DemonFallCanyonInside'),
(2321, -12000.0, -2400.0, 4.35, 0, 2789, 'TaintedScar'),
(2322, 2700.0, -6133.33, 110.51, 0, 2791, 'StormCliffs'),
(2323, -6666.67, 1333.33, 7.16, 0, 2804, 'CrystalVale'),
(2324, 5066.67, -266.67, 295.57, 0, 2806, 'ShadowHoldInside'),
(2325, 1866.67, -1866.67, 89.97, 0, 2807, 'BurningOfAndorhal'),
(2326, 7200.0, -4000.0, 753.74, 0, 2817, 'StarfallBarrowDen'),
(2327, -2266.67, 1100.0, 204.77, 0, 2832, 'NightmareGrove'),
(2328, -10633.33, -1866.67, 190.7, 0, 2853, 'DeadwindPassForever'),
(2329, 2166.67, -5066.67, 51.77, 0, 2856, 'ScarletEnclave'),
(2330, 1966.67, -5500.0, 223.29, 0, 2868, 'ScarletRaidPhase'),
(2331, -10633.33, -1866.67, 190.7, 0, 2875, 'KarazhanCryptsInside'),
(2332, -8800.0, 1333.33, 133.96, 0, 2902, 'ScarabDais'),
(2333, 3233.33, -4233.33, 388.94, 0, 2921, 'NaxxramasForever'),
(2334, 33.33, 33.33, 61.4, 0, 2980, 'DalaranCityForever'),
(2335, 5066.67, -2400.0, 1446.1, 0, 2995, 'HyjalCrater'),
(2336, 1333.33, 1333.33, 326.35, 0, 2996, 'WarsongGulchWinter'),
(2337, -300.0, 1900.0, 2.28, 0, 2997, 'DarkspearIslands'),
(2338, 566.67, 1100.0, 31.8, 0, 3005, 'BattleForGilneas'),
(2339, -4766.67, 33.33, 506.68, 0, 3021, 'EasternKingdomsPreserved');
-- Zephras inns (points from the NightVibes GM Panel export)
DELETE FROM `game_tele` WHERE `id` IN (2340, 2341);
INSERT INTO `game_tele` (`id`, `position_x`, `position_y`, `position_z`, `orientation`, `map`, `name`) VALUES
(2340, 3354.25, 1839.38, 827.806, 0, 2991, 'ZephrasInn'),
(2341, 2264, 771.375, 678.006, 0, 2991, 'ZephrasSouthInn');
