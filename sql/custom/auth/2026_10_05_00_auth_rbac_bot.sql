-- Player-bot subsystem: dedicated RBAC permission for `.bot` commands (see docs/playerbots/).
-- Granted by default to Gamemaster (2) and Administrator (3); grant/revoke per-account as usual
-- via `.account set` / the rbac account commands for finer control.

DELETE FROM `rbac_permissions` WHERE `id` = 1000;
INSERT INTO `rbac_permissions` (`id`, `name`) VALUES
(1000, 'COMMAND_BOT');

DELETE FROM `rbac_default_permissions` WHERE `permissionId` = 1000;
INSERT INTO `rbac_default_permissions` (`secId`, `permissionId`, `realmId`) VALUES
(2, 1000, -1),
(3, 1000, -1);
