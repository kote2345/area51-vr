//=========================================================================
//
//  PlayerMovement.cpp
//
//=========================================================================

#include "PlayerMovement.hpp"
#include "Player.hpp"
#include "Objects/Ladders/Ladder_Field.hpp"
#include "Objects/Pickup.hpp"
#include "Objects/Turret.hpp"
#include "NetworkMgr/GameMgr.hpp"
#include "PerceptionMgr/PerceptionMgr.hpp"

#if defined( A51_ENABLE_OPENXR )
#include "VR/XRSession.hpp"
#if defined(TARGET_ANDROID)
#include <android/log.h>
#endif
extern xbool g_XRGameFrameBridge;
extern a51::xr::VulkanSession g_XRSession;
#endif

//=========================================================================

static const f32 ZERO = 0.00001f;
static const f32 s_ArmsDampen                 = 0.2f;
static const f32 s_ArmReturnForceMultiplier   = 75.0f;
static const f32 s_MovementReferenceFrameRate = 30.0f;

static f32  ComputeSoftLean             ( f32 LeanAmount );
static f32  ComputeLean                 ( f32 SoftLeanAmount );
static f32  ComputeLeanValueForPosition ( const vector3& Start,
                                           const vector3& HitPoint,
                                           xbool bLeaningRight );
static void HideObject                  ( object* pObject );
static void UnhideObject                ( object* pObject );

PlayerMovementSpeeds::PlayerMovementSpeeds( void ) :
    Forward( 0.0f ),
    Strafe ( 0.0f )
{
}


//=========================================================================

PlayerMovement::PlayerMovement( void )
{
    Clear();
}

//=========================================================================

void PlayerMovement::Clear( void )
{
    m_GamepadVelocity.Zero();
}

//=========================================================================

void PlayerMovement::ShapeGamepadMoveVector( vector2& Move )
{
    f32 Magnitude = Move.Length();
    if( Magnitude <= F32_MIN )
    {
        Move.Zero();
        return;
    }

    if( Magnitude > 1.0f )
    {
        Move /= Magnitude;
        Magnitude = 1.0f;
    }

    f32 const CurvedMagnitude = Magnitude * x_sqrt( Magnitude );
    Move *= CurvedMagnitude / Magnitude;
}

//=========================================================================

void PlayerMovement::BuildInput( PlayerMoveInput const& Source,
                                 PlayerMoveInput&       Input ) const
{
    Input = Source;

    vector2 GamepadMove( Source.GamepadStrafe, Source.GamepadForward );
    ShapeGamepadMoveVector( GamepadMove );
    Input.GamepadStrafe  = GamepadMove.X;
    Input.GamepadForward = GamepadMove.Y;
}

//=========================================================================

void PlayerMovement::BuildKeyboardSpeeds( PlayerMoveInput const& Input,
                                          f32                    MaxSpeed,
                                          PlayerMovementSpeeds& Speeds )
{
    Speeds.Forward = MaxSpeed * Input.KeyboardForward;
    Speeds.Strafe  = MaxSpeed * Input.KeyboardStrafe;
}

//=========================================================================

void PlayerMovement::ApproachGamepadVelocity( vector2 const& TargetVelocity,
                                              f32            Acceleration,
                                              f32            DecelerationFactor,
                                              f32            DeltaTime,
                                              vector2&       Velocity )
{
    Acceleration       = MAX( 0.0f, Acceleration );
    DecelerationFactor = MAX( 0.0f, DecelerationFactor );
    DeltaTime          = MAX( 0.0f, DeltaTime );

    f32 const TargetSpeedSqr = TargetVelocity.LengthSquared();
    f32 const CurrentSpeedSqr = Velocity.LengthSquared();
    xbool const IsDecelerating = (TargetSpeedSqr <= F32_MIN) ||
                                 (TargetSpeedSqr < CurrentSpeedSqr) ||
                                 (v2_Dot( Velocity, TargetVelocity ) <= 0.0f);
    f32 const Rate = Acceleration * (IsDecelerating ? DecelerationFactor : 1.0f);

    vector2 Delta = TargetVelocity - Velocity;
    f32 const Distance = Delta.Length();
    if( Distance <= F32_MIN )
    {
        Velocity = TargetVelocity;
        return;
    }

    f32 const Step = Rate * DeltaTime;
    if( Step >= Distance )
    {
        Velocity = TargetVelocity;
        return;
    }

    Velocity += Delta * (Step / Distance);
}

//=========================================================================

void PlayerMovement::AdvanceGamepadSpeed( PlayerMoveInput const&        Input,
                                          PlayerMovementSettings const& Settings,
                                          f32                           DeltaTime,
                                          PlayerMovementSpeeds&         Speeds )
{
    f32 const MaxSpeed = MAX( x_abs( Settings.MaxSpeed ), 0.0f );
    vector2 const TargetVelocity( Input.GamepadStrafe * MaxSpeed,
                                  Input.GamepadForward * MaxSpeed );

    ApproachGamepadVelocity( TargetVelocity,
                             Settings.Acceleration,
                             Settings.DecelerationFactor,
                             DeltaTime,
                             m_GamepadVelocity );

    f32 const Speed = m_GamepadVelocity.Length();
    if( Speed > MaxSpeed )
    {
        m_GamepadVelocity *= MaxSpeed / Speed;
    }

    Speeds.Forward = m_GamepadVelocity.Y;
    Speeds.Strafe  = m_GamepadVelocity.X;
}

//=========================================================================

void PlayerMovement::CombineDeviceSpeeds( PlayerMovementSpeeds const& KeyboardSpeeds,
                                          PlayerMovementSpeeds const& GamepadSpeeds,
                                          f32                         MaxSpeed,
                                          PlayerMovementSpeeds&       Speeds )
{
    Speeds.Forward = x_clamp( KeyboardSpeeds.Forward + GamepadSpeeds.Forward,
                              -MaxSpeed,
                               MaxSpeed );
    Speeds.Strafe = x_clamp( KeyboardSpeeds.Strafe + GamepadSpeeds.Strafe,
                             -MaxSpeed,
                              MaxSpeed );
}

//=========================================================================

void PlayerMovement::Evaluate( PlayerMoveInput const&        Input,
                               PlayerMovementSettings const& Settings,
                               f32                          DeltaTime,
                               PlayerMovementSpeeds&        Speeds )
{
    f32 const MaxSpeed = MAX( x_abs( Settings.MaxSpeed ), 0.0f );

    PlayerMovementSpeeds KeyboardSpeeds;
    BuildKeyboardSpeeds( Input, MaxSpeed, KeyboardSpeeds );

    PlayerMovementSpeeds GamepadSpeeds;
    AdvanceGamepadSpeed( Input, Settings, DeltaTime, GamepadSpeeds );

    // Device policies meet only here. Either device may augment or oppose the
    // other, but neither is processed by the other device's response curve.
    CombineDeviceSpeeds( KeyboardSpeeds, GamepadSpeeds, MaxSpeed, Speeds );
}

void player::Push( const vector3& PushVector )
{
    if ( !m_bInTurret )
    {
        m_Physics.SetPosition( GetPosition() );
        m_Physics.Push(PushVector);
        OnMove( m_Physics.GetPosition() );
    }
}

