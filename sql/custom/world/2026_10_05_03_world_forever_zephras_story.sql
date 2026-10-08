-- Re-run after the sniff files (2026_10_01_03/04, 2026_10_02_00) were regenerated: they rewrite the creatures, spawns and AI this file adjusts (last: 2026-10-05, sniff of 2026-10-04 20:05).
-- Classic 1.60 (WoW Forever), Zephras Isle story from the official beta sniff 70205 of 2026-10-04 (classic_re sniff_scene2.py /
-- sniff_phases.py). Safe to run again.
--   The Cults True Plans (94568): Talaanis recall crystal scene (classic_npc_talaanis_shadowsong).
--   Desperate Times (92640): Valennias plan after "<Nod at Valennia.>" (classic_npc_valennia_stormfist).
--   Making Our Move (92947): the leaders at the muster in Valanaar (classic_npc_valennia_muster).
--   Confront Lorthuna (92646): the battle at the spire (classic_npc_ayessa_spire).
--   Phases: 29805 = the muster in Valanaar and at the shrine approach (Prepare for Battle accepted, until Confront Lorthuna is done),
--   29806 = the allies fighting at the shrine (Prepare for Battle rewarded); zone Zephras Isle 16593.

UPDATE `creature_template` SET `AIName` = '', `ScriptName` = 'classic_npc_talaanis_shadowsong' WHERE `entry` = 252476;
UPDATE `creature_template` SET `AIName` = '', `ScriptName` = 'classic_npc_valennia_stormfist' WHERE `entry` = 252383;
UPDATE `creature_template` SET `AIName` = '', `ScriptName` = 'classic_npc_valennia_muster' WHERE `entry` = 253844;
UPDATE `creature_template` SET `AIName` = '', `ScriptName` = 'classic_npc_ayessa_spire', `npcflag` = `npcflag` | 1 WHERE `entry` = 253849;
-- the recall crystal projections had the sniff AI casting their visuals on spawn; the scene casts them
UPDATE `creature_template` SET `AIName` = '', `ScriptName` = '' WHERE `entry` IN (264712, 264713);
DELETE FROM `smart_scripts` WHERE `entryorguid` IN (252476, 252383, 253844, 253849, 264712, 264713) AND `source_type` = 0;

-- summoned by the scenes, the sniff import had spawned what it saw
DELETE FROM `creature` WHERE `map` = 2991 AND `id` IN (264712, 264713);
DELETE FROM `creature` WHERE `map` = 2991 AND `id` IN (252957, 255833, 256617, 255830, 256620, 256621, 257862, 257866, 264259, 256310)
    AND `position_x` BETWEEN 2950 AND 3010 AND `position_y` BETWEEN 0 AND 100;
DELETE FROM `gameobject` WHERE `map` = 2991 AND `id` = 617075;

-- "High Elder, what about this crystal?" only while the crystal was not heard yet
DELETE FROM `conditions` WHERE `SourceTypeOrReferenceId` = 15 AND `SourceGroup` = 42169 AND `SourceEntry` = 140111;
INSERT INTO `conditions` (`SourceTypeOrReferenceId`, `SourceGroup`, `SourceEntry`, `SourceId`, `ElseGroup`, `ConditionTypeOrReference`, `ConditionTarget`, `ConditionValue1`, `ConditionValue2`, `ConditionValue3`, `ConditionStringValue1`, `NegativeCondition`, `ErrorType`, `ErrorTextId`, `ScriptName`, `Comment`) VALUES
(15, 42169, 140111, 0, 0, 47, 0, 94568, 8, 0, '', 0, 0, 0, '', 'Talaanis Shadowsong - show option while The Cult\'s True Plans is in progress'),
(15, 42169, 140111, 0, 0, 48, 0, 474392, 0, 0, '', 0, 0, 0, '', 'Talaanis Shadowsong - show option if the crystal was not heard yet');

