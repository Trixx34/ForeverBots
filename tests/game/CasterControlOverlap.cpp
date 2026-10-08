#include "tc_catch2.h"
#include "Creature.h"
#include "DummyData.h"
#include "Spell.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include <memory>

namespace
{
    struct Record
    {
        SpellNameEntry Name{};
        std::unique_ptr<SpellInfo> Info;
        Record(uint32 id, AuraType aura, Mechanics mechanic = MECHANIC_NONE)
        {
            Name.ID = id;
            SpellEffectEntry effect{};
            effect.Effect = SPELL_EFFECT_APPLY_AURA;
            effect.EffectAura = aura;
            effect.ImplicitTarget[0] = TARGET_UNIT_CASTER;
            Info = std::make_unique<SpellInfo>(&Name, DIFFICULTY_NONE, std::vector<SpellEffectEntry>{ effect });
            Info->Mechanic = mechanic;
        }
    };
    struct DetachedAura final : Aura
    {
        using Aura::Aura;
        void Remove(AuraRemoveMode mode) override { if (!IsRemoved()) _Remove(mode); }
        void FillTargetMap(std::unordered_map<Unit*, uint32>&, Unit*) override { }
    };
    struct AppliedEffect
    {
        Unit& Owner;
        DetachedAura Instance;
        AppliedEffect(Unit& owner, SpellInfo const& spell)
            : Owner(owner), Instance(AuraCreateInfo(ObjectGuid::Empty, &spell, DIFFICULTY_NONE, 1, &owner))
        {
            Instance._InitEffects(1, nullptr, nullptr);
            owner._RegisterAuraEffect(Instance.GetEffect(0), true);
        }
        ~AppliedEffect() { Owner._RegisterAuraEffect(Instance.GetEffect(0), false); }
    };
    SpellCastResult Admit(Unit& owner, SpellInfo const& spell, int32* mechanic = nullptr)
    {
        return Spell::CheckCasterAuras(&owner, &spell, false, SPELL_SCHOOL_MASK_NORMAL, mechanic);
    }
}

TEST_CASE("Usable while stunned does not bypass a concurrent prevention category", "[CasterControlOverlap]")
{
    struct Case { AuraType Aura; SpellPreventionType Prevention; SpellCastResult Failure; };
    for (Case test : {
        Case{ SPELL_AURA_MOD_SILENCE, SPELL_PREVENTION_TYPE_SILENCE, SPELL_FAILED_SILENCED },
        Case{ SPELL_AURA_MOD_PACIFY, SPELL_PREVENTION_TYPE_PACIFY, SPELL_FAILED_PACIFIED },
        Case{ SPELL_AURA_MOD_PACIFY_SILENCE, SPELL_PREVENTION_TYPE_SILENCE, SPELL_FAILED_SILENCED },
        Case{ SPELL_AURA_MOD_PACIFY_SILENCE, SPELL_PREVENTION_TYPE_PACIFY, SPELL_FAILED_PACIFIED },
        Case{ SPELL_AURA_MOD_NO_ACTIONS, SPELL_PREVENTION_TYPE_NO_ACTIONS, SPELL_FAILED_NO_ACTIONS } })
    {
        CAPTURE(test.Aura, test.Prevention);
        Creature caster(false);
        Record cast(900100, SPELL_AURA_DUMMY), stun(900101, SPELL_AURA_MOD_STUN, MECHANIC_STUN), prevention(900102, test.Aura);
        cast.Info->AttributesEx5 |= SPELL_ATTR5_ALLOW_WHILE_STUNNED;
        cast.Info->PreventionType = test.Prevention;
        UnitTestDataLoader::LoadSpellImmunities(*cast.Info);
        AppliedEffect active(caster, *prevention.Info);
        caster.SetSilencedSchoolMask(SPELL_SCHOOL_MASK_NORMAL);
        caster.SetUnitFlag(UNIT_FLAG_PACIFIED);
        caster.SetUnitFlag2(UNIT_FLAG2_NO_ACTIONS);
        CHECK(Admit(caster, *cast.Info) == test.Failure);
        AppliedEffect activeStun(caster, *stun.Info);
        caster.SetUnitFlag(UNIT_FLAG_STUNNED);
        CHECK(Admit(caster, *cast.Info) == test.Failure);
        cast.Info->PreventionType = SPELL_PREVENTION_TYPE_NONE;
        CHECK(Admit(caster, *cast.Info) == SPELL_CAST_OK);
    }
}

TEST_CASE("Fear and confusion remain checked when stun hides their movement flag", "[CasterControlOverlap]")
{
    for (bool fear : { false, true })
        for (bool stunFirst : { false, true })
        {
            CAPTURE(fear, stunFirst);
            Creature caster(false);
            Record cast(900100, SPELL_AURA_DUMMY), stun(900101, SPELL_AURA_MOD_STUN, MECHANIC_STUN);
            Record control(900102, fear ? SPELL_AURA_MOD_FEAR : SPELL_AURA_MOD_CONFUSE,
                fear ? MECHANIC_FEAR : MECHANIC_POLYMORPH);
            cast.Info->AttributesEx5 |= SPELL_ATTR5_ALLOW_WHILE_STUNNED;
            UnitTestDataLoader::LoadSpellImmunities(*cast.Info);
            AppliedEffect first(caster, stunFirst ? *stun.Info : *control.Info);
            AppliedEffect second(caster, stunFirst ? *control.Info : *stun.Info);
            caster.SetUnitFlag(UNIT_FLAG_STUNNED);
            caster.AddUnitState(fear ? UNIT_STATE_FLEEING : UNIT_STATE_CONFUSED);
            int32 mechanic = 0;
            CHECK(Admit(caster, *cast.Info, &mechanic) == SPELL_FAILED_PREVENTED_BY_MECHANIC);
            CHECK(mechanic == int32(fear ? MECHANIC_FEAR : MECHANIC_POLYMORPH));
        }
}

TEST_CASE("Permissions for every concurrent control mechanic still admit the cast", "[CasterControlOverlap]")
{
    Creature caster(false);
    Record cast(900100, SPELL_AURA_DUMMY), stun(900101, SPELL_AURA_MOD_STUN, MECHANIC_STUN);
    Record fear(900102, SPELL_AURA_MOD_FEAR, MECHANIC_FEAR), confuse(900103, SPELL_AURA_MOD_CONFUSE, MECHANIC_DISORIENTED);
    cast.Info->AttributesEx5 |= SPELL_ATTR5_ALLOW_WHILE_STUNNED | SPELL_ATTR5_ALLOW_WHILE_FLEEING | SPELL_ATTR5_ALLOW_WHILE_CONFUSED;
    UnitTestDataLoader::LoadSpellImmunities(*cast.Info);
    AppliedEffect a(caster, *stun.Info), b(caster, *fear.Info), c(caster, *confuse.Info);
    caster.SetUnitFlag(UNIT_FLAG_STUNNED);
    caster.AddUnitState(UNIT_STATE_FLEEING | UNIT_STATE_CONFUSED);
    CHECK(Admit(caster, *cast.Info) == SPELL_CAST_OK);
}
