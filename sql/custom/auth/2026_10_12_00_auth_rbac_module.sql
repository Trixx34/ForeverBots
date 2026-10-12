-- Module system: dedicated RBAC permission for `module approve/list/info` commands (see docs/module-system/).
-- Granted by default to Administrator (3) only -- these commands approve/inspect externally-authored,
-- process-isolated code (docs/module-system/06-phase4-approval-audit-design.md), a more sensitive
-- operation than most GM commands. Grant/revoke per-account as usual via `.account set`/rbac commands.

DELETE FROM `rbac_permissions` WHERE `id` = 1001;
INSERT INTO `rbac_permissions` (`id`, `name`) VALUES
(1001, 'COMMAND_MODULE');

DELETE FROM `rbac_default_permissions` WHERE `permissionId` = 1001;
INSERT INTO `rbac_default_permissions` (`secId`, `permissionId`, `realmId`) VALUES
(3, 1001, -1);