-- Ayessa on the spire: her greeting (the line she says when the party lands)
DELETE FROM `npc_text` WHERE `ID` = 20253849;
INSERT INTO `npc_text` (`ID`, `Probability0`, `Probability1`, `Probability2`, `Probability3`, `Probability4`, `Probability5`, `Probability6`, `Probability7`, `BroadcastTextID0`, `BroadcastTextID1`, `BroadcastTextID2`, `BroadcastTextID3`, `BroadcastTextID4`, `BroadcastTextID5`, `BroadcastTextID6`, `BroadcastTextID7`, `VerifiedBuild`) VALUES
(20253849, 1, 0, 0, 0, 0, 0, 0, 0, 306575, 0, 0, 0, 0, 0, 0, 0, 70205);

-- quest phases
DELETE FROM `phase_area` WHERE `AreaId` = 16593 AND `PhaseId` IN (29805, 29806);
INSERT INTO `phase_area` (`AreaId`, `PhaseId`, `Comment`) VALUES
(16593, 29805, 'Zephras Isle - the muster for the assault on the shrine'),
(16593, 29806, 'Zephras Isle - the allies fighting at the shrine');
DELETE FROM `conditions` WHERE `SourceTypeOrReferenceId` = 26 AND `SourceGroup` IN (29805, 29806) AND `SourceEntry` = 16593;
INSERT INTO `conditions` (`SourceTypeOrReferenceId`, `SourceGroup`, `SourceEntry`, `SourceId`, `ElseGroup`, `ConditionTypeOrReference`, `ConditionTarget`, `ConditionValue1`, `ConditionValue2`, `ConditionValue3`, `ConditionStringValue1`, `NegativeCondition`, `ErrorType`, `ErrorTextId`, `ScriptName`, `Comment`) VALUES
(26, 29805, 16593, 0, 0, 9, 0, 93065, 0, 0, '', 0, 0, 0, '', 'Phase 29805 - Prepare for Battle taken'),
(26, 29805, 16593, 0, 1, 28, 0, 93065, 0, 0, '', 0, 0, 0, '', 'Phase 29805 - or Prepare for Battle complete'),
(26, 29805, 16593, 0, 2, 8, 0, 93065, 0, 0, '', 0, 0, 0, '', 'Phase 29805 - or Prepare for Battle rewarded'),
(26, 29805, 16593, 0, 2, 28, 0, 92646, 0, 0, '', 1, 0, 0, '', 'Phase 29805 - and Confront Lorthuna not complete'),
(26, 29805, 16593, 0, 2, 8, 0, 92646, 0, 0, '', 1, 0, 0, '', 'Phase 29805 - and Confront Lorthuna not rewarded'),
(26, 29806, 16593, 0, 0, 8, 0, 93065, 0, 0, '', 0, 0, 0, '', 'Phase 29806 - Prepare for Battle rewarded');
-- who is in them (first created on the official server right when the phase came: 8617.2 s / 8702.6 s of the sniff)
UPDATE `creature` SET `PhaseId` = 29805 WHERE `map` = 2991 AND `id` IN (253812, 253813, 253844, 253576);
UPDATE `creature` SET `PhaseId` = 29805 WHERE `map` = 2991 AND `id` IN (254287, 254294, 254296)
    AND ((`position_x` BETWEEN 2295 AND 2335 AND `position_y` BETWEEN 810 AND 865) OR (`position_x` BETWEEN 3070 AND 3095 AND `position_y` BETWEEN 675 AND 695));
UPDATE `creature` SET `PhaseId` = 29806 WHERE `map` = 2991 AND `id` IN (254294, 254296) AND `position_x` > 2900 AND NOT (`position_x` BETWEEN 3070 AND 3095 AND `position_y` BETWEEN 675 AND 695);

