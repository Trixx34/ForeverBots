-- Bot logging database for the Forever bots.
-- Creates the bot log database and user. Replace the password placeholder and host before running:
--   mysql -h<db-host> -uroot -p < forever-botlog-setup.sql
-- The host in the user definitions below is the address of the machine running the worldserver (<client-host>).

CREATE DATABASE IF NOT EXISTS forever_botlog DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

CREATE USER IF NOT EXISTS 'forever_bot'@'<client-host>' IDENTIFIED BY 'CHANGE_ME';
GRANT ALL PRIVILEGES ON forever_botlog.* TO 'forever_bot'@'<client-host>';
FLUSH PRIVILEGES;

USE forever_botlog;

-- One row per bot, so events only carry the guid.
CREATE TABLE IF NOT EXISTS bot (
  guid        BIGINT UNSIGNED NOT NULL,
  name        VARCHAR(24)  NOT NULL,
  class_id    TINYINT UNSIGNED NOT NULL,
  race_id     TINYINT UNSIGNED NOT NULL,
  faction     ENUM('alliance','horde') NOT NULL,
  first_seen  DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  PRIMARY KEY (guid),
  KEY idx_class_faction (class_id, faction)
) ENGINE=InnoDB;

-- Everything a bot does or fails to do. Fixed columns for filtering, JSON for the rest.
--   event_type examples: decision, state_change, death, quest_blocked, quest_done, stuck, path_fail, combat, error
--   reason:  short machine-readable code (e.g. NO_PATH, QUEST_PREREQ, TARGET_ELITE) so it can be grouped
--   Full reason-code registry (quest_blocked, decision, path_fail, stuck): docs/playerbots/quest-design.md section 6.2 / 6.2.1 (implemented) and 6.2.2 (reserved)
--   and docs/playerbots/engine-design.md. path_fail also has PATH_PARTIAL_FAR (partial path, goal far from a walkable poly).
--   details: free-form JSON (alternatives considered, killer, damage log, quest step, ...)
-- Partitioned by day so old data is dropped instantly (see botlog_roll_partitions2 below).
-- Final bot_event definition (fresh installs). session_seq: one id per bot login (BOT_LOGIN), unique per (bot_guid, session_seq).
-- The STORED generated columns pull hot JSON fields out of details so they can be indexed (killer, xp, spell); outcome and lvl_diff are VIRTUAL.
CREATE TABLE IF NOT EXISTS bot_event (
  id          BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  ts          DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  bot_guid    BIGINT UNSIGNED NOT NULL,
  event_type  VARCHAR(32)  NOT NULL,
  severity    TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT '0 trace, 1 info, 2 warn, 3 error',
  reason      VARCHAR(64)  NULL,
  summary     VARCHAR(255) NULL,
  level       TINYINT UNSIGNED NULL,
  map_id      SMALLINT UNSIGNED NULL,
  zone_id     SMALLINT UNSIGNED NULL,
  pos_x       FLOAT NULL,
  pos_y       FLOAT NULL,
  pos_z       FLOAT NULL,
  quest_id    INT UNSIGNED NULL,
  target_entry INT UNSIGNED NULL,
  details     JSON NULL,
  session_seq BIGINT UNSIGNED NULL COMMENT 'login session of the bot, set at BOT_LOGIN',
  killer_entry BIGINT UNSIGNED GENERATED ALWAYS AS (JSON_VALUE(details, '$.killer.entry' RETURNING UNSIGNED)) STORED,
  killer_level BIGINT UNSIGNED GENERATED ALWAYS AS (JSON_VALUE(details, '$.killer.level' RETURNING UNSIGNED)) STORED,
  xp_amount    BIGINT UNSIGNED GENERATED ALWAYS AS (IF(event_type = 'xp', JSON_VALUE(details, '$.amount' RETURNING UNSIGNED), NULL)) STORED,
  spell_id     BIGINT UNSIGNED GENERATED ALWAYS AS (COALESCE(JSON_VALUE(details, '$.killer.spell' RETURNING UNSIGNED), JSON_VALUE(details, '$.spell_id' RETURNING UNSIGNED))) STORED,
  outcome      VARCHAR(24) GENERATED ALWAYS AS (JSON_VALUE(details, '$.outcome')) VIRTUAL,
  lvl_diff     BIGINT GENERATED ALWAYS AS (COALESCE(JSON_VALUE(details, '$.killer.lvl_diff' RETURNING SIGNED), JSON_VALUE(details, '$.target.lvl_diff' RETURNING SIGNED))) VIRTUAL,
  PRIMARY KEY (id, ts),
  KEY idx_bot_ts   (bot_guid, ts),
  KEY idx_type_ts  (event_type, ts),
  KEY idx_type_sev_ts (event_type, severity, ts),
  KEY idx_reason   (reason, ts),
  KEY idx_quest    (quest_id, ts),
  KEY idx_session  (bot_guid, session_seq),
  KEY idx_killer   (killer_entry, killer_level),
  KEY idx_spell    (spell_id),
  KEY idx_xp       (xp_amount)
) ENGINE=InnoDB
PARTITION BY RANGE (TO_DAYS(ts)) (
  PARTITION pmax VALUES LESS THAN MAXVALUE
);

