# Bot AI: common core and class/role split (design note, 2026-10-07)

Status: proposal, nothing implemented. Evidence: `analysis-2026-10-07.md`, `analysis-2026-10-07-run2.md`.

## Today
- Combat code lives in one anonymous namespace in `BotCombat.cpp`. Role is a 3-value enum (Melee, Caster, Hunter) from `RoleOf(class)`; spec and talents are never read.
- `SPELLS[]` has one row per spell with a Kind (SelfBuff, Direct, Dot, Finisher, AutoShot, Wand, Heal); only `ReqAura` and `MinCombo` are per-spell conditions.
- One `combat` strategy wires flee, low-hp flee, heal, engage and cast. `CastAction` picks spells by Kind, not role. NonCombat strategies (rest, goto, follow, quest) are already separate and class-agnostic.

## 1. Common core and the seam
Stays common: target choice and gap rules, aggro awareness, flee, rest/drink/eat, vendor/train/repair, quest/nav/stuck, dead recovery, logging, cast plumbing (rank resolve, result names, fail dedupe, range, face and chase).

The seam is the choice of the next combat spell and the stance toward the target (melee, at range, hold). One interface behind the existing `combat_cast` and `combat_engage` actions:

    struct RoleProfile {
        Role role;
        float standRangePct;      // casters: % of spell range; melee: 0
        bool usesAutoRepeat;      // Auto Shot / wand
        Resolved const* PickSpell(BotCombatCtx&, Player*, Unit* target); // nullptr = fall back
        FallbackPolicy fallback;
    };

Engine unchanged. `combat_common` (flee, heal, engage/cast skeleton) is always on; `role_melee`, `role_ranged`, `role_caster`, `role_healer` register their own cast trigger and action at the same relevance bands; `role_tank` comes later. `DefaultStrategies` picks the role strategy from the resolved role; `bot strategy` can swap at runtime. Multipliers stay generic (act on action flags).

## 2. Roles and selection
- Melee dps: warrior, rogue, paladin (Ret or leveling default), feral druid, enhancement shaman. Ranged physical: hunter. Caster dps: mage, warlock, shadow or leveling priest, balance druid, elemental shaman. Healer: Holy/Discipline priest, Holy paladin, Resto druid and shaman. Tank: Prot warrior, Prot paladin, bear druid (groups only, later).
- Picked once at login and on level-up/respec: dominant talent tree mapped by (class, tree). Below a point threshold (about 5) or below level 10, use the leveling default (warrior, rogue, paladin melee; hunter ranged; mage, warlock, priest caster with wand filler; shaman melee; druid caster until Bear or Cat form).
- Per-bot override via `bot role <bot> <role>` for tests. Log the picked role and talent counts once.

## 3. Data out of the switch blocks
- Per-class tables (`BotClassData.h` or one `.cpp` per class). Rows gain `roleMask`, `priority`, `minRangeOk`, `manaMin` and a small condition enum. Rotation per role: rows for this class and role by priority, first one that passes. This replaces the Kind switch in `CastAction`.
- Class files only for what tables cannot express: hunter pet, rogue stealth/opener, druid forms, shaman totems, warrior stances, paladin seals/auras, warlock pets, priest self-buffs, enabled by `class_<name>` strategies. Keep `ValidateTable` over all tables.

## 4. Steps (each: build, sim 10 bots per class, compare with the analysis baselines)
- A. Fix three known bugs in the current structure (`BotCombat.cpp`): hunter stays in melee (Raptor Strike 19,864 vs Auto Shot 813: keep at range, melee only as fallback, verify Auto Shot active); wand Shoot fails 1,209 vs 104 successes with no melee fallback (fall back after 1-2 failures, clear when mana is back); paladin Judgement 4,510 `CASTER_AURASTATE` failures (verify the seal aura is actually present; back off N s after a failure).
- B. Mana awareness: skip spells whose cost exceeds power, fall to wand or melee, drink after the fight. All NO_POWER failures are mana.
- C. Aggro awareness while idle and between actions (common core, `BotBehavior.cpp` and target chooser): scan for hostile mobs in aggro radius that are higher level or elite and path around, wait outside leash, or pick another target. Mob-first fights are about 85% of deaths.
- D. Introduce `Role` and `RoleProfile` (new `BotRoles.{h,cpp}`, `BotCombat.cpp`, `BotAI.cpp`), behavior unchanged at first.
- E. Split `SPELLS[]` per class with role masks, one class at a time: warrior, hunter, casters.
- F. Move class features into class files only when content needs them.
- G. Flee A/B once A-C have removed the larger death causes.

## 5. Left for groups (hooks now)
Tank threat/taunt, healer triage, interrupts, crowd control, assist/focus rules, group positions. Hooks: Healer and Tank in the Role enum with selection logic; `GetPreferredTarget` on `RoleProfile`; a `Heal` kind that can target other units; a group-context value that is empty when solo; a role field on every combat decision row; registered but non-default `role_tank` and `role_healer` slots.

## 6. Open decisions
1. Auto spec from talent counts, or fixed per class at first?
2. Leveling default for shaman, druid, paladin (proposed: shaman melee, paladin melee, druid caster).
3. Does a hunter ever go to melee voluntarily (pet tanking, adjacent enemy)?
4. Wand-caster fallback threshold (failures, mana %).
5. First aggro-awareness priority: all mobs not on the quest list, or only elite/higher-level?
6. Class file order: warrior first, or worst evidence first (hunter, priest, paladin)?
7. Are bots respecced with an automatic build?
