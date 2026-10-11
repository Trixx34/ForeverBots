-- Classic 1.60: profession certifications bought on an account (spell script classic_spell_profession_certification): every character
-- of the account shows the profession title once it has 300 skill (Player::UpdateClassicProfessionTitles). Safe to run again.
CREATE TABLE IF NOT EXISTS `account_classic_certification` (
  `account` int unsigned NOT NULL COMMENT 'account.id',
  `skill` smallint unsigned NOT NULL COMMENT 'SkillLine of the profession',
  PRIMARY KEY (`account`,`skill`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='profession certifications of the account';
