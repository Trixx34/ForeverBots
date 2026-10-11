-- Classic 1.60: retail TrinityCore spell_proc rows on vanilla item / talent procs.
-- Rows whose 1.60 client data (proc chance and ProcCategoryRecovery) is what Forever ships: removed so the client values apply.
-- Hand of Justice 15600 (client 3% / 2 s, retail row 2%), Invulnerable Mail 16620 (retail row 30 ms cooldown), Spinal Reaper 21185 (10 ms).
DELETE FROM `spell_proc` WHERE `SpellId` IN (15600,16620,21185);
-- Rows where the client data matches vanilla (100%) and vanilla used a server-side rate (VMaNGOS spell_proc_event): vanilla values.
-- Omen of Clarity 16864 2 PPM / 10 s (retail row 6% / 10 ms), Bloodfang 23581 0.6, Maelstrom 23686 1, Darkmoon Heroism 23689 3,
-- Badge of the Swarmguard 26480 10, Flame Lash 27656 1, 27787 1, Rejuvenation set bonus 28716 50%.
UPDATE `spell_proc` SET `ProcsPerMinute` = 2, `Chance` = 0, `Cooldown` = 10000 WHERE `SpellId` = 16864;
UPDATE `spell_proc` SET `ProcsPerMinute` = 0.6 WHERE `SpellId` = 23581;
UPDATE `spell_proc` SET `ProcsPerMinute` = 1 WHERE `SpellId` IN (23686,27656,27787);
UPDATE `spell_proc` SET `ProcsPerMinute` = 3 WHERE `SpellId` = 23689;
UPDATE `spell_proc` SET `ProcsPerMinute` = 10 WHERE `SpellId` = 26480;
UPDATE `spell_proc` SET `Chance` = 50 WHERE `SpellId` = 28716;
-- Lawbringer 8 piece 21747: 6 PPM in vanilla (client data 100% like the vanilla spell, the rate was server-side)
DELETE FROM `spell_proc` WHERE `SpellId` = 21747;
INSERT INTO `spell_proc` (`SpellId`,`SchoolMask`,`SpellFamilyName`,`SpellFamilyMask0`,`SpellFamilyMask1`,`SpellFamilyMask2`,`SpellFamilyMask3`,`ProcFlags`,`ProcFlags2`,`SpellTypeMask`,`SpellPhaseMask`,`HitMask`,`AttributesMask`,`DisableEffectsMask`,`ProcsPerMinute`,`Chance`,`Cooldown`,`Charges`) VALUES
(21747,0,0,0,0,0,0,0,0,1,2,0,0,0,6,0,0,0);