void player::UpdateWeaponPullback( void )
{
    if ( m_CurrentAnimState == ANIM_STATE_DEATH )
    {
        m_WeaponCollisionOffset.Zero();
    }
    else{
        //
        // Check to see if the gun will collide with something. If so, move 
        // it back using m_WeaponCollisionOffset.
        //
        new_weapon* pWeapon = GetCurrentWeaponPtr();
        if ( pWeapon && (pWeapon->GetType() != TYPE_WEAPON_MUTATION) )
        {
            vector3 FirePos;
            vector3 SingleCollisionOffset( 0.0f, 0.0f, 0.0f );
            vector3 LeftCollisionOffset  ( 0.0f, 0.0f, 0.0f );
            vector3 RightCollisionOffset  ( 0.0f, 0.0f, 0.0f );
            f32     SingleCollisionScalar = 0.0f;
            f32     LeftCollisionScalar   = 0.0f;
            f32     RightCollisionScalar  = 0.0f;
            const vector3 ToWeapon             ( m_EyesPosition - pWeapon->GetPosition() );

            if ( pWeapon->GetFiringBonePosition( FirePos, new_weapon::FIRE_POINT_DEFAULT ) )
            {
                FirePos += ToWeapon;
                SingleCollisionOffset = GetWeaponCollisionOffset( pWeapon->GetGuid(), FirePos );
                SingleCollisionScalar = m_WeaponCollisionOffsetScalar;
            }

            if ( pWeapon->GetFiringBonePosition( FirePos, new_weapon::FIRE_POINT_LEFT ) )
            {
                FirePos += ToWeapon;
                LeftCollisionOffset = GetWeaponCollisionOffset( pWeapon->GetGuid(), FirePos );
                LeftCollisionScalar = m_WeaponCollisionOffsetScalar;
            }

            if ( pWeapon->GetFiringBonePosition( FirePos, new_weapon::FIRE_POINT_RIGHT ) )
            {
                FirePos += ToWeapon;
                RightCollisionOffset = GetWeaponCollisionOffset( pWeapon->GetGuid(), FirePos );
                RightCollisionScalar = m_WeaponCollisionOffsetScalar;
            }

            if ( SingleCollisionScalar > LeftCollisionScalar )
            {
                if ( SingleCollisionScalar > RightCollisionScalar )
                {
                    m_WeaponCollisionOffset         = SingleCollisionOffset;
                    m_WeaponCollisionOffsetScalar   = SingleCollisionScalar;
                }
                else
                {
                    m_WeaponCollisionOffset         = RightCollisionOffset;
                    m_WeaponCollisionOffsetScalar   = RightCollisionScalar;
                }
            }
            else
            {
                if ( LeftCollisionScalar > RightCollisionScalar )
                {
                    m_WeaponCollisionOffset         = LeftCollisionOffset;
                    m_WeaponCollisionOffsetScalar   = LeftCollisionScalar;
                }
                else
                {
                    m_WeaponCollisionOffset         = RightCollisionOffset;
                    m_WeaponCollisionOffsetScalar   = RightCollisionScalar;
                }
            }
        }
        else
        {
            m_WeaponCollisionOffset.Zero();
        }
    }
    m_LastWeaponCollisionOffsetScalar = m_WeaponCollisionOffsetScalar;


}

void player::MoveAnimPlayer( const vector3& Pos )
{
    m_AnimPlayer.SetPosition( Pos + GetAnimPlayerOffset() );
}

void player::OnMove( const vector3& rNewPos )
{
    X_PROFILE_SCOPE_CATEGORY( "Context", "player::OnMove" );

    OnMoveViewPosition( rNewPos );

    //==========================================================================================
    // Begin code from ghost.cpp
    //==========================================================================================
    
    if( GetAttrBits() & object::ATTR_DESTROY )
        return;

    m_DeltaPos = rNewPos - GetPosition();

    // HACKOMOTRON - 
    //
    // Problem: When the ghost spawns or is created, he is created essentially 
    // at the origin and then moved to his starting point.  The above 
    // computation for m_DeltaPos results in a large vector which is, in turn,
    // used by the physics the first time the ghost runs his logic.  The poor
    // ghost then proceeds to collide with several walls.
    //
    // Proper solution:
    // (1) The ghost should not run physics or collision.
    // (2) Upon creation or spawning, the ghost should not attempt to travel
    //     long distances in the first frame following.
    //
    // HACK: If m_DeltaPos is too large, set it to 0.

    if( m_DeltaPos.LengthSquared() > (500*500) )  // 5 meters squared
        m_DeltaPos.Zero();

    actor::OnMove( rNewPos );

    m_Physics.SetPosition( rNewPos );

    //==========================================================================================
    // End code from ghost.cpp
    //==========================================================================================
}

void player::OnMoveViewPosition( const vector3& rNewPos )
{
    (void)rNewPos;
    vector3 EyesOffSet = m_EyesOffset;

    EyesOffSet.RotateY( m_Yaw );

    vector3 vViewPosition;
    f32 Height = GetBBox().Max.GetY();

    vViewPosition.Set( rNewPos.GetX(), Height, rNewPos.GetZ() );
    vViewPosition += EyesOffSet;

    MoveAnimPlayer( vViewPosition );
}

void player::OnTransform( const matrix4& L2W )
{
    //==========================================================================================
    // Begin code from ghost.cpp
    //==========================================================================================

    actor::OnTransform(L2W);

    //update physics
    m_Physics.SetPosition( L2W.GetTranslation() );

    //==========================================================================================
    // End code from ghost.cpp
    //==========================================================================================

    m_Yaw = L2W.GetRotation().Yaw;
    m_AnimPlayer.SetYaw( m_fCurrentYawOffset + m_Yaw );
    
    OnMoveViewPosition( L2W.GetTranslation() );
    MoveAnimPlayer( m_EyesPosition );
}

void player::OnRidingPlatformMove( const vector3& NewPos, radian DeltaYaw )
{
    (void)DeltaYaw;
    OnMove( NewPos );
}

