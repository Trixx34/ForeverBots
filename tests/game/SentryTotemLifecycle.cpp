// Regression for the owner-cast Sentry aura surviving its actual Totem::UnSummon.
// Real AuraApplication registration/removal and Totem lifecycle are exercised.
// Detached fixtures have no Map: final world removal/packets remain ingame gates.
#include "tc_catch2.h"
#include "Creature.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Totem.h"
#include <memory>

namespace
{
    struct Owner : Creature
    {
        CreatureTemplate Template{};
        explicit Owner(uint64 id) : Creature(false)
        {
            _Create(ObjectGuid::Create<HighGuid::Creature>(0, 1, id));
            Template.type = CREATURE_TYPE_HUMANOID;
            m_creatureInfo = &Template;
            SetClass(CLASS_SHAMAN);
            SetLevel(40, false);
        }
    };

    struct Record
    {
        SpellNameEntry Name{};
        SpellMiscEntry Misc{};
        SpellClassOptionsEntry Family{};
        SpellEffectEntry Effects[2]{};
        std::unique_ptr<SpellInfo> Info;
        explicit Record(uint32 id)
        {
            Name.ID = Misc.SpellID = Family.SpellID = id;
            Misc.Attributes[15] = 8192;
            Family.SpellClassSet = SPELLFAMILY_SHAMAN;
            for (uint8 i = 0; i < 2; ++i)
            {
                Effects[i].SpellID = id;
                Effects[i].EffectIndex = i;
                Effects[i].ImplicitTarget[0] = TARGET_UNIT_CASTER;
            }
            // Native Sentry parent E0 summon and E1 owner-cast dummy aura.
            Effects[0].Effect = SPELL_EFFECT_SUMMON;
            Effects[0].EffectMiscValue[0] = 3968;
            Effects[0].EffectMiscValue[1] = 83;
            Effects[1].Effect = SPELL_EFFECT_APPLY_AURA;
            Effects[1].EffectAura = SPELL_AURA_DUMMY;
            SpellInfoLoadHelper load;
            load.Misc = &Misc; load.ClassOptions = &Family;
            load.Effects[0] = &Effects[0]; load.Effects[1] = &Effects[1];
            Info = std::make_unique<SpellInfo>(&Name, DIFFICULTY_NONE, load);
        }
    };

    struct DetachedAura : Aura
    {
        using Aura::Aura;
        void Remove(AuraRemoveMode mode) override { if (!IsRemoved()) _Remove(mode); }
        void FillTargetMap(std::unordered_map<Unit*, uint32>&, Unit*) override { }
    };

    struct RegisteredAura
    {
        DetachedAura Instance;
        RegisteredAura(Record const& record, Unit& owner, ObjectGuid caster)
            : Instance(AuraCreateInfo(ObjectGuid::Empty, record.Info.get(), DIFFICULTY_NONE,
                2, &owner).SetCasterGUID(caster))
        {
            Instance._InitEffects(2, nullptr, nullptr);
            REQUIRE(owner._CreateAuraApplication(&Instance, 2) != nullptr);
        }
        ~RegisteredAura() { Instance.Remove(AURA_REMOVE_BY_DEFAULT); }
    };
}

TEST_CASE("Sentry UnSummon removes its owner-cast aura and preserves unrelated auras", "[SentryTotemLifecycle]")
{
    Owner owner(11);
    Record sentry(6495), unrelated(905000); // unrelated is a synthetic control.
    RegisteredAura ownAura(sentry, owner, owner.GetGUID());
    RegisteredAura otherAura(unrelated, owner, owner.GetGUID());
    Totem totem(nullptr, &owner);
    totem.SetEntry(3968);
    totem.SetCreatedBySpell(6495);
    REQUIRE(totem.GetSpell() == 0); // Exact live defect: parent aura is not slot0.
    REQUIRE(owner.HasAura(6495, owner.GetGUID()));

    totem.UnSummon();

    CHECK_FALSE(owner.HasAura(6495, owner.GetGUID()));
    CHECK(ownAura.Instance.IsRemoved());
    CHECK(owner.HasAura(905000, owner.GetGUID()));
    CHECK_FALSE(otherAura.Instance.IsRemoved());
}

TEST_CASE("Other totem removals preserve the Sentry owner aura", "[SentryTotemLifecycle]")
{
    Owner owner(11);
    Record sentry(6495);
    RegisteredAura aura(sentry, owner, owner.GetGUID());
    Totem totem(nullptr, &owner);
    totem.SetEntry(3968);
    totem.SetCreatedBySpell(6495);
    SECTION("another native summon") { totem.SetCreatedBySpell(8512); }
    SECTION("another creature entry") { totem.SetEntry(6112); }
    SECTION("another owner class") { owner.SetClass(CLASS_PRIEST); }
    SECTION("superseded air summon") { owner.m_SummonSlot[SUMMON_SLOT_TOTEM + 3] = owner.GetGUID(); }
    totem.UnSummon();
    CHECK(owner.HasAura(6495, owner.GetGUID()));
    CHECK_FALSE(aura.Instance.IsRemoved());
}
