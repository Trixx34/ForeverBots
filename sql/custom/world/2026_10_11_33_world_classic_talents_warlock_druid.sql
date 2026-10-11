-- Classic 1.60 (WoW Forever) warlock and druid talents: script bindings and serverside helper spell
-- (src/server/scripts/Custom/Classic/world/talents/classic_talents_warlock_druid.cpp)

DELETE FROM `spell_script_names` WHERE `ScriptName` IN ('classic_talent_warl_life_tap', 'classic_talent_warl_demonic_energies', 'classic_talent_warl_improved_shadow_bolt', 'classic_talent_warl_soul_siphon', 'classic_talent_warl_soul_harvest', 'classic_talent_warl_demon_talent_sync', 'classic_talent_warl_summon_demon', 'classic_talent_warl_demonic_sacrifice', 'classic_talent_warl_conflagrate', 'classic_talent_warl_shadowburn', 'classic_talent_warl_bane_of_havoc_tracker', 'classic_talent_warl_bane_of_havoc', 'classic_talent_warl_create_healthstone', 'classic_talent_dru_form_talents', 'classic_talent_dru_thick_hide', 'classic_talent_dru_blood_frenzy', 'classic_talent_dru_eclipse', 'classic_talent_dru_eclipse_buff', 'classic_talent_dru_eclipse_starfire', 'classic_talent_dru_dreamstate', 'classic_talent_dru_king_of_the_jungle');
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
-- Life Tap (all ranks): health to mana, Improved Life Tap, Demonic Energies pet mana
(1454, 'classic_talent_warl_life_tap'),
(1455, 'classic_talent_warl_life_tap'),
(1456, 'classic_talent_warl_life_tap'),
(11687, 'classic_talent_warl_life_tap'),
(11688, 'classic_talent_warl_life_tap'),
(11689, 'classic_talent_warl_life_tap'),
-- Demonic Energies: heal the pet for a share of spell damage
(1225214, 'classic_talent_warl_demonic_energies'),
-- Improved Shadow Bolt: Shadow Vulnerability 17794 on Shadow Bolt crits
(17793, 'classic_talent_warl_improved_shadow_bolt'),
-- Soul Siphon: Drain Life, Drain Soul and Wrack damage per Affliction effect
(689, 'classic_talent_warl_soul_siphon'),
(699, 'classic_talent_warl_soul_siphon'),
(709, 'classic_talent_warl_soul_siphon'),
(7651, 'classic_talent_warl_soul_siphon'),
(11699, 'classic_talent_warl_soul_siphon'),
(11700, 'classic_talent_warl_soul_siphon'),
(1120, 'classic_talent_warl_soul_siphon'),
(8288, 'classic_talent_warl_soul_siphon'),
(8289, 'classic_talent_warl_soul_siphon'),
(11675, 'classic_talent_warl_soul_siphon'),
(1316697, 'classic_talent_warl_soul_siphon'),
-- Soul Harvest: Drain Soul kill buff 1242853
(1120, 'classic_talent_warl_soul_harvest'),
(8288, 'classic_talent_warl_soul_harvest'),
(8289, 'classic_talent_warl_soul_harvest'),
(11675, 'classic_talent_warl_soul_harvest'),
-- Master Demonologist and Demonic Knowledge: demon auras
(23785, 'classic_talent_warl_demon_talent_sync'),
(412732, 'classic_talent_warl_demon_talent_sync'),
-- Summon demon: Demonic Sacrifice cancel (Demonic Pact) and demon talent refresh
(688, 'classic_talent_warl_summon_demon'),
(691, 'classic_talent_warl_summon_demon'),
(697, 'classic_talent_warl_summon_demon'),
(712, 'classic_talent_warl_summon_demon'),
(713, 'classic_talent_warl_summon_demon'),
(427733, 'classic_talent_warl_summon_demon'),
-- Demonic Sacrifice: buff of the sacrificed demon
(18788, 'classic_talent_warl_demonic_sacrifice'),
-- Conflagrate: consumes Immolate, Shadow and Flame
(17962, 'classic_talent_warl_conflagrate'),
(18930, 'classic_talent_warl_conflagrate'),
(18931, 'classic_talent_warl_conflagrate'),
(18932, 'classic_talent_warl_conflagrate'),
(1293817, 'classic_talent_warl_conflagrate'),
(1293818, 'classic_talent_warl_conflagrate'),
-- Shadowburn: Shadow and Flame
(17877, 'classic_talent_warl_shadowburn'),
(18867, 'classic_talent_warl_shadowburn'),
(18868, 'classic_talent_warl_shadowburn'),
(18869, 'classic_talent_warl_shadowburn'),
(18870, 'classic_talent_warl_shadowburn'),
(18871, 'classic_talent_warl_shadowburn'),
-- Bane of Havoc
(1243338, 'classic_talent_warl_bane_of_havoc_tracker'),
(1225228, 'classic_talent_warl_bane_of_havoc'),
-- Create Healthstone: Improved Healthstone items
(6201, 'classic_talent_warl_create_healthstone'),
(6202, 'classic_talent_warl_create_healthstone'),
(5699, 'classic_talent_warl_create_healthstone'),
(11729, 'classic_talent_warl_create_healthstone'),
(11730, 'classic_talent_warl_create_healthstone'),
-- Druid forms: Thick Hide armor, Furor rage
(768, 'classic_talent_dru_form_talents'),
(5487, 'classic_talent_dru_form_talents'),
(9634, 'classic_talent_dru_form_talents'),
(24858, 'classic_talent_dru_form_talents'),
(16929, 'classic_talent_dru_thick_hide'),
-- Blood Frenzy: bear crit rage, cat crit extra combo point
(16958, 'classic_talent_dru_blood_frenzy'),
-- Eclipse: Wrath charges, Starfire cast time
(408248, 'classic_talent_dru_eclipse'),
(408255, 'classic_talent_dru_eclipse_buff'),
(2912, 'classic_talent_dru_eclipse_starfire'),
(8949, 'classic_talent_dru_eclipse_starfire'),
(8950, 'classic_talent_dru_eclipse_starfire'),
(8951, 'classic_talent_dru_eclipse_starfire'),
(9875, 'classic_talent_dru_eclipse_starfire'),
(9876, 'classic_talent_dru_eclipse_starfire'),
(25298, 'classic_talent_dru_eclipse_starfire'),
-- Dreamstate: spell crit mana regen while casting
(408258, 'classic_talent_dru_dreamstate'),
-- King of the Jungle: Tiger Fury energy
(5217, 'classic_talent_dru_king_of_the_jungle');

-- Thick Hide base armor helper (serverside, passive hidden SPELL_AURA_MOD_BASE_RESISTANCE armor, amount set by the script)
DELETE FROM `serverside_spell` WHERE `Id` = 9970701;
INSERT INTO `serverside_spell` (`Id`, `DifficultyID`, `Attributes`, `CastingTimeIndex`, `ProcChance`, `DurationIndex`, `RangeIndex`, `EquippedItemClass`, `SpellName`, `SpellFamilyName`, `SchoolMask`) VALUES
(9970701, 0, 448, 1, 0, 21, 1, -1, 'Thick Hide', 7, 1);
DELETE FROM `serverside_spell_effect` WHERE `SpellID` = 9970701;
INSERT INTO `serverside_spell_effect` (`SpellID`, `EffectIndex`, `DifficultyID`, `Effect`, `EffectAura`, `EffectBasePoints`, `EffectMiscValue1`, `ImplicitTarget1`) VALUES
(9970701, 0, 0, 6, 83, 0, 1, 1);