DELETE FROM `creature_text` WHERE `CreatureID` IN (252476,252383,264712,253844,253813,253812,253849,253847,252957,255833,256620,256621);
INSERT INTO `creature_text` (`CreatureID`, `GroupID`, `ID`, `Text`, `Type`, `Language`, `Probability`, `Emote`, `Duration`, `Sound`, `SoundPlayType`, `BroadcastTextId`, `TextRange`, `comment`) VALUES
(252476, 0, 0, 'Wait, yes... this crystal! Valennia, is this...', 12, 0, 100, 0, 0, 0, 0, 314601, 0, 'Talaanis Shadowsong'),
(252476, 1, 0, 'This is surely what the turncoat hoped to offer us in exchange for our protection.', 12, 0, 100, 0, 0, 0, 0, 314603, 0, 'Talaanis Shadowsong'),
(252476, 2, 0, 'Activate it, Valennia. We need to learn as much as we can about what it contains.', 12, 0, 100, 0, 0, 0, 0, 314605, 0, 'Talaanis Shadowsong'),
(252476, 3, 0, 'Wards... what is this about wards, Valennia?', 12, 0, 100, 0, 0, 0, 0, 314610, 0, 'Talaanis Shadowsong'),
(252476, 4, 0, '...whatever else is out there in Skywall. Spirits save us... this is her plan. Bring down the wards and let her \'Windlord\' in!', 12, 0, 100, 0, 0, 0, 0, 314612, 0, 'Talaanis Shadowsong'),
(252383, 0, 0, 'With the cult running roughshod over most of the island with impunity, Lorthuna undoubtedly thinks we are on the back foot. We can use that overconfidence against her.', 12, 0, 100, 0, 0, 0, 0, 302063, 0, 'Valennia Stormfist'),
(252383, 1, 0, 'I believe that with a proper distraction at the shrine, a small force of Peacekeepers, Windshapers, and High Order should be able to push up a flank and break into the inner sanctum. From there, we take out the High Priestess herself and cut the head off the snake.', 12, 0, 100, 0, 0, 0, 0, 302064, 0, 'Valennia Stormfist'),
(252383, 2, 0, 'Go and speak with the leaders of the Windshapers and the High Order, respectively. They don\'t get along, but I think they can recognize that the bigger threat is the Al\'Aketh. Get them to agree to the plan and then return here.', 12, 0, 100, 0, 0, 0, 0, 302065, 0, 'Valennia Stormfist'),
(252383, 3, 0, '...a recall crystal! Used to record an important moment, no doubt.', 12, 0, 100, 0, 0, 0, 0, 314602, 0, 'Valennia Stormfist'),
(252383, 4, 0, 'The recall crystal is badly damaged, though. The recording is likely to be incomplete, so we must listen carefully.', 12, 0, 100, 0, 0, 0, 0, 314604, 0, 'Valennia Stormfist'),
(252383, 5, 0, 'The crystal is too damaged to play any further, I am afraid.', 12, 0, 100, 0, 0, 0, 0, 314609, 0, 'Valennia Stormfist'),
(252383, 6, 0, 'The anchor pylons keep us suspended in this realm, but they also act as wards! The Shal\'nan once spoke of this. They keep us shielded from...', 12, 0, 100, 0, 0, 0, 0, 314611, 0, 'Valennia Stormfist'),
(264712, 0, 0, '...do not delay. Muster our forces to march immediately. The pylons are key and...the wards...come down. This is...', 12, 0, 100, 0, 0, 0, 0, 314606, 0, 'High Priestess Lorthuna (recall crystal)'),
(264712, 1, 0, '...quickly. With our forces.....will be exposed.....my ritual must not....', 12, 0, 100, 0, 0, 0, 0, 314607, 0, 'High Priestess Lorthuna (recall crystal)'),
(264712, 2, 0, 'I alone am.....this task....the Windlord will.......our voices.....he will come.....we must not fail!', 12, 0, 100, 0, 0, 0, 0, 314608, 0, 'High Priestess Lorthuna (recall crystal)'),
(253844, 0, 0, 'You have my thanks. Being truthful, however, this is going to be a dangerous gambit and we may not succeed. It\'s important you understand that this might be a one-way ride.', 12, 0, 100, 0, 0, 0, 0, 302445, 0, 'Valennia Stormfist (muster)'),
(253844, 1, 0, 'Outside... of Zephras? From the other provinces? We haven\'t had contact with the other islands for years...?', 12, 0, 100, 0, 0, 0, 0, 302447, 0, 'Valennia Stormfist (muster)'),
(253844, 2, 0, 'Very well then. Time is not with us so we should make ourselves ready. Thank you all for your aid. Zephras is in your debt.', 12, 0, 100, 0, 0, 0, 0, 302449, 0, 'Valennia Stormfist (muster)'),
(253813, 0, 0, '...and the Windshapers. We are ready to support an assault on the Shrine, Valennia.', 12, 0, 100, 0, 0, 0, 0, 302444, 0, 'Ayessa Dawnsinger (muster)'),
(253813, 1, 0, 'We understand. As it happens Elaadrin and I spoke. We made arrangements to send for... outside aid. It\'s unclear if they will be able to arrive in time to help, or if they will even arrive at all...', 12, 0, 100, 0, 0, 0, 0, 302446, 0, 'Ayessa Dawnsinger (muster)'),
(253812, 0, 0, 'We came as soon as we were told what was happening. The High Order is here to serve Zephras.', 12, 0, 100, 0, 0, 0, 0, 302443, 0, 'Elaadrin Evengale (muster)'),
(253812, 1, 0, 'It\'s... coming from a little further away than that. We\'ll explain on the way.', 12, 0, 100, 0, 0, 0, 0, 302448, 0, 'Elaadrin Evengale (muster)'),
(253849, 0, 0, 'Lorthuna! Stop this madness. You don\'t know what bringing down the wards protecting Zephras will do. You could doom us all!', 12, 0, 100, 0, 0, 0, 0, 304299, 0, 'Ayessa Dawnsinger (spire)'),
(253849, 1, 0, 'Nor is this how I envisioned our first meeting, Muln.', 12, 0, 100, 0, 0, 0, 0, 305109, 0, 'Ayessa Dawnsinger (spire)'),
(253849, 2, 0, 'Muln Earthfury of the Earthen Ring--the Windshapers would be happy to accept you as our guest.', 12, 0, 100, 0, 0, 0, 0, 305134, 0, 'Ayessa Dawnsinger (spire)'),
(253847, 0, 0, 'I\'ll make this easy for you, witch. Stop what you are doing now and I will make your death swift. This is the last and best offer you will get from the High Order.', 12, 0, 100, 0, 0, 0, 0, 304304, 0, 'Elaadrin Evengale (spire)'),
(253847, 1, 0, 'Ansirem! This isn\'t how I expected our first meeting to go, but I\'m glad you made it!', 12, 0, 100, 0, 0, 0, 0, 305108, 0, 'Elaadrin Evengale (spire)'),
(253847, 2, 0, 'Yes, and you, Archmage Runeweaver. The High Order is eager to learn more of the Kirin Tor.', 12, 0, 100, 0, 0, 0, 0, 305135, 0, 'Elaadrin Evengale (spire)'),
(252957, 0, 0, 'Ayessa... my former sister. How far you\'ve fallen, keeping company with this High Order filth.', 14, 0, 100, 0, 0, 0, 0, 304303, 0, 'High Priestess Lorthuna (spire)'),
(252957, 1, 0, 'Bring the wards down? The wards have been down for days, fools. No, I am merely lighting a beacon... for the true servants of Al\'Akir to follow.', 14, 0, 100, 0, 0, 0, 0, 304307, 0, 'High Priestess Lorthuna (spire)'),
(252957, 2, 0, 'Empty threats, Elaadrin. Regardless of what happens to me now, my great work is done. Consider yourself lucky to be here at the eye of the storm, to witness the fruits of our labor.', 14, 0, 100, 0, 0, 0, 0, 305131, 0, 'High Priestess Lorthuna (spire)'),
(252957, 3, 0, 'Ahh... yes! The call has been answered. Meet your end with joy!', 14, 0, 100, 0, 0, 0, 0, 304433, 0, 'High Priestess Lorthuna (spire)'),
(252957, 4, 0, 'Yes! Yes! It is by my will that the way to Zephras is open to you! Bring me before your Windlord so that he may know who was the herald of his glory!', 14, 0, 100, 0, 0, 0, 0, 304445, 0, 'High Priestess Lorthuna (spire)'),
(252957, 5, 0, 'No! Never, I would never dare!', 14, 0, 100, 0, 0, 0, 0, 304448, 0, 'High Priestess Lorthuna (spire)'),
(255833, 0, 0, 'Who dares call the Lord of the East Wind?', 14, 0, 100, 0, 0, 0, 0, 304441, 0, 'Rohash'),
(255833, 1, 0, 'Wait... what is this place? How could such a place exist in the Windlord\'s realm?', 14, 0, 100, 0, 0, 0, 0, 305132, 0, 'Rohash'),
(255833, 2, 0, 'So the rumors were true. My wayward brothers did well to conceal this place from the gaze of Al\'Akir.', 14, 0, 100, 0, 0, 0, 0, 304443, 0, 'Rohash'),
(255833, 3, 0, 'You dare to command me, mortal? No, I think not. Begone, insignificant elf!', 14, 0, 100, 0, 0, 0, 0, 304447, 0, 'Rohash'),
(255833, 4, 0, 'Well, you certainly never will again, insect. Hah!', 14, 0, 100, 0, 0, 0, 0, 304449, 0, 'Rohash'),
(255833, 5, 0, 'Enjoy your remaining time as "guests" in my lord\'s realm until his judgement inevitably arrives. You\'ll know not when, but it is coming.', 14, 0, 100, 0, 0, 0, 0, 304452, 0, 'Rohash'),
(255833, 6, 0, 'Now that this wondrous realm is no longer hidden from us, you are going to enjoy getting to know the TRUE children of Skywall...', 14, 0, 100, 0, 0, 0, 0, 304454, 0, 'Rohash'),
(255833, 7, 0, 'Well if this isn\'t an inspirational sight. The little elves seem to have friends in... low places.', 14, 0, 100, 0, 0, 0, 0, 305110, 0, 'Rohash'),
(255833, 8, 0, 'Very well, mortals. For now enjoy your respite. I simply cannot wait to tell the Windlord about all that has transpired here. You will be seeing me again... very soon.', 14, 0, 100, 0, 0, 0, 0, 305112, 0, 'Rohash'),
(256620, 0, 0, 'Sorry that we are late, Ayessa.', 12, 0, 100, 0, 0, 0, 0, 305106, 0, 'Muln Earthfury'),
(256620, 1, 0, 'We have no quarrel with you, Lord of the East Wind. Leave us in peace.', 12, 0, 100, 0, 0, 0, 0, 305111, 0, 'Muln Earthfury'),
(256621, 0, 0, 'It took a bit of work to convince Archmage Rhonin to help me open that transplanar portal...', 12, 0, 100, 0, 0, 0, 0, 305107, 0, 'Archmage Ansirem Runeweaver'),
(256621, 1, 0, 'I think it\'s time for us to be leaving this place. I will open a portal to Valanaar.', 12, 0, 100, 0, 0, 0, 0, 305133, 0, 'Archmage Ansirem Runeweaver');

