-- Classic 1.60: Legacy reward track. Reaching 15 / 25 / 40 / 55 Legacy Points makes a reward available; Innkeeper Wiley (6791,
-- Ratchet) gives it as a quest (official sniff 2026-10-10: "Legacy Reward: Replica Ironforge Air Rifle", 96339, turned in there).
-- Rewards from RenownRewards (client 70338, group 48): 15 toy 276236, 25 pet 277714, 40 tabard 277717, 55 mount 277718.
-- Only 96339 was sniffed; 96340-96342 follow it in the client quest list and are made from it (titles from the item names,
-- not confirmed). Each quest needs the Legacy Points of its level: PlayerCondition 9900001-9900004 (hotfixes 2026_10_10_02).
-- Safe to run again.
DELETE FROM `quest_template` WHERE `ID` IN (96340,96341,96342);

DROP TEMPORARY TABLE IF EXISTS `tmp_legacy_reward`;
CREATE TEMPORARY TABLE `tmp_legacy_reward` LIKE `quest_template`;
INSERT INTO `tmp_legacy_reward` SELECT * FROM `quest_template` WHERE `ID` = 96339;
UPDATE `tmp_legacy_reward` SET `ID` = 96340, `LogTitle` = 'Legacy Reward: Spectral Bear Cub', `RewardItem1` = 277714, `VerifiedBuild` = 0;
INSERT INTO `quest_template` SELECT * FROM `tmp_legacy_reward`;
UPDATE `tmp_legacy_reward` SET `ID` = 96341, `LogTitle` = 'Legacy Reward: Spectral Bear Tabard', `RewardItem1` = 277717;
INSERT INTO `quest_template` SELECT * FROM `tmp_legacy_reward`;
UPDATE `tmp_legacy_reward` SET `ID` = 96342, `LogTitle` = 'Legacy Reward: Reins of the Spectral Bear', `RewardItem1` = 277718;
INSERT INTO `quest_template` SELECT * FROM `tmp_legacy_reward`;
DROP TEMPORARY TABLE `tmp_legacy_reward`;

-- the same delivery text as the sniffed 96339
DELETE FROM `quest_offer_reward` WHERE `ID` IN (96340,96341,96342);
INSERT INTO `quest_offer_reward` (`ID`,`Emote1`,`Emote2`,`Emote3`,`Emote4`,`EmoteDelay1`,`EmoteDelay2`,`EmoteDelay3`,`EmoteDelay4`,`RewardText`,`VerifiedBuild`)
SELECT q.`ID`,r.`Emote1`,r.`Emote2`,r.`Emote3`,r.`Emote4`,r.`EmoteDelay1`,r.`EmoteDelay2`,r.`EmoteDelay3`,r.`EmoteDelay4`,r.`RewardText`,0
FROM `quest_offer_reward` r JOIN (SELECT 96340 AS `ID` UNION SELECT 96341 UNION SELECT 96342) q WHERE r.`ID` = 96339;

DELETE FROM `creature_queststarter` WHERE `quest` IN (96339,96340,96341,96342);
INSERT INTO `creature_queststarter` (`id`,`quest`,`VerifiedBuild`) VALUES
(6791,96339,0),(6791,96340,0),(6791,96341,0),(6791,96342,0);
DELETE FROM `creature_questender` WHERE `quest` IN (96339,96340,96341,96342);
INSERT INTO `creature_questender` (`id`,`quest`,`VerifiedBuild`) VALUES
(6791,96339,0),(6791,96340,0),(6791,96341,0),(6791,96342,0);

-- available from the Legacy Points of its level (CONDITION_SOURCE_TYPE_QUEST_AVAILABLE, CONDITION_PLAYER_CONDITION)
DELETE FROM `conditions` WHERE `SourceTypeOrReferenceId` = 19 AND `SourceEntry` IN (96339,96340,96341,96342);
INSERT INTO `conditions` (`SourceTypeOrReferenceId`,`SourceGroup`,`SourceEntry`,`SourceId`,`ElseGroup`,`ConditionTypeOrReference`,`ConditionTarget`,`ConditionValue1`,`ConditionValue2`,`ConditionValue3`,`NegativeCondition`,`ErrorType`,`ErrorTextId`,`ScriptName`,`Comment`) VALUES
(19,0,96339,0,0,56,0,9900001,0,0,0,0,0,'','Legacy Reward: Replica Ironforge Air Rifle - 15 Legacy Points'),
(19,0,96340,0,0,56,0,9900002,0,0,0,0,0,'','Legacy Reward: Spectral Bear Cub - 25 Legacy Points'),
(19,0,96341,0,0,56,0,9900003,0,0,0,0,0,'','Legacy Reward: Spectral Bear Tabard - 40 Legacy Points'),
(19,0,96342,0,0,56,0,9900004,0,0,0,0,0,'','Legacy Reward: Reins of the Spectral Bear - 55 Legacy Points');
