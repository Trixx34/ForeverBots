-- Sniffed class trainers 252359 (druid), 252373/254086 (mage), 252377/254088 (warrior), 252379/254087 (rogue), 252382/254082 (shaman),
-- 252389/254084 (hunter) had no creature_trainer row, so "I'd like training!" opened nothing, and most lacked the trainer npcflag bits
-- (0x10 trainer, 0x20 class trainer), so even a linked list could not be bought from. Link them to the same class lists the Exile's Reach
-- trainers use (see 2026_10_08_02_world_forever_sim_trainers.sql), set the flags, and give mage menu 41122 its missing training option.
-- Needs a worldserver restart. Idempotent.
START TRANSACTION;

DELETE FROM `creature_trainer` WHERE `CreatureID` IN (252359, 252373, 252377, 252379, 252382, 252389, 254082, 254084, 254086, 254087, 254088);
INSERT INTO `creature_trainer` (`CreatureID`, `TrainerID`, `MenuID`, `OptionID`) VALUES
  (252359, 1233,    43828, 0),   -- druid
  (252373, 1000198, 41122, 0),   -- mage
  (254086, 1000198, 41122, 0),
  (252377, 1000911, 41126, 0),   -- warrior
  (254088, 1000911, 41126, 0),
  (252379, 1000915, 41124, 0),   -- rogue (Eltheen Nightbreeze)
  (254087, 1000915, 41124, 0),
  (252382, 1000986, 0,     0),   -- shaman (menu not in the sniff: default trainer fallback)
  (254082, 1000986, 41125, 0),
  (252389, 1005515, 41123, 0),   -- hunter
  (254084, 1005515, 41123, 0);

UPDATE `creature_template` SET `npcflag` = `npcflag` | 0x31
  WHERE `entry` IN (252359, 252373, 252377, 252379, 252382, 252389, 254082, 254084, 254086, 254087, 254088);

DELETE FROM `gossip_menu_option` WHERE `MenuID` = 41122 AND `OptionID` = 0;
INSERT INTO `gossip_menu_option` (`MenuID`, `GossipOptionID`, `OptionID`, `OptionNpc`, `OptionText`, `OptionBroadcastTextID`, `Language`, `Flags`, `ActionMenuID`, `ActionPoiID`, `BoxCoded`, `BoxMoney`, `BoxText`, `BoxBroadcastTextID`, `VerifiedBuild`)
  VALUES (41122, 177927, 0, 3, 'I''d like training!', 0, 0, 0, 0, 0, 0, 0, NULL, 0, 0);
COMMIT;