-- The Windshapers (3599) and the High Order (3600) are enemies in FactionTemplate.db2: their copies in the muster, at the shrine and
-- at the spire attacked each other here. They fight on the same side in these scenes: the Peacekeepers faction (3583, hostile only
-- to the AlAketh, friendly to players).
UPDATE `creature_template` SET `faction` = 3583 WHERE `entry` IN (253812, 253813, 254294, 254296, 253847, 253849, 256618, 256619);

-- Confront Lorthuna: the official server offered it 6 s after The Inner Sanctum was turned in (sniff 70205)
UPDATE `quest_template` SET `RewardNextQuest` = 92646 WHERE `ID` = 93958;
-- its credit (256634, given by Rohashs 1269471) was never spawned, so the import had no template for it
DELETE FROM `creature_template` WHERE `entry` = 256634;
INSERT INTO `creature_template` (`entry`, `name`, `KillCredit1`, `KillCredit2`, `femaleName`, `subname`, `TitleAlt`, `IconName`, `RequiredExpansion`, `VignetteID`, `faction`, `npcflag`, `speed_walk`, `speed_run`, `scale`, `Classification`, `dmgschool`, `BaseAttackTime`, `RangeAttackTime`, `BaseVariance`, `RangeVariance`, `unit_class`, `unit_flags`, `unit_flags2`, `unit_flags3`, `family`, `trainer_class`, `type`, `VehicleId`, `AIName`, `MovementType`, `ExperienceModifier`, `RacialLeader`, `movementId`, `WidgetSetID`, `WidgetSetUnitConditionID`, `RegenHealth`, `CreatureImmunitiesId`, `flags_extra`, `ScriptName`, `StringId`, `VerifiedBuild`)
SELECT 256634, 'Confront Lorthuna Credit', `KillCredit1`, `KillCredit2`, `femaleName`, `subname`, `TitleAlt`, `IconName`, `RequiredExpansion`, `VignetteID`, `faction`, `npcflag`, `speed_walk`, `speed_run`, `scale`, `Classification`, `dmgschool`, `BaseAttackTime`, `RangeAttackTime`, `BaseVariance`, `RangeVariance`, `unit_class`, `unit_flags`, `unit_flags2`, `unit_flags3`, `family`, `trainer_class`, `type`, `VehicleId`, `AIName`, `MovementType`, `ExperienceModifier`, `RacialLeader`, `movementId`, `WidgetSetID`, `WidgetSetUnitConditionID`, `RegenHealth`, `CreatureImmunitiesId`, `flags_extra`, `ScriptName`, `StringId`, `VerifiedBuild` FROM `creature_template` WHERE `entry` = 270313;
DELETE FROM `creature_template_difficulty` WHERE `Entry` = 256634;
INSERT INTO `creature_template_difficulty` (`Entry`, `DifficultyID`, `HealthModifier`, `ManaModifier`, `ArmorModifier`, `DamageModifier`, `CreatureDifficultyID`, `TypeFlags`, `VerifiedBuild`)
SELECT 256634, `DifficultyID`, `HealthModifier`, `ManaModifier`, `ArmorModifier`, `DamageModifier`, `CreatureDifficultyID`, `TypeFlags`, `VerifiedBuild` FROM `creature_template_difficulty` WHERE `Entry` = 270313;
DELETE FROM `creature_template_model` WHERE `CreatureID` = 256634;
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`)
SELECT 256634, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild` FROM `creature_template_model` WHERE `CreatureID` = 270313;

