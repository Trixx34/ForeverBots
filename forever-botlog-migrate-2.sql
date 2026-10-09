-- Adds the summary table of the bot log (Bot.Log.Summary.* in botserver.conf). Idempotent: safe to run twice, also on a fresh
-- install made from forever-botlog-setup.sql (which already contains it). Run as the bot log user, e.g.:
--   mysql -h<db-host> -u<botlog-user> -p < forever-botlog-migrate-2.sql
-- The worldserver detects the table when it opens the pool: restart it afterwards, then set Bot.Log.Summary.Enabled = 1.
--
-- bot_event_rollup: one row per window (Bot.Log.Summary.WindowSec) and bot, event type, reason, severity, summary key (the spell text
-- for cast/aura), map, zone, quest and target, with the number of events (n), the first and last time, the level of the last event
-- and the summary/details of the first event as a sample. Replaces thousands of bot_event rows with one; nothing needed to count
-- events, group them by bot/zone/quest/reason or build time series is lost.
-- bot_event_counts_all: rollup rows and detailed rows as one list of (ts, bot, type, reason, severity, n), so a count query does
-- not care which events were summarized:  SELECT reason, SUM(n) FROM bot_event_counts_all WHERE event_type = 'cast' GROUP BY reason;

USE forever_botlog;

CREATE TABLE IF NOT EXISTS bot_event_rollup (
  window_ts      DATETIME NOT NULL COMMENT 'start of the window',
  bot_guid       BIGINT UNSIGNED NOT NULL,
  event_type     VARCHAR(32) NOT NULL,
  reason         VARCHAR(64) NOT NULL DEFAULT '',
  severity       TINYINT UNSIGNED NOT NULL,
  summary_key    VARCHAR(64) NOT NULL DEFAULT '' COMMENT 'summary text for the types in Bot.Log.Summary.KeyBySummary (cast, aura: the spell)',
  map_id         SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  zone_id        SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  quest_id       INT UNSIGNED NOT NULL DEFAULT 0,
  target_entry   INT UNSIGNED NOT NULL DEFAULT 0,
  n              INT UNSIGNED NOT NULL,
  first_ts       DATETIME(3) NOT NULL,
  last_ts        DATETIME(3) NOT NULL,
  level          TINYINT UNSIGNED NULL COMMENT 'of the last event',
  sample_summary VARCHAR(255) NULL COMMENT 'of the first event',
  sample_details JSON NULL COMMENT 'of the first event',
  session_seq    BIGINT UNSIGNED NULL COMMENT 'of the last event',
  PRIMARY KEY (window_ts, bot_guid, event_type, reason, severity, summary_key, map_id, zone_id, quest_id, target_entry),
  KEY idx_type_reason_window (event_type, reason, window_ts),
  KEY idx_bot_window (bot_guid, window_ts)
) ENGINE=InnoDB;

CREATE OR REPLACE VIEW bot_event_counts_all AS
  SELECT window_ts AS ts, bot_guid, event_type, reason, severity, map_id, zone_id, quest_id, target_entry, n FROM bot_event_rollup
  UNION ALL
  SELECT ts, bot_guid, event_type, reason, severity, map_id, zone_id, quest_id, target_entry, 1 AS n FROM bot_event_all;

-- Retention: the rollup is small (one row per key and window), kept 30 days. Requires event_scheduler=ON like botlog_daily_roll.
DROP PROCEDURE IF EXISTS botlog_rollup_prune;
DELIMITER //
CREATE PROCEDURE botlog_rollup_prune(IN p_days INT)
BEGIN
  DECLARE v_rows INT DEFAULT 1;
  WHILE v_rows > 0 DO
    DELETE FROM bot_event_rollup WHERE window_ts < NOW() - INTERVAL GREATEST(1, p_days) DAY LIMIT 50000;
    SET v_rows = ROW_COUNT();
  END WHILE;
END//
DELIMITER ;

CREATE EVENT IF NOT EXISTS botlog_rollup_prune_daily
  ON SCHEDULE EVERY 1 DAY STARTS (CURRENT_DATE + INTERVAL 1 DAY + INTERVAL 15 MINUTE)
  DO CALL botlog_rollup_prune(30);
