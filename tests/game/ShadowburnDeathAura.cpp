// Native Shadowburn death-item aura must be armed before its direct damage.
// Detached fixtures exercise real AuraApplication state, not Player inventory.
#include "tc_catch2.h"
#include "Creature.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellAuraEffects.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include <memory>
#include <tuple>

namespace
{
    struct Record
    {
        SpellNameEntry Name{};
        SpellMiscEntry Misc{};
        SpellClassOptionsEntry Family{};
        SpellEffectEntry Effects[2]{};
        std::unique_ptr<SpellInfo> Info;
        explicit Record(uint32 id, bool native = true)
        {
            Name.ID = Misc.SpellID = Family.SpellID = id;
            Misc.Attributes[15] = native ? 8192 : 0;
            Misc.SchoolMask = SPELL_SCHOOL_MASK_SHADOW;
            Family.SpellClassSet = SPELLFAMILY_WARLOCK;
            Family.SpellClassMask[0] = 128;
            for (uint8 i = 0; i < 2; ++i)
            {
                Effects[i].SpellID = id;
                Effects[i].EffectIndex = i;
                Effects[i].ImplicitTarget[0] = TARGET_UNIT_TARGET_ENEMY;
            }
            Effects[0].Effect = SPELL_EFFECT_APPLY_AURA;
            Effects[0].EffectAura = SPELL_AURA_CHANNEL_DEATH_ITEM;
            Effects[0].EffectBasePoints = 1;
            Effects[0].EffectItemType = 6265;
            Effects[1].Effect = SPELL_EFFECT_SCHOOL_DAMAGE;
            SpellInfoLoadHelper load;
            load.Misc = &Misc; load.ClassOptions = &Family;
            load.Effects[0] = &Effects[0]; load.Effects[1] = &Effects[1];
            Info = std::make_unique<SpellInfo>(&Name, DIFFICULTY_NONE, load);
        }
    };
    struct Target : Creature
    {
        Target() : Creature(false) { _Create(ObjectGuid::Create<HighGuid::Creature>(0, 1, 12)); SetClass(CLASS_WARLOCK); }
    };
    struct DetachedAura : Aura
    {
        using Aura::Aura;
        void Remove(AuraRemoveMode mode) override { if (!IsRemoved()) _Remove(mode); }
        void FillTargetMap(std::unordered_map<Unit*, uint32>&, Unit*) override { }
    };
    struct HitAura
    {
        DetachedAura Instance;
        AuraApplication* Application;
        HitAura(Record const& record, Target& target)
            : Instance(AuraCreateInfo(ObjectGuid::Empty, record.Info.get(), DIFFICULTY_NONE,
                1, &target).SetCasterGUID(target.GetGUID()))
        {
            Instance._InitEffects(1, nullptr, nullptr);
            Application = target._CreateAuraApplication(&Instance, 1);
            REQUIRE(Application != nullptr);
        }
        ~HitAura() { Instance.Remove(AURA_REMOVE_BY_DEFAULT); }
    };
}

TEST_CASE("All native Shadowburn ranks arm the death item before the killing blow", "[ShadowburnDeathAura]")
{
    for (uint32 id : {17877u, 18867u, 18868u, 18869u, 18870u, 18871u})
    {
        Record record(id);
        Target target;
        HitAura aura(record, target);
        REQUIRE_FALSE(aura.Application->HasEffect(EFFECT_0));
        CHECK(Spell::ApplyShadowburnDeathAura(target, *record.Info, &aura.Instance, 3));
        CHECK(aura.Application->HasEffect(EFFECT_0));
        CHECK_FALSE(Spell::ApplyShadowburnDeathAura(target, *record.Info, &aura.Instance, 3));
        aura.Instance.Remove(AURA_REMOVE_BY_DEATH);
        CHECK(aura.Instance.IsRemoved());
    }
}

TEST_CASE("Shadowburn early death aura is restricted to its native successful aura effect", "[ShadowburnDeathAura]")
{
    bool native = true;
    uint32 id = 17877, hitMask = 3;
    SECTION("Retail record") { native = false; }
    SECTION("Drain Soul remains unchanged") { id = 1120; }
    SECTION("immune aura effect") { hitMask = 2; }
    SECTION("no hit") { hitMask = 0; }
    Record record(id, native);
    Target target;
    HitAura aura(record, target);
    CHECK_FALSE(Spell::ApplyShadowburnDeathAura(target, *record.Info, &aura.Instance, hitMask));
    CHECK_FALSE(aura.Application->HasEffect(EFFECT_0));
    CHECK_FALSE(Spell::ApplyShadowburnDeathAura(target, *record.Info, nullptr, 3));
}
