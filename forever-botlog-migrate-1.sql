-- Upgrade of an EXISTING forever_botlog (created from the older forever-botlog-setup.sql) to the current schema. Idempotent: safe to
-- run twice. Run as a user with ALTER/CREATE/DROP on forever_botlog
-- (the bot log user has ALL PRIVILEGES on it), e.g.:
--   mysql -h<db-host> -u<botlog-user> -p < forever-botlog-migrate-1.sql
-- Stop the worldserver (or accept brief metadata locks) first: every ALTER below rebuilds the partitioned bot_event table.
-- Takes a few seconds per 100k rows. The worldserver detects the new columns/tables when it opens the pool (restart it afterwards).
--
-- What it adds: bot_event.session_seq, STORED generated columns killer_entry/killer_level/xp_amount/spell_id and VIRTUAL outcome/lvl_diff,
-- indexes idx_type_sev_ts/idx_session/idx_killer/idx_spell/idx_xp (idx_reason and idx_quest are KEPT: the console filters by reason and
-- analysis queries group by reason/quest_id), bot_event_hot + view bot_event_all, the summary tables, and the new roll procedures/event.
-- Tested on a local test database with the USE line replaced.

USE forever_botlog;

DROP PROCEDURE IF EXISTS botlog_mig_col;
DROP PROCEDURE IF EXISTS botlog_mig_idx;
DELIMITER //
CREATE PROCEDURE botlog_mig_col(IN p_table VARCHAR(64), IN p_col VARCHAR(64), IN p_def TEXT)
BEGIN
  IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = p_table AND COLUMN_NAME = p_col) THEN
    SET @sql = CONCAT('ALTER TABLE ', p_table, ' ADD COLUMN ', p_col, ' ', p_def);
    PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;
  END IF;
END//
CREATE PROCEDURE botlog_mig_idx(IN p_table VARCHAR(64), IN p_idx VARCHAR(64), IN p_cols VARCHAR(255))
BEGIN
  IF NOT EXISTS (SELECT 1 FROM information_schema.STATISTICS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = p_table AND INDEX_NAME = p_idx) THEN
    SET @sql = CONCAT('ALTER TABLE ', p_table, ' ADD INDEX ', p_idx, ' (', p_cols, ')');
    PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;
  END IF;
END//
DELIMITER ;

CALL botlog_mig_col('bot_event', 'session_seq', 'BIGINT UNSIGNED NULL COMMENT ''login session of the bot, set at BOT_LOGIN''');
CALL botlog_mig_col('bot_event', 'killer_entry', 'BIGINT UNSIGNED GENERATED ALWAYS AS (JSON_VALUE(details, ''$.killer.entry'' RETURNING UNSIGNED)) STORED');
CALL botlog_mig_col('bot_event', 'killer_level', 'BIGINT UNSIGNED GENERATED ALWAYS AS (JSON_VALUE(details, ''$.killer.level'' RETURNING UNSIGNED)) STORED');
CALL botlog_mig_col('bot_event', 'xp_amount', 'BIGINT UNSIGNED GENERATED ALWAYS AS (IF(event_type = ''xp'', JSON_VALUE(details, ''$.amount'' RETURNING UNSIGNED), NULL)) STORED');
CALL botlog_mig_col('bot_event', 'spell_id', 'BIGINT UNSIGNED GENERATED ALWAYS AS (COALESCE(JSON_VALUE(details, ''$.killer.spell'' RETURNING UNSIGNED), JSON_VALUE(details, ''$.spell_id'' RETURNING UNSIGNED))) STORED');
CALL botlog_mig_col('bot_event', 'outcome', 'VARCHAR(24) GENERATED ALWAYS AS (JSON_VALUE(details, ''$.outcome'')) VIRTUAL');
CALL botlog_mig_col('bot_event', 'lvl_diff', 'BIGINT GENERATED ALWAYS AS (COALESCE(JSON_VALUE(details, ''$.killer.lvl_diff'' RETURNING SIGNED), JSON_VALUE(details, ''$.target.lvl_diff'' RETURNING SIGNED))) VIRTUAL');

CALL botlog_mig_idx('bot_event', 'idx_type_sev_ts', 'event_type, severity, ts');
CALL botlog_mig_idx('bot_event', 'idx_session', 'bot_guid, session_seq');
CALL botlog_mig_idx('bot_event', 'idx_killer', 'killer_entry, killer_level');
CALL botlog_mig_idx('bot_event', 'idx_spell', 'spell_id');
CALL botlog_mig_idx('bot_event', 'idx_xp', 'xp_amount');

DROP PROCEDURE botlog_mig_col;
DROP PROCEDURE botlog_mig_idx;

-- Hot table, view and summary tables.
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

-- Switch the daily event to the two-argument procedure (archive 14 days, hot 5 days).
CREATE EVENT IF NOT EXISTS botlog_daily_roll
  ON SCHEDULE EVERY 1 DAY STARTS (CURRENT_DATE + INTERVAL 1 DAY + INTERVAL 5 MINUTE)
  DO CALL botlog_roll_partitions3(14, 5, 2);
ALTER EVENT botlog_daily_roll DO CALL botlog_roll_partitions3(14, 5, 2);

CALL botlog_roll_partitions3(14, 5, 2);
