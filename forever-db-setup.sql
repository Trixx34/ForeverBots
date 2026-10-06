-- Run as root on the NAS:
--   docker exec -i MySQL mysql -uroot -p < forever-db-setup.sql
-- Replace CHANGE_ME before running. Host 192.168.34.32 = this build machine.
-- Databases use a forever_ prefix so they cannot collide with the MoP auth/characters/world/log DBs.

CREATE DATABASE IF NOT EXISTS forever_auth       DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE DATABASE IF NOT EXISTS forever_characters DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE DATABASE IF NOT EXISTS forever_world      DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE DATABASE IF NOT EXISTS forever_hotfixes   DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

CREATE USER IF NOT EXISTS 'foreverapp'@'192.168.34.32' IDENTIFIED BY 'CHANGE_ME';

-- TrinityCore's auto-updater creates tables/applies SQL updates, so it needs full rights on these 4 DBs only.
GRANT ALL PRIVILEGES ON forever_auth.*       TO 'foreverapp'@'192.168.34.32';
GRANT ALL PRIVILEGES ON forever_characters.* TO 'foreverapp'@'192.168.34.32';
GRANT ALL PRIVILEGES ON forever_world.*      TO 'foreverapp'@'192.168.34.32';
GRANT ALL PRIVILEGES ON forever_hotfixes.*   TO 'foreverapp'@'192.168.34.32';
FLUSH PRIVILEGES;
