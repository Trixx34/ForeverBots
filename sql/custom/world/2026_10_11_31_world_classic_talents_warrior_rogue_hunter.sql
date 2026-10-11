-- Classic 1.60 (WoW Forever) warrior, rogue and hunter talents and abilities that are dummy auras / dummy effects in the client data
-- (scripts in src/server/scripts/Custom/Classic/world/talents/classic_talents_warrior_rogue_hunter.cpp)

DELETE FROM `spell_script_names` WHERE `ScriptName` IN (
'classic_spell_warr_stance_rage','classic_spell_warr_enrage_talent','classic_spell_warr_blood_craze','classic_spell_warr_weaponmaster',
'classic_spell_weapon_extra_attack_chance','classic_spell_warr_meathook','classic_spell_rog_hack_and_slash','classic_spell_rog_setup',
'classic_spell_rog_seal_fate','classic_spell_rog_cutthroat','classic_spell_rog_offhand_strike','classic_spell_rog_offhand_poison',
'classic_spell_rog_redirect','classic_spell_hun_entrapment_trap','classic_spell_hun_entrapment_frost_trap','classic_spell_hun_spirit_bond',
'classic_spell_hun_rapid_recuperation','classic_spell_hun_rapid_killing_consumed','classic_spell_hun_lacerating_strikes',
'classic_spell_hun_kill_command','classic_spell_hun_kill_command_pet');
INSERT INTO `spell_script_names` (`spell_id`,`ScriptName`) VALUES
-- warrior: stance change keeps only Tactical Mastery + Improved Tactical Mastery rage
(2457,'classic_spell_warr_stance_rage'),
(71,'classic_spell_warr_stance_rage'),
(2458,'classic_spell_warr_stance_rage'),
(12317,'classic_spell_warr_enrage_talent'),
(16487,'classic_spell_warr_blood_craze'),
(1290261,'classic_spell_warr_weaponmaster'),
(12281,'classic_spell_weapon_extra_attack_chance'),
(1290312,'classic_spell_weapon_extra_attack_chance'),
(403228,'classic_spell_warr_meathook'),
-- rogue
(13960,'classic_spell_rog_hack_and_slash'),
(13983,'classic_spell_rog_setup'),
(14186,'classic_spell_rog_seal_fate'),
(462708,'classic_spell_rog_cutthroat'),
(424799,'classic_spell_rog_offhand_strike'),
(425012,'classic_spell_rog_offhand_strike'),
(424800,'classic_spell_rog_offhand_poison'),
(425013,'classic_spell_rog_offhand_poison'),
(438040,'classic_spell_rog_redirect'),
-- hunter: Entrapment on the trap effects (Freezing, Immolation, Explosive) and the Frost Trap aura
(3355,'classic_spell_hun_entrapment_trap'),
(14308,'classic_spell_hun_entrapment_trap'),
(14309,'classic_spell_hun_entrapment_trap'),
(13797,'classic_spell_hun_entrapment_trap'),
(14298,'classic_spell_hun_entrapment_trap'),
(14299,'classic_spell_hun_entrapment_trap'),
(14300,'classic_spell_hun_entrapment_trap'),
(14301,'classic_spell_hun_entrapment_trap'),
(13812,'classic_spell_hun_entrapment_trap'),
(14314,'classic_spell_hun_entrapment_trap'),
(14315,'classic_spell_hun_entrapment_trap'),
(13810,'classic_spell_hun_entrapment_frost_trap'),
(19578,'classic_spell_hun_spirit_bond'),
(1223987,'classic_spell_hun_rapid_recuperation'),
(415407,'classic_spell_hun_rapid_killing_consumed'),
(1310533,'classic_spell_hun_lacerating_strikes'),
(409379,'classic_spell_hun_kill_command'),
(409418,'classic_spell_hun_kill_command_pet');

-- 13983 Setup: dodge or full resist only (HitMask 0x18), 1 sec cooldown, the script rolls the rank chance
-- 14186 Seal Fate: crits only, hit phase, triggered strikes (Shiv) may proc, 500 ms cooldown, the script rolls the rank chance
-- 12281 Weaponmaster (sword) and 1290312 Hack and Slash (axe and sword): extra attack, the script rolls the rank chance, 200 ms cooldown
-- 462707 Cutthroat buff: the next Ambush (rogue family flag 0x200) uses the charge on cast
DELETE FROM `spell_proc` WHERE `SpellId` IN (13983,14186,12281,1290312,462707);
INSERT INTO `spell_proc` (`SpellId`,`SchoolMask`,`SpellFamilyName`,`SpellFamilyMask0`,`SpellFamilyMask1`,`SpellFamilyMask2`,`SpellFamilyMask3`,`ProcFlags`,`ProcFlags2`,`SpellTypeMask`,`SpellPhaseMask`,`HitMask`,`AttributesMask`,`DisableEffectsMask`,`ProcsPerMinute`,`Chance`,`Cooldown`,`Charges`) VALUES
(13983,0,0,0,0,0,0,0,0,0,0,0x18,0,0,0,100,1000,0),
(14186,0,0,0,0,0,0,0,0,0,0x2,0x2,0x2,0,0,100,500,0),
(12281,0,0,0,0,0,0,0,0,0,0x2,0,0,0,0,100,200,0),
(1290312,0,0,0,0,0,0,0,0,0,0x2,0,0,0,0,100,200,0),
(462707,0,8,0x200,0,0,0,0x10,0,0,0x1,0,0,0,0,100,0,0);
