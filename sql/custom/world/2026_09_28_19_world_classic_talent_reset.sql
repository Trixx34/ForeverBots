-- Classic 1.60: class trainers "I wish to unlearn my talents." (VMaNGOS option_id 1, handled by a script there) opens
-- TrinityCores talent reset confirmation (GossipOptionNpc::TalentMaster = 11)
UPDATE `gossip_menu_option` SET `OptionNpc`=11 WHERE `OptionNpc`=0 AND `OptionText` LIKE 'I wish to unlearn my talents%';