void player::UpdateMovement( f32 DeltaTime )
{
#if defined( A51_ENABLE_OPENXR ) && defined(TARGET_ANDROID)
    static u32 MovementTraceFrame = 0;
    const xbool TraceVRMovement = IsVrAvatarMode() &&
                                  ( (++MovementTraceFrame % 90u) == 0u );
    if( TraceVRMovement )
    {
        __android_log_print( ANDROID_LOG_INFO, "A51VR",
            "movement input pad=%d f=%.3f s=%.3f dead=%d active=%d locked=%d cinema=%d",
            m_ActivePlayerPad, m_MoveInput.GamepadForward,
            m_MoveInput.GamepadStrafe, IsDead() ? 1 : 0,
            m_bActivePlayer ? 1 : 0, m_LockedView.IsActive() ? 1 : 0,
            m_CinemaController.IsActive() ? 1 : 0 );
    }
#endif
    if( !x_isvalid( DeltaTime ) || (DeltaTime < 0.0f) )
    {
        ASSERT( FALSE );
        m_DeltaPos.Zero();
        return;
    }

    if( DeltaTime <= F32_MIN )
    {
        m_DeltaPos.Zero();
        return;
    }

    if( m_LockedView.IsActive() ||
        (IsCinemaRunning() && (GetCinemaCameraGuid() != 0)) )
    {
        m_DeltaPos.Zero();
        return;
    }

    if( UpdateLadderMovement( DeltaTime ) )
    {
        m_Movement.Clear();
        return;
    }

    vector3 ViewX( 1.0f, 0.0f, 0.0f );
    vector3 ViewZ( 0.0f, 0.0f, 1.0f );
    ViewX.RotateY( m_Yaw );
    ViewZ.RotateY( m_Yaw );
#if defined( A51_ENABLE_OPENXR )
    if( IsVrAvatarMode() )
    {
        a51::xr::eye_view XREye{};
        if( g_XRSession.GetEyeView( 0, XREye ) )
        {
            quaternion HeadRotation( -XREye.Orientation[0],
                                      XREye.Orientation[1],
                                     -XREye.Orientation[2],
                                      XREye.Orientation[3] );
            HeadRotation.Normalize();

            matrix4 MovementToWorld;
            MovementToWorld.Identity();
            MovementToWorld.RotateY( m_Yaw );
            matrix4 HeadLocal;
            HeadLocal.Identity();
            HeadLocal.SetRotation( HeadRotation );
            MovementToWorld = MovementToWorld * HeadLocal;

            vector3 HeadForward =
                MovementToWorld.RotateVector( vector3( 0.0f, 0.0f, 1.0f ) );
            HeadForward.GetY() = 0.0f;
            if( HeadForward.SafeNormalize() )
            {
                ViewZ = HeadForward;
                ViewX.Set( HeadForward.GetZ(), 0.0f, -HeadForward.GetX() );
            }
        }
    }
#endif
    /* Flat movement basis: VR head pitch and roll never add vertical movement. */

    CalculateRigOffset( DeltaTime );
    EvaluateMovementSpeeds( DeltaTime );
    CalculateStrafeVelocity( ViewX );
    CalculateForwardVelocity( ViewZ );
    ApplyMovementSpeedFactors();

    vector3 MovementVelocity = m_StrafeVelocity + m_ForwardVelocity;
    if( m_bIsCrouching )
    {
        MovementVelocity *= 0.45f;
    }

    m_DeltaPos = MovementVelocity * DeltaTime;

#if defined( A51_ENABLE_OPENXR ) && defined(TARGET_ANDROID)
    if( TraceVRMovement )
    {
        __android_log_print( ANDROID_LOG_INFO, "A51VR",
            "movement output speed f=%.3f s=%.3f delta=%.3f,%.3f,%.3f yaw=%.3f",
            m_fForwardSpeed, m_fStrafeSpeed,
            m_DeltaPos.GetX(), m_DeltaPos.GetY(), m_DeltaPos.GetZ(), m_Yaw );
    }
#endif

    if( !m_bInTurret )
    {
        m_Physics.Advance( m_Physics.GetPosition() + m_DeltaPos, DeltaTime );
    }

    ASSERT( m_ArmsOffset.IsValid() );
    ASSERT( m_ArmsVelocity.IsValid() );

    vector3 ReturnForce( -m_ArmsOffset );
    ReturnForce *= s_ArmReturnForceMultiplier;
    m_ArmsVelocity += ReturnForce * DeltaTime;

    m_ArmsVelocity *= x_pow( 1.0f - s_ArmsDampen,
                             DeltaTime * s_MovementReferenceFrameRate );
    m_ArmsOffset += m_ArmsVelocity * DeltaTime;

    ASSERT( m_ArmsOffset.IsValid() );
    ASSERT( m_ArmsVelocity.IsValid() );

    UpdateArmsOffsetForLean( DeltaTime );
    OnMove( m_Physics.GetPosition() );
}

void player::UpdateCharacterRotation( const f32& DeltaTime )
{
    CalculateLookHorozOffset( DeltaTime );
    CalculateLookVertOffset( DeltaTime );


    //set the pitch and yaw of the rig.
    radian AnimPitch = -m_Pitch - m_CurrentVertRigOffset - m_ShakePitch;
    AnimPitch = MIN( R_89, MAX( -R_89, AnimPitch ) );

    m_AnimPlayer.SetYaw( m_Yaw + m_CurrentHorozRigOffset - m_ShakeYaw );
    m_AnimPlayer.SetPitch( AnimPitch );
    m_AnimPlayer.SetRoll( m_SoftLeanAmount * DEG_TO_RAD( GetTweakF32( "LeanMaxDegrees" ) ) );
}

void player::UpdateCrouchHeight( const f32& rDeltaTime )
{
    if ( m_bIsCrouching  )
    {
        //trying to crouch
        f32 NewCrouchFactor = MIN( 1.f ,  m_fCurrentCrouchFactor + m_fCrouchChangeRate * rDeltaTime );
        if ( m_Physics.SetCrouchParametric(
                 NewCrouchFactor, IsVrAvatarMode() ) )
        {
            m_fCurrentCrouchFactor = NewCrouchFactor;
        }
    }
    else
    if ( m_fCurrentCrouchFactor > 0.f )
    {
        f32 NewCrouchFactor = MAX( 0.f ,  m_fCurrentCrouchFactor - m_fCrouchChangeRate * rDeltaTime );
        if ( m_Physics.SetCrouchParametric( NewCrouchFactor ) )
        {
            m_fCurrentCrouchFactor = NewCrouchFactor;
        }
    }

}

vector3 player::GetPositionWithOffset( eOffsetPos offset )
{
    switch( offset ) 
    {
    case OFFSET_NONE:
        return GetPosition();
        break;
    case OFFSET_CENTER:
        return GetBBox().GetCenter();
        break;
    case OFFSET_AIM_AT:
        return GetBBox().GetCenter() + GetLeanOffset();
        break;
    case OFFSET_EYES:
        return GetEyesPosition();
        break;
    case OFFSET_TOP_OF_BBOX:
        return GetPosition() + vector3( 0.0f, GetBBox().Max.GetY(), 0.0f );
        break;
    default:
        return GetPosition();
        break;
    }
}

const matrix4& player::GetL2W( void ) const
{
    matrix4& L2W = *(matrix4*)(&actor::GetL2W()); // de-constification
    const vector3 Pos( GetPosition() );
    L2W.Identity();
    L2W.RotateY( m_Yaw );
    L2W.Translate( Pos );
    return L2W;
}

bbox player::GetLocalBBox( void ) const
{
    bbox BBox = m_Physics.GetBBox();

    // Take lean into account so leaning ghosts/players can be hit in MP
    f32  LeanDist = x_abs( GetLeanAmount() * 100.0f );
    BBox.Inflate( LeanDist, 0.0f, LeanDist );

    return BBox;
}

bbox player::GetColBBox( void )
{
    // Start with physics bbox
    bbox BBox = m_Physics.GetBBox();

    // Take lean into account so leaning ghosts/players can be hit in MP
    f32  LeanDist = x_abs( GetLeanAmount() * 100.0f );
    BBox.Inflate( LeanDist, 0.0f, LeanDist );

    // Convert into world space
    BBox.Transform( GetL2W() );
    return BBox;
}

void player::OnColCheck( void )
{
#ifndef X_EDITOR
    // For multi-player use bone bboxes so call base class
    if( GameMgr.IsGameMultiplayer() )
    {
        actor::OnColCheck();
        return;
    }
#endif

    vector3 Pos = GetPosition();
    vector3 Offset  ( GetLeanOffset() );
    Offset.GetY() = 0.0f;
    Pos += Offset;

    vector3 SpherePos[16];
    s32     nSpheres;
    s32     i;

    g_CollisionMgr.StartApply( GetGuid() );

    nSpheres = g_CollisionMgr.GetCylinderSpherePositions(  
        Pos,
        Pos + vector3(0,m_Physics.GetColHeight(),0),
        m_Physics.GetColRadius(),
        SpherePos,
        object::MAT_TYPE_FLESH );

    for( i=0; i<nSpheres; i++ )
    {
        g_CollisionMgr.ApplySphere( SpherePos[i], m_Physics.GetColRadius(), object::MAT_TYPE_FLESH );
    }

    g_CollisionMgr.EndApply();
}

