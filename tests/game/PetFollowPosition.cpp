#include "tc_catch2.h"
#include "Creature.h"
#include "MotionMaster.h"
#include "PetFollowPosition.h"

namespace
{
    struct Follower : Creature
    {
        float Angle;
        Follower(bool pet, float angle) : Creature(false), Angle(angle)
        {
            if (pet)
                m_unitTypeMask |= UNIT_MASK_PET;
        }
        float GetFollowAngle() const override { return Angle; }
    };
}

TEST_CASE("Hunter pets follow beside while other pet types retain their angle", "[PetFollowPosition]")
{
    CHECK(PetFollowPosition::FollowAngle(HUNTER_PET, PET_FOLLOW_ANGLE) == Catch::Approx(float(M_PI / 2)));
    CHECK(PetFollowPosition::FollowAngle(SUMMON_PET, PET_FOLLOW_ANGLE) == PET_FOLLOW_ANGLE);
    CHECK(PetFollowPosition::FollowAngle(SUMMON_PET, 0.75f) == 0.75f);
}

TEST_CASE("Return from combat uses the same pet angle as ordinary follow", "[PetFollowPosition]")
{
    Follower hunter(true, float(M_PI / 2)), demon(true, PET_FOLLOW_ANGLE), guardian(false, 0.75f);
    REQUIRE(hunter.IsPet());
    CHECK(MotionMaster::GetHomeFollowAngle(hunter) == Catch::Approx(float(M_PI / 2)));
    CHECK(MotionMaster::GetHomeFollowAngle(demon) == PET_FOLLOW_ANGLE);
    REQUIRE_FALSE(guardian.IsPet());
    CHECK(MotionMaster::GetHomeFollowAngle(guardian) == PET_FOLLOW_ANGLE);
}

TEST_CASE("Recall uses the hunter follow distance and angle without relocating other summons", "[PetFollowPosition]")
{
    auto hunter = PetFollowPosition::RecallOffset(HUNTER_PET, float(M_PI / 2));
    CHECK(hunter.Distance == PET_FOLLOW_DIST);
    CHECK(hunter.Angle == Catch::Approx(float(M_PI / 2)));
    auto other = PetFollowPosition::RecallOffset(SUMMON_PET, 0.75f);
    CHECK(other.Distance == 0.0f);
    CHECK(other.Angle == 0.0f);
}
