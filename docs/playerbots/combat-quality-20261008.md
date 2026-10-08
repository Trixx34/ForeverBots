# Combat quality fixes, 2026-10-08

Evidence: analysis-2026-10-07-run2.md. Not sim-verified (no build available when written).

- Wand Shoot result 32 is SPELL_FAILED_DONT_REPORT. Spell::CheckCast returns it for an auto-repeat/ranged spell while the ranged swing timer is not ready (benign). It was counted as a wand failure, so priest, mage and warlock dropped to melee fallback after two such results. It now counts like NOT_READY: no wand-failure count, logged as routine.
- Judgement CASTER_AURASTATE: Judgement needs a seal and consumes it. Self buffs were throttled to one cast per 25 s, so after the first Judgement the seal was not recast and the next Judgement had no seal. A buff that another spell requires (the seal) is now recast after 3 s. The existing ReqAura check and the 60 s backoff on aura-state results stay.
- Hunter (Auto Shot first, Raptor Strike only in melee range) and the 32/172 decoding were already in 93c0daa9; unchanged.

To verify in the sim: paladin Judgement success count and CASTER_AURASTATE count; wand Shoot successes and RANGED_TO_MELEE count for priest/mage/warlock; hunter Auto Shot vs Raptor Strike cast ratio.
