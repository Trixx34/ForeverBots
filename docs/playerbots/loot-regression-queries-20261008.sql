-- Chest loot regression checks on bot_event (R5: deaths 459 -> 960, stuck 657 -> 888, path failures 250 -> 399).
-- Run against the sim run after the Bot.AI.Loot.Safety.* change and compare with R5 (same 60 minute ramp window).
-- Uses the view bot_event_all (bot_event + bot_event_hot) because decision rows are written to the hot table.
-- Set @t0 / @t1 to the start and end of the window. All thresholds below are first guesses.
-- Row kinds used: event_type 'decision' with reason QUEST_LOOT_GO / QUEST_LOOT_OBJECT / QUEST_LOOT_SPAWN_EMPTY / QUEST_LOOT_DANGER /
-- QUEST_LOOT_SPAWN_QUARANTINED; event_type 'death' (reason DIED); event_type 'stuck' and 'path_fail'.
-- Not checked against a live schema here: the column names come from forever-botlog-setup.sql.

SET @t0 = '2026-10-08 00:00:00', @t1 = '2026-10-08 01:00:00';
SET @near_s = 90, @near_yd = 60;

-- Q1. Window totals: the three regression numbers plus loot activity.
SELECT
  SUM(event_type = 'death')                          AS deaths,
  SUM(event_type = 'stuck')                          AS stuck,
  SUM(event_type = 'path_fail')                      AS path_fail,
  SUM(reason = 'QUEST_LOOT_GO')                      AS loot_go,
  SUM(reason = 'QUEST_LOOT_OBJECT')                  AS loot_done,
  SUM(reason = 'QUEST_LOOT_SPAWN_EMPTY')             AS loot_empty,
  SUM(reason = 'QUEST_LOOT_DANGER')                  AS loot_danger,
  SUM(reason = 'QUEST_LOOT_SPAWN_QUARANTINED')       AS loot_quarantined
FROM bot_event_all WHERE ts >= @t0 AND ts < @t1;

-- Q2. Deaths that follow a loot trip: a death within @near_s seconds after a QUEST_LOOT_GO of the same bot and session,
-- split by the distance between the death and the last chest target (target position is not logged: the distance is to the bot's
-- position at QUEST_LOOT_GO, which is where the trip started; a death far from it means the bot died on the way).
SELECT d.zone_id, COUNT(*) AS deaths_after_loot_go, COUNT(DISTINCT d.bot_guid) AS bots,
       ROUND(AVG(TIMESTAMPDIFF(SECOND, g.ts, d.ts))) AS avg_s_after_go,
       ROUND(AVG(SQRT(POW(d.pos_x - g.pos_x, 2) + POW(d.pos_y - g.pos_y, 2)))) AS avg_yd_from_trip_start,
       d.killer_entry
FROM bot_event_all d
JOIN bot_event_all g
  ON g.bot_guid = d.bot_guid AND g.session_seq <=> d.session_seq AND g.reason = 'QUEST_LOOT_GO'
 AND g.ts <= d.ts AND g.ts >= d.ts - INTERVAL @near_s SECOND
WHERE d.event_type = 'death' AND d.ts >= @t0 AND d.ts < @t1
GROUP BY d.zone_id, d.killer_entry ORDER BY deaths_after_loot_go DESC LIMIT 40;
-- Same, but a death counts only when no QUEST_LOOT_OBJECT / QUEST_LOOT_SPAWN_EMPTY row of the bot came between the go and the death
-- (the bot was still walking to or standing at the chest).
SELECT COUNT(*) AS deaths_still_on_trip
FROM bot_event_all d
JOIN bot_event_all g
  ON g.bot_guid = d.bot_guid AND g.session_seq <=> d.session_seq AND g.reason = 'QUEST_LOOT_GO'
 AND g.ts <= d.ts AND g.ts >= d.ts - INTERVAL @near_s SECOND
WHERE d.event_type = 'death' AND d.ts >= @t0 AND d.ts < @t1
  AND NOT EXISTS (SELECT 1 FROM bot_event_all x WHERE x.bot_guid = d.bot_guid AND x.session_seq <=> d.session_seq
                  AND x.reason IN ('QUEST_LOOT_OBJECT', 'QUEST_LOOT_SPAWN_EMPTY', 'QUEST_LOOT_DANGER') AND x.ts > g.ts AND x.ts < d.ts);

-- Q3. Deaths per chest area: deaths within @near_yd yards of the position of a loot object event, same map, any time in the window.
-- High counts at a few chests point at guarded chests. Rows are clustered by the chest position rounded to 20 yd.
SELECT o.map_id, ROUND(o.pos_x / 20) * 20 AS cx, ROUND(o.pos_y / 20) * 20 AS cy,
       COUNT(DISTINCT o.id) AS loot_events, COUNT(DISTINCT d.id) AS deaths_nearby
FROM bot_event_all o
LEFT JOIN bot_event_all d
  ON d.event_type = 'death' AND d.map_id = o.map_id AND d.ts >= @t0 AND d.ts < @t1
 AND SQRT(POW(d.pos_x - o.pos_x, 2) + POW(d.pos_y - o.pos_y, 2)) <= @near_yd
WHERE o.reason IN ('QUEST_LOOT_OBJECT', 'QUEST_LOOT_DANGER') AND o.ts >= @t0 AND o.ts < @t1
GROUP BY o.map_id, cx, cy HAVING deaths_nearby > 0 ORDER BY deaths_nearby DESC LIMIT 30;

