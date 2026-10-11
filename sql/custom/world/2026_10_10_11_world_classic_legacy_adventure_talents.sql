-- Classic 1.60: Legacy perks (trait trees 1187 Professions, 1188 Adventure, 1189 Progression) that need a spell script.
-- Reagent Economy 1225503: its aura 215 must not put the player in arena preparation (the reagent rule is in Player::CanNoReagentCast).
-- Dedicated Study 1225469: +1 to the lowest tradeskill, or Elemental Essences once every tradeskill is at 300.
-- Recently Bandaged 11196: shortened by Field Medicine 1225475.
-- High Alert 1225477: no stealth detection bonus in battlegrounds (recalculated on map change by classic_player_high_alert_map).
-- Thrill of Adventure 1225472 is a PlayerScript (classic_player_thrill_of_adventure); the other perks are handled in the core.
DELETE FROM `spell_script_names` WHERE `ScriptName` IN ('classic_spell_legacy_reagent_economy','classic_spell_legacy_dedicated_study','classic_spell_recently_bandaged_field_medicine','classic_spell_legacy_high_alert');
INSERT INTO `spell_script_names` (`spell_id`,`ScriptName`) VALUES
(1225503, 'classic_spell_legacy_reagent_economy'),
(1225469, 'classic_spell_legacy_dedicated_study'),
(11196, 'classic_spell_recently_bandaged_field_medicine'),
(1225477, 'classic_spell_legacy_high_alert');