-- the leaders and their escort on the spire stand in formation until the party is ready (the import gave them the wandering
-- they did during the official battle)
UPDATE `creature` SET `MovementType` = 0, `wander_distance` = 0 WHERE `map` = 2991 AND `id` IN (253847, 253849, 256618, 256619);

-- the Portal to Valanaar appears at the end of the scene (classic_npc_ayessa_spire); it takes the players home: 1273007 triggers
-- 1273006, a teleport to Valanaar (the player arrived at 2131.4 839.8 671.4)
DELETE FROM `gameobject` WHERE `map` = 2991 AND `id` = 617084;
DELETE FROM `spell_target_position` WHERE `ID` = 1273006 AND `EffectIndex` = 0;
INSERT INTO `spell_target_position` (`ID`, `EffectIndex`, `OrderIndex`, `MapID`, `PositionX`, `PositionY`, `PositionZ`, `Orientation`, `VerifiedBuild`) VALUES
(1273006, 0, 0, 2991, 2131.4, 839.8, 671.4, 3.58, 70205);

-- Ayessa on the spire builds her menu in classic_npc_ayessa_spire, but the server hides the gossip flag from players when the
-- creature has no gossip menu of its own (Player::CanSeeGossipOn): a menu with her line
DELETE FROM `gossip_menu` WHERE `MenuID` = 90253849;
INSERT INTO `gossip_menu` (`MenuID`, `TextID`, `VerifiedBuild`) VALUES (90253849, 20253849, 70205);
DELETE FROM `creature_template_gossip` WHERE `CreatureID` = 253849;
INSERT INTO `creature_template_gossip` (`CreatureID`, `MenuID`, `VerifiedBuild`) VALUES (253849, 90253849, 70205);
