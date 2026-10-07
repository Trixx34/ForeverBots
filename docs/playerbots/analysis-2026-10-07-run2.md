# 250-bot sim run: class and death analysis (2026-10-07)

Source: read-only queries on `forever_sim_log`, window 07:59:41 to 11:32 (3.55 h), 250 level-1 bots, flee mode 0 (R1, R2, R5 active). 43,219 fights, 1,823 deaths. The 22 race-7 bots never fought and stayed at level 1; they are excluded from per-bot-hour rates (228 active bots). Deaths are skewed: the top 25 bots hold 49% of them. Zone and race drive deaths more than class (Elwynn humans 24 deaths per bot, Mulgore tauren 1.3). "Observed/expected" compares deaths with what the race and level mix predicts.

## 1. Deaths by class
| Class | Deaths per bot-hour | Observed/expected |
|---|---|---|
| Hunter | 2.72 | 1.61 (271 against 169 expected) |
| Warlock | 3.33 | 1.06 (cohort is human) |
| Shaman | 0.88 | 0.70 |
| Paladin | 1.86 | 0.82 |
| Other classes | n/a | 0.87 to 1.02 |

- Death rate per fight when the mob hit first: mage, warlock, hunter, priest 11 to 19%; warrior, paladin, rogue, druid, shaman 5 to 9%.
- When the bot hit first: 0.3% for melee, about 1 to 3% for the squishy classes.
- Melee and druid start fights at about 1.1 yd and are hit first in 46 to 61% of fights. Ranged classes start at about 16.5 yd.

## 2. Casts
- 5,877 CAST_FAILED and 2,152 CAST_NO_POWER rows. All NO_POWER rows are mana. Line of sight is under 2% of failures.
- Paladin Judgement failed 4,510 times with CASTER_AURASTATE and never succeeded (cause is a hypothesis).
- Wand Shoot (result code 32, logged as OTHER) failed 1,209 times against 104 successes for priest, mage and warlock. These three never fall back to melee; shaman and druid do (14.7% and 8.3% of sessions).

## 3. Time and progress
- Time use is nearly identical across classes: combat 25 to 32%, walking about 53%, still 14 to 17%.
- Hunters lag: 28.9 kills and 1.15 level-ups per bot-hour, longest fights (15.9 s). Raptor Strike was cast 19,864 times against 813 Auto Shot. Warlocks are second-slowest at 1.20 level-ups per bot-hour.

## 4. Flee
- Flee started in 6.7% of sessions; 64% of those ended in death, 89% at 15% HP or below.
- Not causal: flee only triggers in fights already being lost, and no bot ran with flee on. An A/B at mode 1 is needed.

## 5. Avoid list (level-gap killers, rate / observed over expected)
- Defias Bandit 116: 61% / 1.54. Young Forest Bear 822: 57% / 1.44. Shadowgale Ursera 250927: 100% over 95 fights / 2.26. Narg 79: 16 of 16.
- Same-level overperformers: Prideclaw 251245 (6.8), Ravaged Corpse 1526 (5.4), Kul Tiras Sailor 3128 (4.2), Kobold Miner 40 (3.3).
- Common mobs that are not unusually lethal: Webwood Lurker, Clattering Scorpid, Nightsaber.

## 6. Proposals (none approved)
1. Avoid the level-gap killers unless within one level.
2. Pull at range (Charge, stealth, ranged spells); evidence is a correlation across classes only.
3. Stop casting Judgement until the aura-state failure is understood.
4. Make Auto Shot the hunter's main attack.
5. Melee fallback for priest, mage and warlock.
6. Run the flee A/B at mode 1.

## 7. Logging gaps
No successful-cast or aura events (out-of-combat buffs and seal state cannot be checked); result codes not decoded (32 and 172 show as OTHER); no equipment, ammo or pet detail; COMBAT_SUMMARY has no fight_id or outcome; no pack or neighbour field at pull time; 14 to 17% of bot time has no logged reason; LOG_SUPPRESSED hid 545 stuck and quest-blocked rows, so those counts are lower bounds. Earlier finding: mob position and aggro cause are also not logged.
