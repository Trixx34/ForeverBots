-- Classic 1.60: Ragefire Chasm (389) and The Stockade (34) used the TC Cataclysm and Pandaria instance scripts, whose DungeonEncounter ids
-- (1443-1446, 1144-1146) the Classic client does not have. The new classic scripts keep TC boss states for the vanilla
-- bosses and send the Classic encounters (Ragefire Chasm 2732-2735, The Stockade 2756-2760).
UPDATE `instance_template` SET `script`='classic_instance_ragefire_chasm' WHERE `map`=389;
UPDATE `instance_template` SET `script`='classic_instance_the_stockade' WHERE `map`=34;