-- Hot rows: decision, state_change and trace events (Bot.Log.HotTypes), written here instead of bot_event when Bot.Log.HotSplit=1.
-- Short retention (botlog_roll_partitions2 p_hot_days, 3-7 days, default 5). Same columns as bot_event; ids start at 10^12 so that
-- bot_event_all has unique ids. COMBAT_SUMMARY and LOG_SUPPRESSED stay in bot_event (low volume, needed for the 14-day analysis).
CREATE TABLE IF NOT EXISTS bot_event_hot LIKE bot_event;
ALTER TABLE bot_event_hot AUTO_INCREMENT = 1000000000000;

-- Both tables as one: use this for queries that must see every event. src: 0 = bot_event, 1 = bot_event_hot.
CREATE OR REPLACE VIEW bot_event_all AS
  SELECT 0 AS src, id, ts, bot_guid, event_type, severity, reason, summary, level, map_id, zone_id, pos_x, pos_y, pos_z, quest_id, target_entry,
         details, session_seq, killer_entry, killer_level, xp_amount, spell_id, outcome, lvl_diff FROM bot_event
  UNION ALL
  SELECT 1 AS src, id, ts, bot_guid, event_type, severity, reason, summary, level, map_id, zone_id, pos_x, pos_y, pos_z, quest_id, target_entry,
         details, session_seq, killer_entry, killer_level, xp_amount, spell_id, outcome, lvl_diff FROM bot_event_hot;

