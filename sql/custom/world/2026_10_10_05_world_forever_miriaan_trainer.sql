-- Miriaan Mistblade (254087) has a rogue trainer gossip option, but no creature_trainer mapping.
-- Link the option to the existing rogue trainer so the trainer gossip handler can open the spell list.
INSERT INTO `creature_trainer` (`CreatureID`, `TrainerID`, `MenuID`, `OptionID`) VALUES (254087, 26, 41124, 0)
ON DUPLICATE KEY UPDATE `TrainerID` = 26;

-- The gossip option can open the list without this flag, but the spell-buy handler requires NPC_FLAG_TRAINER.
UPDATE `creature_template` SET `npcflag` = `npcflag` | 16 WHERE `entry` = 254087;