void player::UpdateLean( f32 LeanValue, f32 DeltaTime )
{
    const lean_state OldLeanState      = m_LeanState;
    const f32        OldLeanAmount     = m_LeanAmount;
    const f32        OldSoftLeanAmount = m_SoftLeanAmount;

    if ( (LeanValue > GetTweakF32( "LeanThreshold" )) && (m_LeanAmount >= 0.0f) )
    {
        // Leaning left
        const f32 ElapsedTime       = m_LeanAmount * GetTweakF32( "LeanTime" );
        const f32 NewElapsedTime    = ElapsedTime + DeltaTime;
        m_LeanAmount                = NewElapsedTime / GetTweakF32( "LeanTime" );
        m_LeanState                 = LEAN_LEFT;
    }
    else if ( (LeanValue < -GetTweakF32( "LeanThreshold" )) && (m_LeanAmount <= 0.0f) )
    {
        // Leaning right
        const f32 ElapsedTime       = (-m_LeanAmount) * GetTweakF32( "LeanTime" );
        const f32 NewElapsedTime    = ElapsedTime + DeltaTime;
        m_LeanAmount                = -(NewElapsedTime / GetTweakF32( "LeanTime" ));
        m_LeanState                 = LEAN_RIGHT;
    }
    else
    {
        // Returning to upright
        if ( m_LeanAmount > 0.0f )
        {
            // from left
            const f32 ElapsedTime       = m_LeanAmount * GetTweakF32( "LeanTime" );
            const f32 NewElapsedTime    = ElapsedTime - DeltaTime;
            m_LeanAmount                = NewElapsedTime / GetTweakF32( "LeanTime" );
            m_LeanAmount                = MAX( 0.0f, m_LeanAmount );
            m_LeanState                 = LEAN_RETURN_FROM_LEFT;
        }
        else if ( m_LeanAmount < 0.0f )
        {
            // from right
            const f32 ElapsedTime       = (-m_LeanAmount) * GetTweakF32( "LeanTime" );
            const f32 NewElapsedTime    = ElapsedTime - DeltaTime;
            m_LeanAmount                = -(NewElapsedTime / GetTweakF32( "LeanTime" ));
            m_LeanAmount                = MIN( 0.0f, m_LeanAmount );
            m_LeanState                 = LEAN_RETURN_FROM_RIGHT;
        }
        else
        {
            m_LeanState = LEAN_NONE;
        }

    }
    m_LeanAmount = MAX( -1.0f, m_LeanAmount );
    m_LeanAmount = MIN( 1.0f, m_LeanAmount );

    m_SoftLeanAmount = ComputeSoftLean( m_LeanAmount );

    // check to make sure we aren't violating any collision
    if ( (m_LeanState == LEAN_RIGHT) || (m_LeanState == LEAN_LEFT) )
    {
        // New position
        vector3 StartPos( GetPosition() );
        vector3 Offset  ( GetAnimPlayerOffset() );
        Offset.GetY() = 0.0f;
        vector3 EndPos  ( GetPosition() + Offset );

        //
        // We need to elevate our collision check to account for uneven floors
        // that cause us to collide at our feet, when we're mostly just
        // concerned about the player's head and weapon
        //
        static const f32 TweakLeanElevate = 10.0f;
        StartPos.GetY() += TweakLeanElevate;
        EndPos.GetY() += TweakLeanElevate;

        const vector3 Delta( EndPos - StartPos );

        if ( Delta.LengthSquared() > ZERO )
        {
            m_Physics.SetupPlayerCollisionCheck( StartPos, EndPos );
            g_CollisionMgr.CheckCollisions( object::TYPE_ALL_TYPES, 
                                            object::ATTR_BLOCKS_PLAYER_LOS, 
                                            object::ATTR_COLLISION_PERMEABLE | object::ATTR_LIVING );

            if ( g_CollisionMgr.m_nCollisions > 0 )
            {
                vector3 StopPos = StartPos + (Delta * g_CollisionMgr.m_Collisions[0].T);
                m_SoftLeanAmount = ComputeLeanValueForPosition( StartPos, StopPos, m_LeanState == LEAN_RIGHT );
                m_LeanAmount = ComputeLean( m_SoftLeanAmount );
            }
        }
    }

#ifndef X_EDITOR
    // Send across net?    
    if(     ( OldLeanState      != m_LeanState      )
        ||  ( OldLeanAmount     != m_LeanAmount     )
        ||  ( OldSoftLeanAmount != m_SoftLeanAmount ) )
    {
        m_NetDirtyBits |= LEAN_BIT;  // NETWORK
    }        
#endif // X_EDITOR
    
}

vector3 player::GetLeanOffset( void )
{
    vector3 Forward ( radian3( 0.0f, m_Yaw, 0.0f ) );
    vector3 Lean    ( 0.0f, 0.0f, 0.0f );

    if ( m_SoftLeanAmount != 0.0f )
    {
        Lean = Forward;
        Lean.RotateY( R_90 );
        Lean *= (m_SoftLeanAmount * GetTweakF32( "LeanX" )); // Scale according to our horiz offset

        f32 Vertical = (PI / 2) * x_abs( m_SoftLeanAmount) * GetTweakF32( "LeanY" );

        Lean += vector3( 0.0f, -Vertical, 0.0f );
    }

    return Lean;
}

void player::UpdateArmsOffsetForLean( f32 DeltaTime )
{
    new_weapon* pWeapon = GetCurrentWeaponPtr();
    if ( pWeapon )
    {
        //const bbox BBox( pWeapon->GetBBox() );
        const vector3 CenterPos( pWeapon->GetBBox().GetCenter() );
        radian        Pitch, Yaw;
        GetSimulationView().GetPitchYaw( Pitch, Yaw );
        const vector3 LookDir( radian3( Pitch, Yaw, 0.0f ) );
        const vector3 ViewPos(GetSimulationView().GetPosition() - (LookDir * 10000.0f) );
        const vector3 ViewEnd( ViewPos + (LookDir * 10000.0f) );
        vector3 Closest( CenterPos.GetClosestPToLSeg( ViewPos, ViewEnd ) );
        const f32 DistanceThisFrame = GetTweakF32( "LeanWeaponOffsetSpeed" ) * DeltaTime;

        switch ( m_LeanState )
        {
        case LEAN_LEFT:
        case LEAN_RIGHT:
            {
                // Move the rig under our look direction
                vector3 Dir = Closest - CenterPos;
                const f32 TotalDistanceSquared = Dir.LengthSquared();
                if ( x_sqr( DistanceThisFrame ) >= TotalDistanceSquared )
                {
                    m_LeanWeaponOffset += Dir; // Just go the remaining distance
                }
                else
                {
                    Dir.Normalize();
                    Dir *= (m_LeanWeaponOffset.Length() + DistanceThisFrame);
                    m_LeanWeaponOffset = Dir;
                }
            }
            break;

        case LEAN_NONE:
        case LEAN_RETURN_FROM_LEFT:
        case LEAN_RETURN_FROM_RIGHT:
            {
                vector3 Dir = -m_LeanWeaponOffset;
                // Move the rig back
                if ( x_sqr( DistanceThisFrame ) > Dir.LengthSquared() )
                {
                    m_LeanWeaponOffset.Zero();
                }
                else
                {
                    Dir.Normalize();
                    Dir *= DistanceThisFrame;
                    m_LeanWeaponOffset += Dir;
                }
            }
            break;
        default:
            ASSERTS( FALSE, xfs( "Invalid lean_state: %d", m_LeanState ) );
        }
    }
    else
    {
        m_LeanWeaponOffset.Zero();
    }
    
    ASSERT( m_LeanWeaponOffset.IsValid() );
}

