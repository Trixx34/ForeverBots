-- Bot logging database for the Forever bots.
-- NOT yet run anywhere. Run as root on the NAS (after confirming):
--   docker exec -i MySQL mysql -uroot -p < forever-botlog-setup.sql
-- Replace CHANGE_ME first. Host 192.168.34.32 = the Forever VM.

CREATE DATABASE IF NOT EXISTS forever_botlog DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

CREATE USER IF NOT EXISTS 'foreverbot'@'192.168.34.32' IDENTIFIED BY 'CHANGE_ME';
GRANT ALL PRIVILEGES ON forever_botlog.* TO 'foreverbot'@'192.168.34.32';
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
--   details: free-form JSON (alternatives considered, killer, damage log, quest step, ...)
-- Partitioned by day so old data is dropped instantly (see botlog_roll_partitions below).
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
  PRIMARY KEY (id, ts),
  KEY idx_bot_ts   (bot_guid, ts),
  KEY idx_type_ts  (event_type, ts),
  KEY idx_reason   (reason, ts),
  KEY idx_quest    (quest_id, ts)
) ENGINE=InnoDB
PARTITION BY RANGE (TO_DAYS(ts)) (
  PARTITION pmax VALUES LESS THAN MAXVALUE
);

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

-- Adds daily partitions for today..today+3 of one table of the current database and drops the ones older than p_keep_days.
DROP PROCEDURE IF EXISTS botlog_roll_table;
DELIMITER //
CREATE PROCEDURE botlog_roll_table(IN p_table VARCHAR(64), IN p_keep_days INT)
BEGIN
  DECLARE d INT DEFAULT 0;
  DECLARE pname VARCHAR(16);
  DECLARE done INT DEFAULT 0;
  DECLARE old_name VARCHAR(64);
  DECLARE cur CURSOR FOR
    SELECT PARTITION_NAME FROM information_schema.PARTITIONS
    WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = p_table
      AND PARTITION_NAME LIKE 'p\_2%'
      AND PARTITION_NAME < CONCAT('p_', DATE_FORMAT(CURDATE() - INTERVAL p_keep_days DAY, '%Y%m%d'));
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

  OPEN cur;
  drop_loop: LOOP
    FETCH cur INTO old_name;
    IF done THEN LEAVE drop_loop; END IF;
    SET @sql = CONCAT('ALTER TABLE ', p_table, ' DROP PARTITION ', old_name);
    PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;
  END LOOP;
  CLOSE cur;
END//

-- Rolls bot_event (p_keep_days) and bot_pos (2 days).
DROP PROCEDURE IF EXISTS botlog_roll_partitions//
CREATE PROCEDURE botlog_roll_partitions(IN p_keep_days INT)
BEGIN
  CALL botlog_roll_table('bot_event', p_keep_days);
  CALL botlog_roll_table('bot_pos', 2);
END//
DELIMITER ;

CALL botlog_roll_partitions(14);

-- Daily rollover. Requires event_scheduler=ON on the server (default ON in MySQL 8).
-- If it is OFF, run "CALL botlog_roll_partitions(14);" yourself daily, or enable it.
CREATE EVENT IF NOT EXISTS botlog_daily_roll
  ON SCHEDULE EVERY 1 DAY STARTS (CURRENT_DATE + INTERVAL 1 DAY + INTERVAL 5 MINUTE)
  DO CALL botlog_roll_partitions(14);
