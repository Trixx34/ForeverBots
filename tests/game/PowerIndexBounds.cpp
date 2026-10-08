#include "tc_catch2.h"
#include "DB2Stores.h"
#include <limits>

TEST_CASE("Class power lookup rejects out of range resource identifiers", "[PowerIndexBounds]")
{
    for (uint32 classId : { uint32(CLASS_WARRIOR), uint32(CLASS_HUNTER), uint32(CLASS_PRIEST), uint32(CLASS_SHAMAN) })
    {
        CAPTURE(classId);
        // REQUIRE stops the unfixed lookup before the much larger invalid indices.
        REQUIRE(DB2Manager::GetPowerIndexByClass(Powers(MAX_POWERS), classId) == MAX_POWERS_PER_CLASS);
        CHECK(DB2Manager::GetPowerIndexByClass(Powers(MAX_POWERS + 1), classId) == MAX_POWERS_PER_CLASS);
        CHECK(DB2Manager::GetPowerIndexByClass(Powers(-1), classId) == MAX_POWERS_PER_CLASS);
        CHECK(DB2Manager::GetPowerIndexByClass(Powers(std::numeric_limits<int32>::max()), classId) == MAX_POWERS_PER_CLASS);
    }
}
