//==============================================================================
// VR-only two-bone arm target solver.
//==============================================================================

#include "VRArmIK.hpp"

#include <cmath>

namespace a51::xr
{

const char* const kMultiplayerUpperArmBones[2] =
{
    "B_01_Arm_L_UpperArm", "B_01_Arm_R_UpperArm"
};

const char* const kMultiplayerForearmBones[2] =
{
    "B_01_Arm_L_ForeArm", "B_01_Arm_R_ForeArm"
};

const char* const kMultiplayerHandBones[2] =
{
    "B_02_Arm_L_Hand", "B_02_Arm_R_Hand"
};

namespace
{

ik_vec3 Add( const ik_vec3& A, const ik_vec3& B )
{
    return { A.X + B.X, A.Y + B.Y, A.Z + B.Z };
}

ik_vec3 Subtract( const ik_vec3& A, const ik_vec3& B )
{
    return { A.X - B.X, A.Y - B.Y, A.Z - B.Z };
}

ik_vec3 Scale( const ik_vec3& V, f32 S )
{
    return { V.X * S, V.Y * S, V.Z * S };
}

f32 Dot( const ik_vec3& A, const ik_vec3& B )
{
    return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
}

ik_vec3 Cross( const ik_vec3& A, const ik_vec3& B )
{
    return { A.Y * B.Z - A.Z * B.Y,
             A.Z * B.X - A.X * B.Z,
             A.X * B.Y - A.Y * B.X };
}

f32 LengthSquared( const ik_vec3& V )
{
    return Dot( V, V );
}

xbool Normalize( ik_vec3& V )
{
    const f32 LengthSq = LengthSquared( V );
    if( !std::isfinite( LengthSq ) || LengthSq < 1.0e-8f )
        return FALSE;
    V = Scale( V, 1.0f / std::sqrt( LengthSq ) );
    return TRUE;
}

} // anonymous namespace

xbool SolveTwoBoneArm( const ik_vec3& Shoulder,
                       const ik_vec3& WristTarget,
                       const ik_vec3& ElbowPole,
                       f32 UpperArmLength,
                       f32 ForearmLength,
                       arm_ik_result& Result )
{
    Result.Valid = FALSE;
    if( !std::isfinite( UpperArmLength ) ||
        !std::isfinite( ForearmLength ) ||
        UpperArmLength <= 1.0e-4f || ForearmLength <= 1.0e-4f )
    {
        return FALSE;
    }

    ik_vec3 ShoulderToTarget = Subtract( WristTarget, Shoulder );
    f32 Distance = std::sqrt( LengthSquared( ShoulderToTarget ) );
    if( !std::isfinite( Distance ) )
        return FALSE;

    ik_vec3 Direction;
    if( Distance > 1.0e-4f )
        Direction = Scale( ShoulderToTarget, 1.0f / Distance );
    else
    {
        Direction = { 0.0f, 0.0f, 1.0f };
        Distance = 0.0f;
    }

    const f32 MinReach = std::fabs( UpperArmLength - ForearmLength ) + 1.0e-4f;
    const f32 MaxReach = UpperArmLength + ForearmLength - 1.0e-4f;
    const f32 Reach = ( Distance < MinReach ) ? MinReach
                    : ( Distance > MaxReach ) ? MaxReach
                    : Distance;
    const ik_vec3 Wrist = Add( Shoulder, Scale( Direction, Reach ) );

    ik_vec3 PoleDirection = Subtract( ElbowPole, Shoulder );
    PoleDirection = Subtract( PoleDirection,
                              Scale( Direction, Dot( PoleDirection, Direction ) ) );
    if( !Normalize( PoleDirection ) )
    {
        PoleDirection = Cross( Direction, { 0.0f, 1.0f, 0.0f } );
        if( !Normalize( PoleDirection ) )
        {
            PoleDirection = Cross( Direction, { 1.0f, 0.0f, 0.0f } );
            if( !Normalize( PoleDirection ) )
                return FALSE;
        }
    }

    const f32 Along = ( UpperArmLength * UpperArmLength -
                        ForearmLength * ForearmLength + Reach * Reach ) /
                      ( 2.0f * Reach );
    const f32 HeightSq = UpperArmLength * UpperArmLength - Along * Along;
    const f32 Height = std::sqrt( ( HeightSq > 0.0f ) ? HeightSq : 0.0f );

    Result.Shoulder = Shoulder;
    Result.Elbow = Add( Add( Shoulder, Scale( Direction, Along ) ),
                        Scale( PoleDirection, Height ) );
    Result.Wrist = Wrist;
    Result.Valid = TRUE;
    return TRUE;
}

} // namespace a51::xr