vector3 player::GetAnimPlayerOffset( void )
{
    return m_ArmsOffset + GetLeanOffset() + m_LeanWeaponOffset;
}

void player::ManTurret( guid TurretGuid, 
                        guid Turret2Guid, 
                        guid Turret3Guid, 
                        guid AnchorGuid,
                        guid LeftBoundaryGuid,
                        guid RightBoundaryGuid,
                        guid UpperBoundaryGuid,
                        guid LowerBoundaryGuid )
{
    LOG_MESSAGE( "Turret", "Man Turret" );

    if ( m_bIsMutated )
    {
        SetupMutationChange( FALSE );
        m_bIsMutantVisionOn = FALSE;
        ForceNextWeapon();
    }

    m_Turret.PreviousWeapon = m_CurrentWeaponItem;
    SetNextWeapon2( INVEN_WEAPON_TRA );

    m_Turret.TurretGuid         = TurretGuid;
    m_Turret.Turret2Guid        = Turret2Guid;
    m_Turret.Turret3Guid        = Turret3Guid;
    m_Turret.PreviousL2W        = GetL2W();
    m_Turret.PreviousZone1      = m_ZoneTracker.GetMainZone();
    m_Turret.PreviousZone2      = m_ZoneTracker.GetZone2();
    m_Turret.PreviousWeapon     = m_CurrentWeaponItem;
    m_Turret.LeftBoundaryGuid   = LeftBoundaryGuid;
    m_Turret.RightBoundaryGuid  = RightBoundaryGuid;
    m_Turret.UpperBoundaryGuid  = UpperBoundaryGuid;
    m_Turret.LowerBoundaryGuid  = LowerBoundaryGuid;

    // Hide the turret and parts
    turret* pTurret = (turret*)g_ObjMgr.GetObjectByGuid( TurretGuid );
    ASSERT( pTurret->IsKindOf( turret::GetRTTI() ) );
    if ( pTurret )
    {
        pTurret->Hide();
    }
    HideObject( g_ObjMgr.GetObjectByGuid( Turret2Guid ) );
    HideObject( g_ObjMgr.GetObjectByGuid( Turret3Guid ) );

    object* pAnchor = g_ObjMgr.GetObjectByGuid( AnchorGuid );
    if ( pAnchor )
    {
        m_Turret.AnchorL2W = pAnchor->GetL2W();
        const radian3 Rotation( m_Turret.AnchorL2W.GetRotation() );
        Teleport( m_Turret.AnchorL2W.GetTranslation(),
                  Rotation.Pitch,
                  Rotation.Yaw,
                  static_cast<zone_mgr::zone_id>( pAnchor->GetZone1() ),
                  static_cast<zone_mgr::zone_id>( pAnchor->GetZone2() ),
                  PlayerTeleportVelocityPolicy::Clear );
    }
    else
    {
        m_Turret.AnchorL2W.Identity();
    }

    SetIsCrouching( FALSE );
    m_bInTurret  = TRUE;
}

void player::ExitTurret( void )
{
    if ( m_bInTurret )
    {
        LOG_MESSAGE( "Turret", "Exit Turret" );

        const radian3 Rotation( m_Turret.PreviousL2W.GetRotation() );
        Teleport( m_Turret.PreviousL2W.GetTranslation(),
                  Rotation.Pitch,
                  Rotation.Yaw,
                  m_Turret.PreviousZone1,
                  m_Turret.PreviousZone2,
                  PlayerTeleportVelocityPolicy::Clear );
        loco* pLoco = GetLocoPointer();
        if( pLoco )
            pLoco->m_Physics.SetVelocity( vector3(0.0f,0.0f,0.0f) );
        SwitchWeapon2( m_Turret.PreviousWeapon );
        m_Inventory2.RemoveAmount( INVEN_WEAPON_TRA, 1 );
        m_bInTurret  = FALSE;

        // unhide the turret and parts
        turret* pTurret = (turret*)g_ObjMgr.GetObjectByGuid( m_Turret.TurretGuid );
        ASSERT( pTurret->IsKindOf( turret::GetRTTI() ) );
        if ( pTurret )
        {
            pTurret->Unhide();
        }
        UnhideObject( g_ObjMgr.GetObjectByGuid( m_Turret.Turret2Guid ) );
        UnhideObject( g_ObjMgr.GetObjectByGuid( m_Turret.Turret3Guid ) );

    }
}

void player::Teleport( const vector3& Position,
                       zone_mgr::zone_id Zone1,
                       zone_mgr::zone_id Zone2,
                       PlayerTeleportVelocityPolicy VelocityPolicy,
                       xbool DoBlend,
                       xbool DoEffect )
{
    Teleport( Position,
              GetPitch(),
              GetYaw(),
              Zone1,
              Zone2,
              VelocityPolicy,
              DoBlend,
              DoEffect );
}

//==============================================================================

void player::Teleport( const vector3& Position,
                       radian Pitch,
                       radian Yaw,
                       zone_mgr::zone_id Zone1,
                       zone_mgr::zone_id Zone2,
                       PlayerTeleportVelocityPolicy VelocityPolicy,
                       xbool DoBlend,
                       xbool DoEffect )
{
    vector3 Velocity = m_Physics.GetVelocity();
    if( VelocityPolicy == PlayerTeleportVelocityPolicy::Keep )
    {
        radian const DeltaYaw = x_MinAngleDiff( Yaw, GetYaw() );
        Velocity.RotateY( DeltaYaw );
    }
    else
    {
        Velocity.Zero();
        m_ForwardVelocity.Zero();
        m_StrafeVelocity.Zero();
        m_Movement.Clear();
    }

    if( !DoBlend )
    {
        extern void ActivateSoundEmitters( const vector3& Position );
        ActivateSoundEmitters( Position );
    }

    // When the player (not actor or ghost) teleports and gets a new pitch/yaw,
    // we need to clear his network targeting information just to be safe.
    m_TargetNetSlot = -1;

    m_bApplyingTeleport = TRUE;
    actor::Teleport( Position, DoBlend, DoEffect );
    SetPitch( Pitch );
    SetYaw( Yaw );
    m_bApplyingTeleport = FALSE;
    m_DeltaPos.Zero();

    m_Physics.InitialGroundCheck( Position );
    g_ZoneMgr.RebaseZoneTracking( *this,
                                  m_ZoneTracker,
                                  Position,
                                  Zone1,
                                  Zone2,
                                  zone_mgr::SeedSource::Destination );

    if( !m_RenderViewZoneUsesCameraSeed &&
        !m_ViewController.IsBlending() )
    {
        g_ZoneMgr.RebaseZoneTracking( m_RenderViewZoneTracker,
                                      GetDefaultViewPos(),
                                      Zone1,
                                      Zone2,
                                      zone_mgr::SeedSource::Destination );
        m_RenderViewZoneInitialized = TRUE;
        m_RenderViewZoneSourceGuid = NULL_GUID;
        m_RebaseRenderViewZone = FALSE;
    }
    ResetFallStateAfterDiscontinuity();
    m_Physics.ZeroVelocity();
    if( VelocityPolicy == PlayerTeleportVelocityPolicy::Keep )
    {
        m_Physics.AddVelocity( Velocity );
    }
    SetIsAirborn( m_Physics.IsAirborn() );

    if( !DoBlend )
    {
        // Update ears
        matrix4 W2V;
        W2V.Identity();
        W2V.RotateX( Pitch );
        W2V.RotateY( Yaw );
        W2V.Translate( Position + vector3(0,180,0) );
        W2V.InvertRT();
        g_AudioMgr.SetEar( m_AudioEarID, W2V, Position, GetZone1(), 1.0f );

        // Make sure 1st person hands are in sync
        radian AnimPitch = -m_Pitch - m_CurrentVertRigOffset - m_ShakePitch;
        AnimPitch = MIN( R_89, MAX( -R_89, AnimPitch ) );

        m_AnimPlayer.SetYaw( m_Yaw + m_CurrentHorozRigOffset - m_ShakeYaw );
        m_AnimPlayer.SetPitch( AnimPitch );
    }

    // Make sure 1st person weapon is in sync
    AttachWeapon();
}

