# Queued fixes from the 1-5 run (2026-10-08), item 4

Not built and not sim-verified. Needs a build, a worldserver restart (SQL), and a sim run.

1. Item 264908 (Ancient Heirloom): the reconstructed hotfix rows (Wowhead data, not client DB2) moved into the dated update `sql/custom/hotfixes/2026_10_08_01_hotfixes_forever_ancient_heirloom.sql`. REWARD_ITEM_MISSING stays as a fallback. Icon/flavour text come from Wowhead, so check the item in-game.
2. Death code (BotBehavior.cpp): the corpse run and spirit healer walks already had a fail cap (`Bot.AI.Death.CorpseRunMaxFails`), but no total time cap, and a missing healer retried every 30 s forever. New `Bot.AI.Death.GiveUpSec` (default 300, 0 = off): a ghost over the cap is resurrected at the closest graveyard (`CORPSE_RUN_TIMEOUT` / `SPIRIT_HEAL_TIMEOUT`). `SPIRIT_HEAL_GAVE_UP` now also respawns at the graveyard instead of in place.
3. TRAIN_NO_TRAINER: the data gaps (dwarf shaman, Exile's Reach trainers, undead paladin) were already written as SQL; moved to `sql/custom/world/2026_10_08_02_..._sim_trainers.sql` and `2026_10_08_03_..._undead_paladin_trainer.sql`. Code fallback in BotQuest.cpp: when no friendly class trainer is within 4000 yd on the map, retry with 15000 yd. The "class 2 on map 0" rows could not be traced further without the raw event rows; the `why` detail on each row says which gap remains. Check after a run.
4. Reason codes: all 21 were already in the registry (`quest-design.md` 6.2.1); added the two new death codes and updated TRAIN_NO_TRAINER.
5. Skyborne NO_PATH skipped as requested.
