-- Classic 1.60: Korv (Blackrock Depths arena, 16053) Fire Nova Totem
-- Spell 11314 Fire Nova Totem is not in the 1.60 client, classic_npc_korv now summons the totem creature 7844 by hand
-- and makes it cast Fire Nova 11307 after 5s. The creature still carries its zzOLD name, give it the vanilla one.
UPDATE `creature_template` SET `name`='Fire Nova Totem IV' WHERE `entry`=7844;