xbool player::IsAvatar( void )
{
    xbool bRenderAvatar = IsVrAvatarMode()
                          || !m_bActivePlayer
                          || (m_CurrentAnimState == ANIM_STATE_FALLING_TO_DEATH)
                          || (m_LocalSlot == -1);

#if defined(X_EDITOR)
    const view* ActiveView = eng_GetView();
    if ( ActiveView && ((ActiveView->GetPosition() - GetRenderView().GetPosition()).LengthSquared() > 0.5f) )
    {
        bRenderAvatar = TRUE;
    }
#endif // X_EDITOR

    return bRenderAvatar;
}

xbool player::IsVrAvatarMode( void ) const
{
#if defined( A51_ENABLE_OPENXR ) && !defined( X_EDITOR )
    return g_XRGameFrameBridge && m_bActivePlayer &&
           !GameMgr.IsGameMultiplayer();
#else
    return FALSE;
#endif
}

xbool player::UsingLoco( void )
{
#if defined( X_EDITOR )
    return( FALSE );
#else
    if( IsVrAvatarMode() )
        return TRUE;

    // SB: Use loco so skin geom is setup for bbox collision detection in MP
    return ( GameMgr.IsGameMultiplayer() );
#endif
}

static f32 ComputeSoftLean( f32 LeanAmount )
{
    LeanAmount = MAX( -1.0f, LeanAmount );
    LeanAmount = MIN(  1.0f, LeanAmount );

    const f32 Sign      = (LeanAmount > 0.0f) ? 1.0f : (LeanAmount < 0.0f) ? -1.0f : 0;
    const f32 Period    = x_abs( LeanAmount ) * PI;
    const f32 Phase     = PI / 2.0f;

    return( Sign * (1.0f - ((x_sin( Period + Phase ) + 1.0f) / 2.0f)) );
}

static f32 ComputeLean( f32 SoftLeanAmount )
{
    SoftLeanAmount = MAX( -1.0f, SoftLeanAmount );
    SoftLeanAmount = MIN(  1.0f, SoftLeanAmount );

    const f32 LeanAmount = x_abs( (x_asin( ((x_abs( SoftLeanAmount ) - 1.0f) * -2.0f) - 1.0f ) - (PI / 2.0f)) / PI );
    const f32 Sign = (SoftLeanAmount >= 0.0f) ? 1.0f : -1.0f;

    return Sign * LeanAmount;
}

static f32 ComputeLeanValueForPosition( const vector3& Start, const vector3& HitPoint, xbool bLeaningRight )
{
    vector3 Lean( HitPoint - Start );
    Lean.GetY() = 0.0f;

    f32 LeanAmount = (Lean / GetTweakF32( "LeanX" )).Length();
    if ( bLeaningRight )
    {
        LeanAmount *= -1.0f;
    }

    return LeanAmount;
}

static void HideObject( object* pObj )
{
    if ( pObj )
    {
        vector3 Pos( pObj->GetPosition() );
        Pos.GetY() -= 10000.0f;
        pObj->OnMove( Pos );
    }
}

static void UnhideObject( object* pObj )
{
    if ( pObj )
    {
        vector3 Pos( pObj->GetPosition() );
        Pos.GetY() += 10000.0f;
        pObj->OnMove( Pos );
    }
}

//=========================================================================

void player::UpdateGhostLoco( f32 DeltaTime )
{
    if( m_bDead || !UsingLoco() )
    {
        return;
    }

    m_pLoco->SetGhostIsMoving( m_Physics.GetVelocity().LengthSquared() > x_sqr( 0.01f ) );
    m_pLoco->SetPitch( m_Pitch );
    radian BodyYaw = m_Yaw;
#if defined( A51_ENABLE_OPENXR )
    if( IsVrAvatarMode() )
    {
        for( u32 Hand = 0; Hand < 2; ++Hand )
        {
            f32 Grip = 0.0f;
            f32 Trigger = 0.0f;
            g_XRSession.GetControllerFingerInput( Hand, Grip, Trigger );
            m_VrGripAmount[Hand] = Grip;
            m_VrTriggerAmount[Hand] = Trigger;
            m_Loco.m_Player.SetVrFingerInput( Hand, Grip, Trigger );
        }
        a51::xr::eye_view XREye{};
        if( g_XRSession.GetEyeView( 0, XREye ) )
        {
            quaternion HeadRotation( -XREye.Orientation[0],
                                      XREye.Orientation[1],
                                     -XREye.Orientation[2],
                                      XREye.Orientation[3] );
            HeadRotation.Normalize();

            matrix4 HeadLocal;
            HeadLocal.Identity();
            HeadLocal.SetRotation( HeadRotation );
            BodyYaw += HeadLocal.GetRotation().Yaw;
        }
    }
#endif
    /* The rendered headset view is body yaw plus this relative head yaw.
     * Give the same world heading to the avatar locomotion so the soldier
     * turns with the player when they look around in VR. */
    m_pLoco->SetYaw( BodyYaw );
    m_Loco.m_Player.SetVrBodyYaw( BodyYaw );
    OnAdvanceGhostLogic( DeltaTime );
#if defined( A51_ENABLE_OPENXR )
    if( IsVrAvatarMode() )
        UpdateVrWeapons( DeltaTime );
#endif
}

//==========================================================================

