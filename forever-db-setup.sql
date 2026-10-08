-- Creates the databases and user. Replace the password placeholder and host before running:
--   mysql -h<db-host> -uroot -p < forever-db-setup.sql
-- The host in the user definitions below is the address of the machine running the worldserver (<client-host>).
-- Databases use a forever_ prefix so they cannot collide with other auth/characters/world/log databases on the same server.

CREATE DATABASE IF NOT EXISTS forever_auth       DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE DATABASE IF NOT EXISTS forever_characters DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE DATABASE IF NOT EXISTS forever_world      DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE DATABASE IF NOT EXISTS forever_hotfixes   DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

CREATE USER IF NOT EXISTS 'forever'@'<client-host>' IDENTIFIED BY 'CHANGE_ME';

-- TrinityCore's auto-updater creates tables/applies SQL updates, so it needs full rights on these 4 DBs only.
GRANT ALL PRIVILEGES ON forever_auth.*       TO 'forever'@'<client-host>';
GRANT ALL PRIVILEGES ON forever_characters.* TO 'forever'@'<client-host>';
GRANT ALL PRIVILEGES ON forever_world.*      TO 'forever'@'<client-host>';
GRANT ALL PRIVILEGES ON forever_hotfixes.*   TO 'forever'@'<client-host>';
FLUSH PRIVILEGES;
