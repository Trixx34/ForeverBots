-- Player alts as bots (step A3): `.bot alt add|remove|list` is available to every player (the command itself only accepts
-- characters of the issuer's own account). Players have secId 0 in rbac_default_permissions; revoke row (0, 1001) to turn it off.
DELETE FROM `rbac_permissions` WHERE `id` = 1001;
INSERT INTO `rbac_permissions` (`id`, `name`) VALUES
(1001, 'COMMAND_BOT_ALT');

DELETE FROM `rbac_default_permissions` WHERE `permissionId` = 1001;
INSERT INTO `rbac_default_permissions` (`secId`, `permissionId`, `realmId`) VALUES
(0, 1001, -1),
(1, 1001, -1),
(2, 1001, -1),
(3, 1001, -1);
