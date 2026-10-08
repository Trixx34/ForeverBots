# Logging gaps (analysis-2026-10-07-run2 section 7), 2026-10-08

Status: code written, NOT compiled and NOT sim-verified.

Already present on `forever` (no change): decoded cast results 32 (DONT_REPORT) and 172 (PREVENTED_BY_MECHANIC), CAST_OK and AURA rows, `mob_pos` and `pack_20yd` on COMBAT_START, per-spell `spell_stats`.

Added:
- COMBAT_SUMMARY: `fight_id` (joins to COMBAT_START/END), `outcome` (bot_died, fled, all_targets_dead, partial_kills, no_kill, no_target) and `casts_ok` (total successful casts; the existing `casts` object is the per-spell split).
- COMBAT_START: `aggro_cause` (bot_pull, mob_aggro, mob_outside_radius, mob_not_on_bot, unknown), `aggro_range`, `pull_dist`.
- AI_TICK_STATS: `no_action` per engine, counting ticks that ran no action by reason (no_trigger, multiplied_to_zero, not_possible, not_useful, execute_failed). This explains idle time at the engine level; it does not say what a bot was waiting for inside an action.
- LOG_SUPPRESSED: `by_reason` (count per event code) and `login_total` (suppressed rows this session).

Config: `Bot.Log.PullDetail` and `Bot.Log.IdleReasons` (both default 1). Volume: no new rows; a few fields on existing rows.

To verify in the sim: build; check the new fields parse as JSON in bot_event; `aggro_cause` distribution looks sane; `no_action` totals roughly match the 14 to 17% unexplained time.
