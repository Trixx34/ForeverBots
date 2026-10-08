-- Classic 1.60.1.70009: retail spell rows keyed by (SpellID, DifficultyID) duplicate the Classic clients own records
-- (client ASSERT "Duplicate difficulty records detected", e.g. spell 774 SpellEffect) -> never send / load them
DELETE FROM `hotfix_data` WHERE `TableHash` IN (0xF42FC065,0xBA978F4E,0xDBE7F829,0xF9F37C57,0xF04238A5,0x668FAE03,0x1DDEC5E6,0xC603EE28,0xE064A75C,0x27B7A01A);
DELETE FROM `spell_aura_options`;
DELETE FROM `spell_aura_restrictions`;
DELETE FROM `spell_categories`;
DELETE FROM `spell_cooldowns`;
DELETE FROM `spell_effect`;
DELETE FROM `spell_interrupts`;
DELETE FROM `spell_levels`;
DELETE FROM `spell_misc`;
DELETE FROM `spell_target_restrictions`;
DELETE FROM `spell_x_spell_visual`;
