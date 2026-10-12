# Module System — New `UnitScript::OnAuraChange` Hook Design

Status: design drafted, not yet implemented.
Resolves gap #1 from `08-forever-bot-migration-gap-analysis.md` — the one touch point without an existing generic `ScriptMgr` hook.

## Finding: existing coverage is better than first estimated

Re-checking `UnitScript` (`src/server/game/Scripting/ScriptMgr.h`) shows **more** existing coverage than the gap analysis first credited:

```cpp
class TC_GAME_API UnitScript : public ScriptObject
{
    virtual void OnHeal(Unit* healer, Unit* reciever, uint32& gain);
    virtual void OnDamage(Unit* attacker, Unit* victim, uint32& damage);
    virtual void ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& damage);
    virtual void ModifyMeleeDamage(Unit* target, Unit* attacker, uint32& damage);
    virtual void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellInfo const* spellInfo);
};
```

Melee/spell/DoT damage are **already separately granular** — a ported bot module gets finer-grained combat data than `BotAI::OnDamageTaken`'s single entry point provides today, for free. Confirmed call sites: `Unit.cpp:851` (`OnDamage`), `Unit.cpp:6660` (`OnHeal`).

## The one real gap: aura apply/remove

No existing `UnitScript` hook fires generically on aura application/removal — `AuraScript` is per-spell-authored (a script tied to one specific spell ID), not a subscribable "notify me about all aura changes on this unit" hook, which is what a bot needs to react to buffs/debuffs generically.

**Exact call sites found** (`src/server/game/Entities/Unit/Unit.cpp`):
- `Unit::_ApplyAura(AuraApplication* aurApp, uint32 effMask)` — `:3629`
- `Unit::_ApplyAuraEffect(Aura* aura, uint8 effIndex)` — `:3615`
- `Unit::_UnapplyAura(AuraApplicationMap::iterator& i, AuraRemoveMode removeMode)` — `:3686`
- `Unit::_UnapplyAura(AuraApplication* aurApp, AuraRemoveMode removeMode)` — `:3778`

## Proposed hook

Add to `UnitScript` (`ScriptMgr.h`), following the exact style of `OnDamage`/`OnHeal`:

```cpp
// Called when an aura is applied to or removed from a unit
virtual void OnAuraChange(Unit* target, AuraApplication* aurApp, bool apply);
```

- Single hook covers both apply and remove (matches the `bool apply` style already used in `AuraApplication::_HandleEffect(uint8 effIndex, bool apply)` — consistent with existing codebase conventions rather than inventing a new pattern).
- Fired from both `_ApplyAura` (with `apply = true`) and both `_UnapplyAura` overloads (with `apply = false`) in `ScriptMgr.cpp`'s corresponding `UnitScript`-dispatch function (mirroring how `OnDamage`/`OnHeal` are dispatched — one `ScriptMgr::OnAuraChange(...)` wrapper that loops `ScriptRegistry<UnitScript>` and calls each registered script's `OnAuraChange`).
- `AuraApplication*` (not just the `Aura*`) is passed because it carries the effect mask and target-specific application state — matches what `_ApplyAura`/`_UnapplyAura` already operate on, avoiding an awkward reconstruction of apply-context from a bare `Aura*`.

## Mapping to the Tier 1 module protocol

This becomes a new `EventNotify` payload type (fire-and-forget — a module reacting to a buff appearing doesn't need to block the aura-application call site), e.g.:

```proto
message AuraChangeEvent {
  uint64 target_guid = 1;
  uint32 spell_id = 2;
  bool applied = 3;       // true = applied, false = removed
}
```

Registered the same way as every other Tier 1 event: core's `UnitScript`-derived dispatcher (the module-system equivalent `ScriptObject` subclass, per `08`'s "formalize as proper `ScriptMgr` hooks" decision) forwards to `ModuleMgr`, which routes it as an `EventNotify` to any module subscribed to `UnitScript.OnAuraChange` in its manifest.

## Net effect on the bot migration gap analysis

Confirms and slightly strengthens the `08` conclusion: **all six of the original ad-hoc bot touch points now map to a `ScriptMgr` hook** — five already existed (with damage actually being three separately-granular hooks, not one), and this doc specifies the sixth (`OnAuraChange`) in concrete, implementable detail, following exact existing code conventions rather than inventing a new pattern. No remaining "undesigned" hook-category gap for the core-engine touch points.

## Next action

With the hook gap fully resolved, the next open item from `08`'s recommended path is **standing up the dedicated module DB/schema** for bot-private bookkeeping (migrating `forever_botlog` + genuinely module-private tables) — or, alternatively, scoping the low-frequency subsystem ports (quest/loot/auction/gear/chat) that this hook design now unblocks.
