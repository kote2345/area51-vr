//==============================================================================
// VR-only two-bone arm target solver for the Area 51 multiplayer avatar.
//==============================================================================

#ifndef A51_VR_ARM_IK_HPP
#define A51_VR_ARM_IK_HPP

#include "x_types.hpp"

namespace a51::xr
{

struct ik_vec3
{
    f32 X;
    f32 Y;
    f32 Z;
};

struct arm_ik_result
{
    ik_vec3 Shoulder;
    ik_vec3 Elbow;
    ik_vec3 Wrist;
    xbool   Valid;
};

/* Area 51 multiplayer soldier skeleton names. These bones are resolved on
 * the already loaded game avatar; this module does not ship a second model. */
extern const char* const kMultiplayerUpperArmBones[2];
extern const char* const kMultiplayerForearmBones[2];
extern const char* const kMultiplayerHandBones[2];

/* Solve a shoulder/elbow/wrist chain in one caller supplied space. Segment
 * lengths and positions use the same units. The pole point steers the elbow. */
xbool SolveTwoBoneArm( const ik_vec3& Shoulder,
                       const ik_vec3& WristTarget,
                       const ik_vec3& ElbowPole,
                       f32 UpperArmLength,
                       f32 ForearmLength,
                       arm_ik_result& Result );

} // namespace a51::xr

#endif // A51_VR_ARM_IK_HPP
