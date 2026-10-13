-- Module store (doc/module-system/15-module-store.md): key/value rows private to each module.
-- Modules never run SQL; the core reads and writes these rows on a module's behalf, scoped to its own module name.
CREATE TABLE IF NOT EXISTS `module_store` (
  `module` VARCHAR(64) NOT NULL,
  `key` VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `value` VARBINARY(1024) NOT NULL,
  PRIMARY KEY (`module`, `key`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
