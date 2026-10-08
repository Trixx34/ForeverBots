/* This file is part of TrinityCore; licensed under GPL version 2 or later. */
#ifndef TRINITY_PET_FOLLOW_POSITION_H
#define TRINITY_PET_FOLLOW_POSITION_H

#include "Common.h"
#include "PetDefines.h"

namespace PetFollowPosition
{
inline float FollowAngle(PetType type, float inheritedAngle)
{
    return type == HUNTER_PET ? PET_FOLLOW_ANGLE * 0.5f : inheritedAngle;
}

struct Offset { float Distance; float Angle; };

inline Offset RecallOffset(PetType type, float followAngle)
{
    return type == HUNTER_PET ? Offset{ PET_FOLLOW_DIST, followAngle } : Offset{ 0.0f, 0.0f };
}
}

#endif