-- Summaries kept after the partitions drop (filled by botlog_summarize, called from botlog_roll_table).
CREATE TABLE IF NOT EXISTS bot_event_hourly (
  hour_ts    DATETIME NOT NULL,
  src        TINYINT UNSIGNED NOT NULL COMMENT '0 bot_event, 1 bot_event_hot',
  bot_guid   BIGINT UNSIGNED NOT NULL,
  event_type VARCHAR(32) NOT NULL,
  severity   TINYINT UNSIGNED NOT NULL,
  n          INT UNSIGNED NOT NULL,
  PRIMARY KEY (hour_ts, src, bot_guid, event_type, severity),
  KEY idx_type_hour (event_type, hour_ts)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS bot_death_daily (
  day          DATE NOT NULL,
  class_id     TINYINT UNSIGNED NOT NULL,
  level        TINYINT UNSIGNED NOT NULL,
  zone_id      SMALLINT UNSIGNED NOT NULL,
  killer_entry INT UNSIGNED NOT NULL COMMENT '0 = unknown',
  n            INT UNSIGNED NOT NULL,
  PRIMARY KEY (day, class_id, level, zone_id, killer_entry)
) ENGINE=InnoDB;

-- Bot position samples for the sim web console map (movement trails). One row per bot every Bot.Log.PosIntervalSec seconds
-- while it moves, plus one on login and on map/zone change. Short retention (2 days), partitioned by day like bot_event.
--   flags bit 0 = moving, bit 1 = in combat, bit 2 = dead
CREATE TABLE IF NOT EXISTS bot_pos (
  ts        DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  bot_guid  BIGINT UNSIGNED NOT NULL,
  map_id    SMALLINT UNSIGNED NOT NULL,
  zone_id   SMALLINT UNSIGNED NOT NULL,
  x         FLOAT NOT NULL,
  y         FLOAT NOT NULL,
  z         FLOAT NOT NULL,
  flags     TINYINT UNSIGNED NOT NULL DEFAULT 0,
  KEY idx_map_ts (map_id, ts),
  KEY idx_bot_ts (bot_guid, ts)
) ENGINE=InnoDB
PARTITION BY RANGE (TO_DAYS(ts)) (
  PARTITION pmax VALUES LESS THAN MAXVALUE
);

-- ---------------------------------------------------------------------------------------------------------------------------
-- Daily summaries and partition rolling
-- ---------------------------------------------------------------------------------------------------------------------------

-- Summarises [p_from, p_to) (day boundaries) of bot_event (src 0) or bot_event_hot (src 1) into bot_event_hourly and, for bot_event,
-- the deaths into bot_death_daily. Idempotent: a re-run over the same whole days overwrites the counts.
DROP PROCEDURE IF EXISTS botlog_summarize;
DELIMITER //
CREATE PROCEDURE botlog_summarize(IN p_table VARCHAR(64), IN p_from DATE, IN p_to DATE)
BEGIN
  DECLARE v_src TINYINT DEFAULT IF(p_table = 'bot_event_hot', 1, 0);
  IF p_table NOT IN ('bot_event', 'bot_event_hot') THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'botlog_summarize: unknown table';
  END IF;

  SET @sql = CONCAT(
    'INSERT INTO bot_event_hourly (hour_ts, src, bot_guid, event_type, severity, n) ',
    'SELECT DATE_FORMAT(ts, ''%Y-%m-%d %H:00:00''), ', v_src, ', bot_guid, event_type, severity, COUNT(*) ',
    'FROM ', p_table, ' WHERE ts >= ? AND ts < ? ',
    'GROUP BY DATE_FORMAT(ts, ''%Y-%m-%d %H:00:00''), bot_guid, event_type, severity ',
    'ON DUPLICATE KEY UPDATE n = VALUES(n)');
  SET @f = p_from; SET @t = p_to;
  PREPARE s FROM @sql; EXECUTE s USING @f, @t; DEALLOCATE PREPARE s;

  IF p_table = 'bot_event' THEN
    INSERT INTO bot_death_daily (day, class_id, level, zone_id, killer_entry, n)
    SELECT DATE(e.ts), COALESCE(b.class_id, 0), COALESCE(e.level, 0), COALESCE(e.zone_id, 0), COALESCE(e.killer_entry, 0), COUNT(*)
    FROM bot_event e LEFT JOIN bot b ON b.guid = e.bot_guid
    WHERE e.event_type = 'death' AND e.reason = 'DIED' AND e.ts >= p_from AND e.ts < p_to
    GROUP BY DATE(e.ts), COALESCE(b.class_id, 0), COALESCE(e.level, 0), COALESCE(e.zone_id, 0), COALESCE(e.killer_entry, 0)
    ON DUPLICATE KEY UPDATE n = VALUES(n);
  END IF;
END//

-- Adds daily partitions for today..today+3 of one table of the current database and drops the ones older than p_keep_days.
-- bot_event and bot_event_hot are summarised (the 3 days before the cutoff, so a missed run is caught up) before partitions drop.
DROP PROCEDURE IF EXISTS botlog_roll_table//
CREATE PROCEDURE botlog_roll_table(IN p_table VARCHAR(64), IN p_keep_days INT)
BEGIN
  DECLARE d INT DEFAULT 0;
  DECLARE pname VARCHAR(16);
  DECLARE done INT DEFAULT 0;
  DECLARE old_name VARCHAR(64);
  DECLARE cutoff DATE DEFAULT CURDATE() - INTERVAL p_keep_days DAY;
  DECLARE cur CURSOR FOR
    SELECT PARTITION_NAME FROM information_schema.PARTITIONS
    WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = p_table
      AND PARTITION_NAME LIKE 'p\_2%'
      AND PARTITION_NAME < CONCAT('p_', DATE_FORMAT(cutoff, '%Y%m%d'));
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET done = 1;

  WHILE d <= 3 DO
    SET pname = CONCAT('p_', DATE_FORMAT(CURDATE() + INTERVAL d DAY, '%Y%m%d'));
    IF NOT EXISTS (SELECT 1 FROM information_schema.PARTITIONS
                   WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = p_table
                     AND PARTITION_NAME = pname) THEN
      SET @sql = CONCAT('ALTER TABLE ', p_table, ' REORGANIZE PARTITION pmax INTO (',
                        'PARTITION ', pname, ' VALUES LESS THAN (TO_DAYS(''',
                        DATE_FORMAT(CURDATE() + INTERVAL (d + 1) DAY, '%Y-%m-%d'), ''')), ',
                        'PARTITION pmax VALUES LESS THAN MAXVALUE)');
      PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;
    END IF;
    SET d = d + 1;
  END WHILE;

  IF p_table IN ('bot_event', 'bot_event_hot') THEN
    CALL botlog_summarize(p_table, cutoff - INTERVAL 3 DAY, cutoff);
  END IF;

  OPEN cur;
  drop_loop: LOOP
    FETCH cur INTO old_name;
    IF done THEN LEAVE drop_loop; END IF;
    SET @sql = CONCAT('ALTER TABLE ', p_table, ' DROP PARTITION ', old_name);
    PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;
  END LOOP;
  CLOSE cur;
END//

-- Rolls bot_event (p_keep_days, archive), bot_event_hot (p_hot_days, decision/state_change/trace rows) and bot_pos (p_pos_days, default 2).
-- p_pos_days is the bot_pos retention: raise it (e.g. 7) for longer map trails, at up to 720 rows per bot and hour at the 5 s interval.
DROP PROCEDURE IF EXISTS botlog_roll_partitions3//
CREATE PROCEDURE botlog_roll_partitions3(IN p_keep_days INT, IN p_hot_days INT, IN p_pos_days INT)
BEGIN
  CALL botlog_roll_table('bot_event', p_keep_days);
  IF EXISTS (SELECT 1 FROM information_schema.TABLES WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'bot_event_hot') THEN
    CALL botlog_roll_table('bot_event_hot', p_hot_days);
  END IF;
  CALL botlog_roll_table('bot_pos', GREATEST(1, p_pos_days));
END//

-- Two-argument form kept for older callers: bot_pos retention 2 days.
DROP PROCEDURE IF EXISTS botlog_roll_partitions2//
CREATE PROCEDURE botlog_roll_partitions2(IN p_keep_days INT, IN p_hot_days INT)
BEGIN
  CALL botlog_roll_partitions3(p_keep_days, p_hot_days, 2);
END//

-- Compatibility wrapper (the daily event and older notes call it with one argument): hot retention 5 days.
DROP PROCEDURE IF EXISTS botlog_roll_partitions//
CREATE PROCEDURE botlog_roll_partitions(IN p_keep_days INT)
BEGIN
  CALL botlog_roll_partitions2(p_keep_days, LEAST(5, p_keep_days));
END//
DELIMITER ;

CALL botlog_roll_partitions3(14, 5, 2);

-- Daily rollover. Requires event_scheduler=ON on the server (default ON in MySQL 8).
-- If it is OFF, run "CALL botlog_roll_partitions3(14, 5, 2);" yourself daily, or enable it.
-- Retention: bot_event 14 days (archive), bot_event_hot 5 days (3-7), bot_pos 2 days; the summaries (bot_event_hourly, bot_death_daily) are kept.
CREATE EVENT IF NOT EXISTS botlog_daily_roll
  ON SCHEDULE EVERY 1 DAY STARTS (CURRENT_DATE + INTERVAL 1 DAY + INTERVAL 5 MINUTE)
  DO CALL botlog_roll_partitions3(14, 5, 2);

-- Summary table of the bot log (Bot.Log.Summary.*), see forever-botlog-migrate-2.sql for the description.
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
