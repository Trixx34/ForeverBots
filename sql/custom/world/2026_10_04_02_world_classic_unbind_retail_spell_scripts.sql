-- Classic 1.60: retail spell scripts bound to class spells whose Classic version shares the spell id. Most of them fail
-- Validate() (their retail spells do not exist) and never run; these passed it and change Classic behaviour:
--   29166 Innervate            spell_dru_innervate              cast only on healer-spec players (no specs: never castable)
--   755   Health Funnel        spell_warl_health_funnel         deals % max health to the caster every tick
--   1079  Rip                  spell_dru_rip                    adds retail attack power scaling to vanilla Rip
--   5246  Intimidating Shout   spell_warr_intimidating_shout    rewrites targets of the retail effect layout
--   99    Demoralizing Roar    spell_dru_incapacitating_roar    retail Incapacitating Roar: shifts into bear form
--   5215  Prowl                spell_dru_prowl                  retail: shifts into cat form
--   586   Fade                 spell_pri_translucent_image      hooks a retail 4th effect
--   6201  Create Healthstone   spell_warl_create_healthstone    casts the retail healthstone spell
--   19740 Blessing of Might    spell_pal_blessing_of_might      retail dummy effect (Classic: aura), never matches
-- Kept: Banish, Cat Form (drops Prowl), Pick Pocket, Dash, Tame Beast, Blessing of Sacrifice, Hot Streak marker, Rain of Fire.
-- Safe to run again.
DELETE FROM `spell_script_names` WHERE (`spell_id`, `ScriptName`) IN (
(29166, 'spell_dru_innervate'),
(755, 'spell_warl_health_funnel'),
(1079, 'spell_dru_rip'),
(5246, 'spell_warr_intimidating_shout'),
(99, 'spell_dru_incapacitating_roar'),
(5215, 'spell_dru_prowl'),
(586, 'spell_pri_translucent_image'),
(6201, 'spell_warl_create_healthstone'),
(19740, 'spell_pal_blessing_of_might'));