void player::UpdateVrWeapons( f32 DeltaTime )
{
#if defined( A51_ENABLE_OPENXR )
    if( !IsVrAvatarMode() || !m_Loco.IsAnimLoaded() || IsDead() )
        return;

    matrix4 HandTransforms[2];
    xbool HandValid[2] = { FALSE, FALSE };
    for( s32 Hand = 0; Hand < 2; ++Hand )
    {
        HandValid[Hand] = GetVrHandTransform( Hand, HandTransforms[Hand] );
        if( !HandValid[Hand] )
        {
            m_VrControllerPositionValid[Hand] = FALSE;
            m_VrControllerVelocity[Hand].Zero();
            continue;
        }

        const vector3 Position = HandTransforms[Hand].GetTranslation();
        if( m_VrControllerPositionValid[Hand] && ( DeltaTime > 0.0001f ) )
        {
            m_VrControllerVelocity[Hand] =
                ( Position - m_VrPreviousControllerPosition[Hand] ) / DeltaTime;
            const f32 Speed = m_VrControllerVelocity[Hand].Length();
            if( Speed > 1200.0f )
                m_VrControllerVelocity[Hand] *= 1200.0f / Speed;
        }
        else
        {
            m_VrControllerVelocity[Hand].Zero();
        }
        m_VrPreviousControllerPosition[Hand] = Position;
        m_VrControllerPositionValid[Hand] = TRUE;
    }

    const s32 RootBone = m_Loco.m_Player.GetBoneIndex( "B_01_Root" );
    if( RootBone < 0 )
        return;

    const f32 AvatarCrouchOffset = m_fCurrentCrouchFactor * 70.0f;
    matrix4 Body = m_Loco.m_Player.GetBoneL2W( RootBone );
    Body.ClearTranslation();
    Body.ClearScale();

    const vector3 HolsterOffsets[6] =
    {
        vector3( -24.0f, 82.0f, -12.0f ),
        vector3(  24.0f, 82.0f, -12.0f ),
        vector3(  26.0f, 135.0f, -42.0f ),
        vector3(  26.0f, 90.0f,   8.0f ),
        vector3( -16.0f, 70.0f, -20.0f ),
        vector3(  16.0f, 70.0f, -20.0f )
    };
    const radian HolsterYaw[2] = { R_90, -R_90 };

    /* Releasing a weapon near its belt socket is the deliberate holster
     * gesture. Otherwise it gets a short physical drop before returning. */
    for( s32 Hand = 0; Hand < 2; ++Hand )
    {
        if( m_VrHeldWeapon[Hand] == INVEN_NULL ||
            ( m_VrGripAmount[Hand] > 0.45f ) )
            continue;

        const inven_item Item = m_VrHeldWeapon[Hand];
        const s32 Index = inventory2::ItemToWeaponIndex( Item );
        new_weapon* pHeldWeapon = GetWeaponPtr( Item );
        if( pHeldWeapon )
        {
            pHeldWeapon->EndPrimaryFire();
            pHeldWeapon->EndAltFire();
            pHeldWeapon->SetRenderState( new_weapon::RENDER_STATE_NPC );
        }
        if( Index >= 0 && Index < INVEN_NUM_WEAPONS )
        {
            vr_weapon_runtime& Runtime = m_VrWeaponRuntime[Index];
            matrix4 Socket = Body;
            matrix4 LocalYaw;
            LocalYaw.Setup( vector3( 0.0f, 1.0f, 0.0f ),
                            HolsterYaw[Index & 1] );
            Socket = Body * LocalYaw;
            // Holster offsets are measured from the player's floor origin.
            // The animated root is already at hip height; using it here
            // adds the belt height a second time.
            vector3 SocketBase = GetPosition();
            SocketBase.GetY() -= AvatarCrouchOffset;
            const vector3 SocketPos =
                SocketBase +
                Body.RotateVector( HolsterOffsets[Index % 6] );
            Socket.SetTranslation( SocketPos );

            if( ( Runtime.Transform.GetTranslation() - SocketPos ).LengthSquared()
                <= x_sqr( 34.0f ) )
            {
                Runtime.State = VR_WEAPON_HOLSTERED;
                Runtime.Transform = Socket;
                Runtime.Velocity.Zero();
            }
            else
            {
                Runtime.State = VR_WEAPON_DROPPED;
                Runtime.ReturnTimer = 2.0f;
                Runtime.Velocity = m_VrControllerVelocity[Hand] * 0.8f +
                                   GetVelocity() * 0.25f;
            }
            Runtime.Hand = -1;
        }
        m_VrHeldWeapon[Hand] = INVEN_NULL;
    }

    /* A world pickup passes its item and grabbing hand through OnPickup.
     * Initialize that weapon directly in the controller pose so the legacy
     * auto-switch/pickup animation cannot pull it into the flat-view hand. */
    if( m_VrPendingPickupWeapon != INVEN_NULL &&
        m_VrPendingPickupHand >= 0 && m_VrPendingPickupHand < 2 )
    {
        inven_item Item = m_VrPendingPickupWeapon;
        if( ( Item == INVEN_WEAPON_SMP ) &&
            m_Inventory2.HasItem( INVEN_WEAPON_DUAL_SMP ) )
            Item = INVEN_WEAPON_DUAL_SMP;
        else if( ( Item == INVEN_WEAPON_SHOTGUN ) &&
                 m_Inventory2.HasItem( INVEN_WEAPON_DUAL_SHT ) )
            Item = INVEN_WEAPON_DUAL_SHT;

        const s32 Index = inventory2::ItemToWeaponIndex( Item );
        if( Index >= 0 && Index < INVEN_NUM_WEAPONS &&
            m_Inventory2.HasItem( Item ) && GetWeaponPtr( Item ) )
        {
            vr_weapon_runtime& Runtime = m_VrWeaponRuntime[Index];
            Runtime.State = VR_WEAPON_HELD;
            Runtime.Hand = m_VrPendingPickupHand;
            Runtime.HandGripOffsetInitialized = FALSE;
            Runtime.Velocity.Zero();
            Runtime.ReturnTimer = 0.0f;
            Runtime.Initialized = TRUE;
            m_VrHeldWeapon[m_VrPendingPickupHand] = Item;

            /* A world pickup is not equipped by TakePickup in VR: automatic
             * weapon switching is deliberately disabled there. Make the
             * physically grabbed weapon the active weapon immediately so
             * firing, the barrel direction and the debug ray all resolve
             * through the same held weapon on its very first pickup. */
            m_PrevWeaponItem = m_CurrentWeaponItem;
            m_CurrentWeaponItem = Item;
            m_NextWeaponItem = INVEN_NULL;

            new_weapon* pPickedWeapon = GetWeaponPtr( Item );
            if( pPickedWeapon )
            {
                pPickedWeapon->SetupRenderInformation();
                pPickedWeapon->SetVisible( TRUE );
                pPickedWeapon->SetRenderState(
                    new_weapon::RENDER_STATE_PLAYER );
                pPickedWeapon->ClearZoom();
            }
            SetAnimState( ANIM_STATE_IDLE );
        }

        m_VrPendingPickupWeapon = INVEN_NULL;
        m_VrPendingPickupHand = -1;
    }

    /* Grab the nearest owned weapon when a grip crosses its press threshold.
     * Only one gun is active in the legacy firing model at a time. */
    for( s32 Hand = 0; Hand < 2; ++Hand )
    {
        const xbool bGripPressed = ( m_VrGripAmount[Hand] >= 0.70f ) &&
                                   ( m_VrPreviousGrip[Hand] < 0.70f );
        if( !bGripPressed || !HandValid[Hand] ||
            ( m_VrHeldWeapon[0] != INVEN_NULL ) ||
            ( m_VrHeldWeapon[1] != INVEN_NULL ) )
            continue;

        /* Pickups normally activate from the player's body collision. In VR
         * that can happen before the player squeezes, so explicitly look for
         * a nearby weapon when grip is pressed. This also lets the hand reach
         * the actual model instead of requiring the pickup origin to be
         * almost exactly inside the palm. */
        pickup* pBestPickup = NULL;
        f32 BestPickupDistanceSqr = x_sqr( 90.0f );
        const vector3 HandPos = HandTransforms[Hand].GetTranslation();
        for( slot_id Slot = g_ObjMgr.GetFirst( object::TYPE_PICKUP );
             Slot != SLOT_NULL; Slot = g_ObjMgr.GetNext( Slot ) )
        {
            object* pObject = g_ObjMgr.GetObjectBySlot( Slot );
            if( !pObject || !pObject->IsKindOf( pickup::GetRTTI() ) )
                continue;

            pickup& Candidate = pickup::GetSafeType( *pObject );
            const inven_item Item = Candidate.GetItem();
            if( !Candidate.GetTakeable() ||
                !IN_RANGE( INVEN_WEAPON_FIRST, Item, INVEN_WEAPON_LAST ) ||
                Item == INVEN_WEAPON_MUTATION )
                continue;

            const f32 DistanceSqr =
                ( Candidate.GetPosition() - HandPos ).LengthSquared();
            if( DistanceSqr < BestPickupDistanceSqr )
            {
                BestPickupDistanceSqr = DistanceSqr;
                pBestPickup = &Candidate;
            }
        }

        if( pBestPickup && OnPickup( *pBestPickup ) )
        {
            pBestPickup->CompleteVrPickup( *this );
            continue;
        }

        inven_item BestItem = INVEN_NULL;
        f32 BestDistanceSqr = x_sqr( 48.0f );
        for( s32 WeaponIndex = 0; WeaponIndex < INVEN_NUM_WEAPONS; ++WeaponIndex )
        {
            const inven_item Item = inventory2::WeaponIndexToItem( WeaponIndex );
            if( Item == INVEN_NULL || Item == INVEN_WEAPON_MUTATION ||
                !m_Inventory2.HasItem( Item ) || !GetWeaponPtr( Item ) )
                continue;

            vr_weapon_runtime& Runtime = m_VrWeaponRuntime[WeaponIndex];
            if( ( Runtime.State != VR_WEAPON_HOLSTERED ) &&
                ( Runtime.State != VR_WEAPON_DROPPED ) )
                continue;

            const f32 DistanceSqr =
                ( Runtime.Transform.GetTranslation() - HandPos ).LengthSquared();
            if( DistanceSqr < BestDistanceSqr )
            {
                BestDistanceSqr = DistanceSqr;
                BestItem = Item;
            }
        }

        if( BestItem != INVEN_NULL )
        {
            const s32 Index = inventory2::ItemToWeaponIndex( BestItem );
            vr_weapon_runtime& Runtime = m_VrWeaponRuntime[Index];
            Runtime.State = VR_WEAPON_HELD;
            Runtime.Hand = Hand;
            Runtime.HandGripOffsetInitialized = FALSE;
            Runtime.Velocity.Zero();
            Runtime.ReturnTimer = 0.0f;
            m_VrHeldWeapon[Hand] = BestItem;
            GetWeaponPtr( BestItem )->SetRenderState(
                new_weapon::RENDER_STATE_PLAYER );
            if( BestItem != m_CurrentWeaponItem )
            {
                SetNextWeapon2( BestItem, TRUE, FALSE );
                SetAnimState( ANIM_STATE_PICKUP );
            }
        }
    }

    for( s32 WeaponIndex = 0; WeaponIndex < INVEN_NUM_WEAPONS; ++WeaponIndex )
    {
        const inven_item Item = inventory2::WeaponIndexToItem( WeaponIndex );
        if( Item == INVEN_NULL )
            continue;

        new_weapon* pWeapon = GetWeaponPtr( Item );
        if( !pWeapon )
            continue;

        vr_weapon_runtime& Runtime = m_VrWeaponRuntime[WeaponIndex];
        if( !m_Inventory2.HasItem( Item ) || Item == INVEN_WEAPON_MUTATION )
        {
            pWeapon->SetVisible( FALSE );
            Runtime.Initialized = FALSE;
            continue;
        }

        matrix4 Socket = Body;
        matrix4 LocalYaw;
        LocalYaw.Setup( vector3( 0.0f, 1.0f, 0.0f ),
                        HolsterYaw[WeaponIndex & 1] );
        Socket = Body * LocalYaw;
        vector3 SocketBase = GetPosition();
        SocketBase.GetY() -= AvatarCrouchOffset;
        const vector3 SocketPos =
            SocketBase +
            Body.RotateVector( HolsterOffsets[WeaponIndex % 6] );
        Socket.SetTranslation( SocketPos );

        if( !Runtime.Initialized )
        {
            Runtime.State = VR_WEAPON_HOLSTERED;
            Runtime.Transform = Socket;
            Runtime.Initialized = TRUE;
        }

        switch( Runtime.State )
        {
        case VR_WEAPON_HOLSTERED:
            Runtime.Transform = Socket;
            break;

        case VR_WEAPON_HELD:
            if( ( Runtime.Hand >= 0 ) && HandValid[Runtime.Hand] )
            {
                Runtime.Transform = HandTransforms[Runtime.Hand];
            }
            break;

        case VR_WEAPON_DROPPED:
        {
            const vector3 OldPos = Runtime.Transform.GetTranslation();
            Runtime.Velocity.GetY() -= 980.0f * DeltaTime;
            const vector3 NewPos = OldPos + Runtime.Velocity * DeltaTime;
            const f32 Travel = ( NewPos - OldPos ).Length();
            if( Travel > 0.001f )
            {
                g_CollisionMgr.UseLowPoly();
                g_CollisionMgr.SphereSetup( GetGuid(), OldPos, NewPos, 7.0f );
                if( g_CollisionMgr.CheckCollisions(
                        object::TYPE_ALL_TYPES,
                        (object::object_attr)( object::ATTR_BLOCKS_CHARACTER |
                                               object::ATTR_BLOCKS_LARGE_PROJECTILES ),
                        object::ATTR_COLLISION_PERMEABLE ) &&
                    g_CollisionMgr.m_nCollisions > 0 )
                {
                    const collision_mgr::collision& Collision =
                        g_CollisionMgr.m_Collisions[0];
                    const f32 HitT = x_clamp( Collision.T - 0.01f,
                                              0.0f, 1.0f );
                    Runtime.Transform.SetTranslation(
                        OldPos + ( NewPos - OldPos ) * HitT );
                    const f32 NormalSpeed =
                        v3_Dot( Runtime.Velocity, Collision.Plane.Normal );
                    if( NormalSpeed < 0.0f )
                        Runtime.Velocity -=
                            Collision.Plane.Normal * ( 1.35f * NormalSpeed );
                    if( Collision.Plane.Normal.GetY() > 0.55f )
                    {
                        Runtime.Velocity.GetX() *= 0.72f;
                        Runtime.Velocity.GetZ() *= 0.72f;
                    }
                }
                else
                {
                    Runtime.Transform.SetTranslation( NewPos );
                }
            }

            Runtime.ReturnTimer -= DeltaTime;
            if( Runtime.ReturnTimer <= 0.0f )
            {
                Runtime.State = VR_WEAPON_RETURNING;
                Runtime.ReturnTimer = 0.0f;
                Runtime.ReturnStart = Runtime.Transform.GetTranslation();
                Runtime.ReturnRotation = quaternion( Runtime.Transform );
                Runtime.Velocity.Zero();
            }
            break;
        }

        case VR_WEAPON_RETURNING:
        {
            Runtime.ReturnTimer += DeltaTime;
            const f32 T = x_clamp( Runtime.ReturnTimer / 0.45f, 0.0f, 1.0f );
            const f32 Ease = T * T * ( 3.0f - 2.0f * T );
            Runtime.Transform = Socket;
            Runtime.Transform.SetTranslation(
                Runtime.ReturnStart +
                ( Socket.GetTranslation() - Runtime.ReturnStart ) * Ease );
            Runtime.Transform.SetRotation(
                Blend( Runtime.ReturnRotation, quaternion( Socket ), Ease ) );
            if( T >= 1.0f )
                Runtime.State = VR_WEAPON_HOLSTERED;
            break;
        }
        }

        pWeapon->SetVisible( TRUE );
        pWeapon->SetRenderState(
            ( Item == INVEN_WEAPON_DESERT_EAGLE )
                ? new_weapon::RENDER_STATE_PLAYER
                : new_weapon::RENDER_STATE_NPC );
        pWeapon->SetVrWorldTransform( Runtime.Transform );
    }

    for( s32 Hand = 0; Hand < 2; ++Hand )
        m_VrPreviousGrip[Hand] = m_VrGripAmount[Hand];
#else
    (void)DeltaTime;
#endif
}
