-- Classic 1.60: retail TrinityCore SpellItemEnchantment rows (build 69497). They have no hotfix_data rows, so the client never saw
-- them, but the server loaded them over the Classic data for 21 vanilla enchants: sharpening stones, weightstones and scopes lost
-- their damage (EffectPointsMin 0, 60 min instead of 30), Elemental Sharpening Stone 2506 and Biznicks scope 2523 became one point
-- of rating instead of 2% crit / 3% hit, Counterweight 34 haste rating, Shadow Oil 25 and Frost Oil 26 retail or missing spells.
-- The other 17 rows are retail-only enchants.
DELETE FROM `spell_item_enchantment_locale` WHERE `ID` IN (SELECT `ID` FROM `spell_item_enchantment` WHERE `VerifiedBuild` = 69497) AND `VerifiedBuild` = 69497;
DELETE FROM `spell_item_enchantment` WHERE `VerifiedBuild` = 69497;
