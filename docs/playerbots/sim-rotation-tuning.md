# Rotation tuning on the simulator (2026-10-08)

Script: `contrib/sim/rotation_report.py` (Python 3, standard library, needs the `mysql` client). It reads `DUMMY_SUMMARY` and `CAST_FAILED` rows from the bot log DB and prints a per-class report with `TUNE:` hints. Not run against a live DB yet; the report logic was checked on made-up rows only.

## Run

1. One time: run `sql/custom/world/2026_10_08_04_world_forever_bot_dummy.sql` on the world DB, set `Bot.AI.Dummy.Enabled = 1` in `botserver.conf`, and make sure the bot log DB is on (`forever-botlog-setup.sql`).
2. Group one bot per class at the level to test (repeat for 10, 20, 30 ...).
3. `python3 contrib/sim/rotation_report.py plan --seconds 60` prints one line per class (`warrior dummy 60`, ...). Paste them into party chat one at a time, waiting about 70 s between lines.
4. Repeat everything with `Bot.AI.Rotation.Enabled = 0` (restart), then `= 1`, so the report can compare.
5. `python3 contrib/sim/rotation_report.py report --since "2026-10-08 12:00" --user forever_bot --password ... --worldlog Server.log`. Add `--json out.json` to keep the numbers.

## What the report shows per class

Dps (mean, median, range), idle share of the run, first hit time, damage by spell with melee swings as `melee`, the top `CAST_FAILED` reasons, rotation on vs off, and the startup line "class spells validated, dropped:" (with `--worldlog`).

## Hints (`TUNE:`) and what to change

* Melee share above 40% for priest, mage or warlock: spells never fire. Read the cast failures, then fix the row in `BotCombat.cpp` SPELLS[].
* Idle above 30%: cooldown gaps or resource starvation; add a filler row or lower the mana threshold.
* First hit after 6 s: approach or opener is slow.
* One spell above 80% of damage: the other rows rarely fire (condition too strict).
* Rotation on less than 5% better than off: the table adds little.
* Dropped spells: wrong ids, fix those first.

Limits: healers are not measured; the thresholds are constants at the top of the script; the dummy mode itself has not run on a server either.