-- Q4. Does the danger check fire, and where? Level of the bots when it fires and how often a task ends in LOOT_DANGER.
SELECT quest_id, level, COUNT(*) AS danger_rows, COUNT(DISTINCT bot_guid) AS bots
FROM bot_event_all WHERE reason = 'QUEST_LOOT_DANGER' AND ts >= @t0 AND ts < @t1
GROUP BY quest_id, level ORDER BY danger_rows DESC LIMIT 40;
SELECT quest_id, COUNT(*) AS parked, COUNT(DISTINCT bot_guid) AS bots
FROM bot_event_all WHERE reason = 'LOOT_DANGER' AND ts >= @t0 AND ts < @t1 GROUP BY quest_id ORDER BY parked DESC;

-- Q5. Retry load per chest spawn: how many bots keep failing on the same spawn (the summary carries "spawn <id>").
SELECT quest_id, CAST(REGEXP_SUBSTR(summary, 'spawn [0-9]+') AS CHAR) AS spawn, COUNT(*) AS empty_rows, COUNT(DISTINCT bot_guid) AS bots,
       SUBSTRING_INDEX(SUBSTRING_INDEX(summary, '(', -1), ')', 1) AS cause
FROM bot_event_all WHERE reason = 'QUEST_LOOT_SPAWN_EMPTY' AND ts >= @t0 AND ts < @t1
GROUP BY quest_id, spawn, cause ORDER BY empty_rows DESC LIMIT 40;
-- Quarantined spawns and how often each came back after the quarantine (rows with the same spawn id after the first quarantine row).
SELECT CAST(REGEXP_SUBSTR(summary, 'spawn [0-9]+') AS CHAR) AS spawn, quest_id, COUNT(*) AS quarantines, MIN(ts) AS first_ts,
       JSON_VALUE(MAX(details), '$.reason') AS last_reason
FROM bot_event_all WHERE reason = 'QUEST_LOOT_SPAWN_QUARANTINED' AND ts >= @t0 AND ts < @t1 GROUP BY spawn, quest_id ORDER BY quarantines DESC;
-- Tasks per bot and quest that ended in a drop or back-off (retry storms): more than 5 in the window is a storm.
SELECT bot_guid, quest_id, reason, COUNT(*) AS n
FROM bot_event_all
WHERE reason IN ('NO_PATH', 'PATH_PARTIAL_FAR', 'UNREACHABLE', 'ITEM_NOT_DROPPING', 'GO_BUSY', 'LOOT_NO_USABLE_SPAWN', 'LOOT_DANGER', 'NO_TARGET_SPAWN')
  AND quest_id IN (SELECT DISTINCT quest_id FROM bot_event_all WHERE reason = 'QUEST_LOOT_GO' AND ts >= @t0 AND ts < @t1)
  AND ts >= @t0 AND ts < @t1
GROUP BY bot_guid, quest_id, reason HAVING n > 5 ORDER BY n DESC LIMIT 40;

-- Q6. Stuck and path failures on loot quests versus all quests (the quest_id of motion rows is the quest context of the goal).
SELECT event_type, reason,
       SUM(quest_id IN (SELECT DISTINCT quest_id FROM bot_event_all WHERE reason = 'QUEST_LOOT_GO' AND ts >= @t0 AND ts < @t1)) AS on_loot_quests,
       COUNT(*) AS all_rows, COUNT(DISTINCT bot_guid) AS bots
FROM bot_event_all WHERE event_type IN ('stuck', 'path_fail') AND ts >= @t0 AND ts < @t1
GROUP BY event_type, reason ORDER BY all_rows DESC;
-- Stuck / path-fail rows within 120 s after a loot go of the same bot (the walk to the chest), by reason and zone.
SELECT s.event_type, s.reason, s.zone_id, COUNT(*) AS n, COUNT(DISTINCT s.bot_guid) AS bots
FROM bot_event_all s
JOIN bot_event_all g ON g.bot_guid = s.bot_guid AND g.session_seq <=> s.session_seq AND g.reason = 'QUEST_LOOT_GO'
 AND g.ts <= s.ts AND g.ts >= s.ts - INTERVAL 120 SECOND
WHERE s.event_type IN ('stuck', 'path_fail') AND s.ts >= @t0 AND s.ts < @t1
GROUP BY s.event_type, s.reason, s.zone_id ORDER BY n DESC LIMIT 40;

-- Q7. Error spam: severity 2/3 rows per reason, top 20, to compare the console-line rate and the log rows between runs.
SELECT event_type, reason, severity, COUNT(*) AS n, COUNT(DISTINCT bot_guid) AS bots
FROM bot_event_all WHERE severity >= 2 AND ts >= @t0 AND ts < @t1
GROUP BY event_type, reason, severity ORDER BY n DESC LIMIT 20;

-- Q8. Outcome check: did the guards cost progress? Loot done and rewards of the loot quests, with and without the change.
SELECT quest_id,
       SUM(reason = 'QUEST_LOOT_OBJECT') AS looted,
       SUM(reason = 'QUEST_ACCEPTED')    AS accepted,
       SUM(reason = 'QUEST_REWARDED')    AS rewarded
FROM bot_event_all
WHERE ts >= @t0 AND ts < @t1
  AND quest_id IN (SELECT DISTINCT quest_id FROM bot_event_all WHERE reason = 'QUEST_LOOT_GO' AND ts >= @t0 AND ts < @t1)
GROUP BY quest_id ORDER BY looted DESC;
-- Expected after the change if the guards help: Q2 and Q3 counts fall, Q5 empty_rows per spawn fall, Q6 loot-quest stuck/path_fail fall,
-- Q8 looted/rewarded stay within noise of R5 (3521, 93552, 753, 3904, 3902, 3361).
