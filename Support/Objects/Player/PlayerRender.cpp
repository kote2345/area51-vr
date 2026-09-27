//=========================================================================

//
//  PlayerRender.cpp
//
//=========================================================================

//=========================================================================
//  INCLUDES
//=========================================================================

#include "Player.hpp"
#include "Objects/BaseProjectile.hpp"
#include "Render/PrimitiveDebug.hpp"
#include "GameLib/RenderContext.hpp"
#include "Objects/HudObject.hpp"
#include "Objects/LoreObject.hpp"
#include "NetworkMgr/NetworkMgr.hpp"
#include "UI/ui_renderer.hpp"
#include "StringMgr/StringMgr.hpp"
#include "e_Audio.hpp"
#include "CollisionMgr/CollisionMgr.hpp"

#if defined( A51_ENABLE_OPENXR )
#include "VR/XRSession.hpp"
#include "VR/VRArmIK.hpp"
#if defined(TARGET_ANDROID)
#include <android/log.h>
#endif
extern a51::xr::VulkanSession g_XRSession;
#endif

//=========================================================================
//  IMPLEMENTATION
//=========================================================================

extern f32 g_SpawnFadeTime;

#if defined( X_EDITOR )
xbool g_ShowPlayerPos = TRUE;
#else
xbool g_ShowPlayerPos = FALSE;
#endif

extern xbool g_ShowLoreObjectCollision;
extern f32   g_LO_SphereSize;
extern f32   g_LO_RenderDist;
extern f32   g_Dist;

#if defined( A51_ENABLE_OPENXR )
namespace
{

static xbool BuildBoneDelta( const vector3& From,
                             const vector3& To,
                             const vector3& Pivot,
                             matrix4& Delta )
{
    vector3 Source = From;
    vector3 Target = To;
    if( !Source.SafeNormalize() || !Target.SafeNormalize() )
        return FALSE;

    f32 CosAngle = v3_Dot( Source, Target );
    CosAngle = MAX( -1.0f, MIN( 1.0f, CosAngle ) );
    const radian Angle = x_acos( CosAngle );
    if( Angle < DEG_TO_RAD( 0.05f ) )
    {
        Delta.Identity();
        return TRUE;
    }

    vector3 Axis = v3_Cross( Source, Target );
    if( !Axis.SafeNormalize() )
    {
        Axis = v3_Cross( Source, vector3( 0.0f, 1.0f, 0.0f ) );
        if( !Axis.SafeNormalize() )
        {
            Axis = v3_Cross( Source, vector3( 1.0f, 0.0f, 0.0f ) );
            if( !Axis.SafeNormalize() )
                return FALSE;
        }
    }

    Delta.Setup( Axis, Angle );
    Delta.SetTranslation( Pivot - Delta.RotateVector( Pivot ) );
    return TRUE;
}

static xbool IsBoneDescendant( const anim_group& Group,
                               s32 Bone,
                               s32 Root )
{
    while( Bone >= 0 )
    {
        if( Bone == Root )
            return TRUE;
        Bone = Group.GetBoneParent( Bone );
    }
    return FALSE;
}

static void ApplyBoneDeltaToSubtree( const anim_group& Group,
                                     matrix4* pMatrices,
                                     s32 nMatrices,
                                     s32 Root,
                                     const matrix4& Delta )
{
    for( s32 Bone = Root; Bone < nMatrices; ++Bone )
    {
        if( IsBoneDescendant( Group, Bone, Root ) )
            pMatrices[Bone] = Delta * pMatrices[Bone];
    }
}

static matrix4 GetVRTrackingToWorld( const player& Player )
{
    matrix4 TrackingToWorld = Player.GetL2W();
    TrackingToWorld.SetTranslation( Player.GetRenderView().GetPosition() );
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
        TrackingToWorld = TrackingToWorld * HeadLocal;
    }
    return TrackingToWorld;
}

static xbool GetVRBodyJointTarget( const matrix4& TrackingToWorld,
                                   const a51::xr::body_tracking_pose& Pose,
                                   u32 Joint,
                                   vector3& Target )
{
    if( !Pose.Valid || Joint >= a51::xr::BODY_JOINT_FULL_BODY_COUNT ||
        !Pose.Joints[Joint].PositionValid )
    {
        return FALSE;
    }

    const vector3 TrackingOffset(
        -Pose.Joints[Joint].Position[0] * 100.0f,
         Pose.Joints[Joint].Position[1] * 100.0f,
        -Pose.Joints[Joint].Position[2] * 100.0f );
    Target = TrackingToWorld * TrackingOffset;
    return TRUE;
}

static xbool GetVRBodyJointRotation( const matrix4& TrackingToWorld,
                                     const a51::xr::body_tracking_pose& Pose,
                                     u32 Joint,
                                     matrix4& Rotation )
{
    if( !Pose.Valid || Joint >= a51::xr::BODY_JOINT_UPPER_BODY_COUNT ||
        !Pose.Joints[Joint].OrientationValid )
    {
        return FALSE;
    }

    const a51::xr::body_joint_pose& BodyJoint = Pose.Joints[Joint];
    quaternion JointRotation( -BodyJoint.Orientation[0],
                                BodyJoint.Orientation[1],
                               -BodyJoint.Orientation[2],
                                BodyJoint.Orientation[3] );
    JointRotation.Normalize();

    matrix4 JointLocal;
    JointLocal.Identity();
    JointLocal.SetRotation( JointRotation );
    Rotation = TrackingToWorld;
    Rotation.ClearTranslation();
    Rotation.ClearScale();
    Rotation = Rotation * JointLocal;
    Rotation.ClearTranslation();
    Rotation.ClearScale();
    return TRUE;
}

static vector3 GetSolvedBoneJoint( const loco_char_anim_player& AnimPlayer,
                                   const matrix4* pMatrices,
                                   s32 Bone )
{
    return pMatrices[Bone] * AnimPlayer.GetBoneBindPosition( Bone );
}

static void ApplyTrackedTorsoSegment( const anim_group& Group,
                                      const loco_char_anim_player& AnimPlayer,
                                      matrix4* pMatrices,
                                      s32 nMatrices,
                                      s32 ParentBone,
                                      s32 ChildBone,
                                      s32 AffectedBone,
                                      const vector3& TargetStart,
                                      const vector3& TargetEnd )
{
    if( ParentBone < 0 || ChildBone < 0 ||
        AffectedBone < 0 || ParentBone >= nMatrices ||
        ChildBone >= nMatrices || AffectedBone >= nMatrices )
    {
        return;
    }

    const vector3 CurrentStart = GetSolvedBoneJoint(
        AnimPlayer, pMatrices, ParentBone );
    const vector3 CurrentEnd = GetSolvedBoneJoint(
        AnimPlayer, pMatrices, ChildBone );
    matrix4 Delta;
    if( !BuildBoneDelta( CurrentEnd - CurrentStart,
                         TargetEnd - TargetStart, CurrentStart, Delta ) )
    {
        return;
    }

    Delta.SetTranslation( TargetStart - Delta.RotateVector( CurrentStart ) );
    ApplyBoneDeltaToSubtree( Group, pMatrices, nMatrices,
                             AffectedBone, Delta );
}

static xbool ApplyTrackedLeg( const anim_group& Group,
                              const loco_char_anim_player& AnimPlayer,
                              matrix4* pMatrices,
                              s32 nMatrices,
                              s32 ThighBone,
                              s32 CalfBone,
                              s32 FootBone,
                              const vector3& TargetThigh,
                              const vector3& TargetKnee,
                              const vector3& TargetAnkle )
{
    if( ThighBone < 0 || CalfBone < 0 || FootBone < 0 ||
        ThighBone >= nMatrices || CalfBone >= nMatrices ||
        FootBone >= nMatrices )
    {
        return FALSE;
    }

    vector3 CurrentHip = GetSolvedBoneJoint( AnimPlayer, pMatrices,
                                              ThighBone );
    matrix4 HipTranslation;
    HipTranslation.Identity();
    HipTranslation.SetTranslation( TargetThigh - CurrentHip );
    ApplyBoneDeltaToSubtree( Group, pMatrices, nMatrices,
                             ThighBone, HipTranslation );

    CurrentHip = GetSolvedBoneJoint( AnimPlayer, pMatrices, ThighBone );
    vector3 CurrentKnee = GetSolvedBoneJoint( AnimPlayer, pMatrices,
                                               CalfBone );
    matrix4 ThighDelta;
    if( !BuildBoneDelta( CurrentKnee - CurrentHip,
                         TargetKnee - TargetThigh,
                         CurrentHip, ThighDelta ) )
    {
        return FALSE;
    }
    ApplyBoneDeltaToSubtree( Group, pMatrices, nMatrices,
                             ThighBone, ThighDelta );

    CurrentKnee = GetSolvedBoneJoint( AnimPlayer, pMatrices, CalfBone );
    const vector3 CurrentAnkle = GetSolvedBoneJoint(
        AnimPlayer, pMatrices, FootBone );
    matrix4 CalfDelta;
    if( !BuildBoneDelta( CurrentAnkle - CurrentKnee,
                         TargetAnkle - TargetKnee,
                         CurrentKnee, CalfDelta ) )
    {
        return FALSE;
    }
    ApplyBoneDeltaToSubtree( Group, pMatrices, nMatrices,
                             CalfBone, CalfDelta );
    return TRUE;
}

static xbool GetVRHandTarget( const player& Player,
                              u32 Hand,
                              vector3& Target,
                              matrix4& TargetRotation )
{
    a51::xr::controller_pose Pose{};
    if( !g_XRSession.GetControllerPose( Hand, Pose ) || !Pose.Valid )
        return FALSE;

    /* Controller locations are head-relative. Transform them with the same
     * HMD orientation and eye position as the rendered camera so turning the
     * head does not drag the avatar arms away from the controller poses. */
    const matrix4 TrackingToWorld = GetVRTrackingToWorld( Player );
    const vector3 TrackingOffset( -Pose.Position[0] * 100.0f,
                                   Pose.Position[1] * 100.0f,
                                  -Pose.Position[2] * 100.0f );
    Target = TrackingToWorld * TrackingOffset;

    quaternion ControllerLocal( -Pose.Orientation[0],
                                  Pose.Orientation[1],
                                 -Pose.Orientation[2],
                                  Pose.Orientation[3] );
    ControllerLocal.Normalize();
    matrix4 ControllerRotation;
    ControllerRotation.Identity();
    ControllerRotation.SetRotation( ControllerLocal );
    matrix4 TrackingToWorldRotation = TrackingToWorld;
    TrackingToWorldRotation.ClearTranslation();
    TrackingToWorldRotation.ClearScale();
    TargetRotation = TrackingToWorldRotation * ControllerRotation;
    TargetRotation.ClearTranslation();
    TargetRotation.ClearScale();
    return TRUE;
}

static void AddVrHudImage( const texture& Texture,
                           const vector3& PanelOrigin,
                           const vector3& Right,
                           const vector3& Up,
                           f32 WorldPerPixel,
                           f32 X,
                           f32 Y,
                           f32 Width,
                           f32 Height,
                           const vector2& UV0,
                           const vector2& UV1,
                           const xcolor& Color )
{
    const vector3 TopLeft = PanelOrigin + Right * ( X * WorldPerPixel ) -
                            Up * ( Y * WorldPerPixel );
    const vector3 TopRight = TopLeft + Right * ( Width * WorldPerPixel );
    const vector3 BottomLeft = TopLeft - Up * ( Height * WorldPerPixel );
    const vector3 BottomRight = TopRight - Up * ( Height * WorldPerPixel );
    const render::primitive_draw_desc Desc(
        &Texture, render::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        render::PRIMITIVE_BLEND_ALPHA, render::PRIMITIVE_DEPTH_DISABLED,
        render::PRIMITIVE_RASTER_SOLID_NO_CULL,
        render::PRIMITIVE_SAMPLER_LINEAR_CLAMP,
        render::PRIMITIVE_LAYER_TRANSPARENT );
    render::PrimitiveBatch Batch( Desc );
    Batch.Reserve( 4, 6 );
    const render::primitive_vertex TL( TopLeft, vector2( UV0.X, UV0.Y ), Color );
    const render::primitive_vertex TR( TopRight, vector2( UV1.X, UV0.Y ), Color );
    const render::primitive_vertex BR( BottomRight, vector2( UV1.X, UV1.Y ), Color );
    const render::primitive_vertex BL( BottomLeft, vector2( UV0.X, UV1.Y ), Color );
    Batch.AddTriangle( TL, TR, BR );
    Batch.AddTriangle( TL, BR, BL );
    matrix4 Identity;
    Identity.Identity();
    Batch.Submit( Identity );
}

static void AddVrHudText( ui_font& Font,
                          const char* pText,
                          const vector3& PanelOrigin,
                          const vector3& Right,
                          const vector3& Up,
                          f32 WorldPerPixel,
                          f32 X,
                          f32 Y,
                          f32 RightAlignedWidth,
                          const xcolor& HudColor )
{
    texture* pFontTexture = Font.GetBitmapTexture();
    const s32 BitmapWidth = Font.GetBitmapWidth();
    const s32 BitmapHeight = Font.GetBitmapHeight();
    if( !pFontTexture || BitmapWidth <= 0 || BitmapHeight <= 0 || !pText )
        return;

    const xwstring Text( pText );
    f32 CursorX = X + RightAlignedWidth - Font.TextWidth( Text );
    xcolor TopColor( HudColor );
    xcolor BottomColor( HudColor );
    TopColor.A = BottomColor.A = 204;
    if( ( HudColor.R + HudColor.G + HudColor.B ) / 3 > 59 )
    {
        TopColor.R = TopColor.G = TopColor.B = 255;
    }
    else
    {
        BottomColor.R = BottomColor.G = BottomColor.B = 0;
    }

    const render::primitive_draw_desc Desc(
        pFontTexture, render::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        render::PRIMITIVE_BLEND_ALPHA, render::PRIMITIVE_DEPTH_DISABLED,
        render::PRIMITIVE_RASTER_SOLID_NO_CULL,
        /* ui_font::RenderGlyphQuad uses point sampling for this packed atlas.
         * Linear filtering blends the tiny glyph masks with neighboring
         * transparent atlas cells at the weapon HUD's reduced world scale. */
        render::PRIMITIVE_SAMPLER_POINT_CLAMP,
        render::PRIMITIVE_LAYER_TRANSPARENT );
    render::PrimitiveBatch Batch( Desc );
    Batch.Reserve( Text.GetLength() * 4, Text.GetLength() * 6 );

    for( s32 i = 0; i < Text.GetLength(); ++i )
    {
        // GetCharacter performs the character-to-atlas lookup itself. Passing
        // LookUpCharacter's atlas index here looked that index up a second
        // time as though it were a character code, sampling unrelated atlas
        // cells instead of the digits.
        const ui_font::Character& Character = Font.GetCharacter( Text[i] );
        const f32 GlyphWidth = (f32)Character.W;
        const f32 GlyphHeight = (f32)Font.GetLineHeight();
        // Match ui_font::RenderText's atlas coordinates exactly. Insetting the
        // UVs here clips the narrow glyphs used by the ammo counter.
        const vector2 UV0( (f32)Character.X / BitmapWidth,
                           (f32)Character.Y / BitmapHeight );
        const vector2 UV1( (f32)( Character.X + Character.W ) / BitmapWidth,
                           (f32)( Character.Y + Font.GetLineHeight() ) / BitmapHeight );

        const vector3 TopLeft = PanelOrigin +
            Right * ( CursorX * WorldPerPixel ) - Up * ( Y * WorldPerPixel );
        const vector3 TopRight = TopLeft + Right * ( GlyphWidth * WorldPerPixel );
        const vector3 BottomLeft = TopLeft - Up * ( GlyphHeight * WorldPerPixel );
        const vector3 BottomRight = TopRight - Up * ( GlyphHeight * WorldPerPixel );
        const render::primitive_vertex TL( TopLeft, UV0, TopColor );
        const render::primitive_vertex TR( TopRight, vector2( UV1.X, UV0.Y ), TopColor );
        const render::primitive_vertex BR( BottomRight, UV1, BottomColor );
        const render::primitive_vertex BL( BottomLeft, vector2( UV0.X, UV1.Y ), BottomColor );
        Batch.AddTriangle( TL, TR, BR );
        Batch.AddTriangle( TL, BR, BL );
        CursorX += GlyphWidth + 1.0f;
    }

    matrix4 Identity;
    Identity.Identity();
    Batch.Submit( Identity );
}

static void DrawVrAmmoHud( player& Player,
                           hud_ammo& AmmoHud,
                           new_weapon& Weapon,
                           const matrix4& WeaponTransform,
                           const vector3& BarrelDirection )
{
    texture* pFrameTexture = AmmoHud.m_AmmoBoxBMP.GetPointer();
    texture* pClipTexture = AmmoHud.m_AmmoBoxBMP_Clip.GetPointer();
    texture* pReserveTexture = AmmoHud.m_AmmoBoxBMP_Resv.GetPointer();
    texture* pGrenadesTexture = AmmoHud.m_AmmoBoxBMP_Nads.GetPointer();
    ui_font* pHudNumberFont = g_UiMgr ? g_UiMgr->GetFont( "hudnum" ) : NULL;
    ui_font* pSmallFont = g_UiMgr ? g_UiMgr->GetFont( "small" ) : NULL;
    if( !pFrameTexture || !pClipTexture || !pReserveTexture ||
        !pGrenadesTexture || !pHudNumberFont || !pSmallFont )
        return;

    s32 AmmoAmount = 0;
    s32 AmmoMax = 0;
    s32 AmmoPerClip = 0;
    s32 AmmoInClip = 0;
    Weapon.GetAmmoState( new_weapon::AMMO_PRIMARY, AmmoAmount, AmmoMax,
                         AmmoPerClip, AmmoInClip );
    if( AmmoPerClip <= 0 )
        return;

    const xbitmap& FrameBitmap = pFrameTexture->m_bitmap;
    const f32 FrameWidth = (f32)FrameBitmap.GetWidth();
    const f32 FrameHeight = (f32)FrameBitmap.GetHeight();
    if( FrameWidth <= 0.0f || FrameHeight <= 0.0f )
        return;

    vector3 Right( BarrelDirection );
    if( !Right.SafeNormalize() )
        Right = WeaponTransform.RotateVector( vector3( 1.0f, 0.0f, 0.0f ) );
    if( !Right.SafeNormalize() )
        return;

    // Use the weapon's own axes so the ammo panel keeps the same pitch, yaw,
    // and roll as the held model instead of turning toward the player's head.
    vector3 Up = WeaponTransform.RotateVector( vector3( 0.0f, 1.0f, 0.0f ) );
    Up -= Right * v3_Dot( Up, Right );
    if( !Up.SafeNormalize() )
    {
        Up = WeaponTransform.RotateVector( vector3( 1.0f, 0.0f, 0.0f ) );
        Up -= Right * v3_Dot( Up, Right );
        if( !Up.SafeNormalize() )
            return;
    }
    vector3 PanelNormal = v3_Cross( Right, Up );
    if( !PanelNormal.SafeNormalize() )
        return;

    vector3 TowardPlayer = Player.GetRenderView().GetPosition() -
                           WeaponTransform.GetTranslation();
    const f32 NormalSide = v3_Dot( PanelNormal, TowardPlayer ) >= 0.0f
                         ? 1.0f : -1.0f;

    const f32 PanelWidth = 24.0f;
    const f32 WorldPerPixel = PanelWidth / FrameWidth;
    const f32 PanelHeight = FrameHeight * WorldPerPixel;
    const vector3 PanelCenter = WeaponTransform.GetTranslation() +
                                BarrelDirection * 9.0f + Up * 4.0f +
                                PanelNormal * ( NormalSide * 3.0f );
    const vector3 PanelOrigin = PanelCenter - Right * ( PanelWidth * 0.5f ) +
                                Up * ( PanelHeight * 0.5f );
    const vector2 UV0( 0.0f, 0.0f );
    const vector2 UV1( 1.0f, 1.0f );
    const xcolor InsideColor( 0, 31, 0, 204 );

    AddVrHudImage( *pClipTexture, PanelOrigin, Right, Up, WorldPerPixel,
                   0.0f, 0.0f, FrameWidth, FrameHeight, UV0, UV1,
                   InsideColor );
    if( AmmoHud.m_WarningClipALPHA > 0 )
    {
        xcolor Warning( 200, 50, 0, (u8)AmmoHud.m_WarningClipALPHA );
        if( AmmoHud.m_CriticalClip )
            Warning.A = (u8)MIN( hud_object::m_PulseAlpha, 100.0f );
        AddVrHudImage( *pClipTexture, PanelOrigin, Right, Up, WorldPerPixel,
                       0.0f, 0.0f, FrameWidth, FrameHeight, UV0, UV1, Warning );
    }

    AddVrHudImage( *pReserveTexture, PanelOrigin, Right, Up, WorldPerPixel,
                   0.0f, 0.0f, FrameWidth, FrameHeight, UV0, UV1,
                   InsideColor );
    if( AmmoHud.m_WarningResvALPHA > 0 )
    {
        xcolor Warning( 200, 50, 0, (u8)AmmoHud.m_WarningResvALPHA );
        if( AmmoHud.m_CriticalResv )
            Warning.A = (u8)MIN( hud_object::m_PulseAlpha, 100.0f );
        AddVrHudImage( *pReserveTexture, PanelOrigin, Right, Up, WorldPerPixel,
                       0.0f, 0.0f, FrameWidth, FrameHeight, UV0, UV1, Warning );
    }

    AddVrHudImage( *pGrenadesTexture, PanelOrigin, Right, Up, WorldPerPixel,
                   0.0f, 0.0f, FrameWidth, FrameHeight, UV0, UV1,
                   InsideColor );
    if( AmmoHud.m_WarningNads )
        AddVrHudImage( *pGrenadesTexture, PanelOrigin, Right, Up, WorldPerPixel,
                       0.0f, 0.0f, FrameWidth, FrameHeight, UV0, UV1,
                       xcolor( 180, 50, 0, 100 ) );

    AddVrHudImage( *pFrameTexture, PanelOrigin, Right, Up, WorldPerPixel,
                   0.0f, 0.0f, FrameWidth, FrameHeight, UV0, UV1, g_HudColor );

    const s32 ClipX = 53;
    const s32 ClipY = 19;
    const s32 ReserveX = 134 - 32;
    const s32 ReserveY = 43 - 19;
    const char* pClipText = xfs( "%d", AmmoInClip );
    const char* pReserveText = xfs( "%d", MAX( 0, AmmoAmount - AmmoInClip ) );
    const vector3 TextOrigin = PanelOrigin +
                               PanelNormal * ( NormalSide * 0.2f );
    AddVrHudText( *pHudNumberFont, pClipText, TextOrigin, Right, Up,
                  WorldPerPixel, (f32)ClipX, (f32)ClipY, 20.0f, g_HudColor );
    AddVrHudText( *pSmallFont, pReserveText, TextOrigin, Right, Up,
                  WorldPerPixel, (f32)ReserveX, (f32)ReserveY, 20.0f,
                  g_HudColor );

    texture* pGrenadeIcon = NULL;
    const inven_item GrenadeItem = Player.GetCurrentGrenadeType2();
    switch( GrenadeItem )
    {
    case INVEN_GRENADE_FRAG:          pGrenadeIcon = AmmoHud.m_FragAmmoIcon.GetPointer(); break;
    case INVEN_GRENADE_JBEAN:         pGrenadeIcon = AmmoHud.m_JBeanAmmoIcon.GetPointer(); break;
    case INVEN_GRENADE_JBEAN_ENHANCE: pGrenadeIcon = AmmoHud.m_JBeanXAmmoIcon.GetPointer(); break;
    default: break;
    }
    if( pGrenadeIcon )
    {
        const f32 IconWidth = (f32)pGrenadeIcon->m_bitmap.GetWidth();
        const f32 IconHeight = (f32)pGrenadeIcon->m_bitmap.GetHeight();
        AddVrHudImage( *pGrenadeIcon, PanelOrigin, Right, Up, WorldPerPixel,
                       94.0f, 4.0f, IconWidth, IconHeight, UV0, UV1,
                       XCOLOR_WHITE );
        const char* pGrenadeCount = xfs( "%d", (s32)Player.GetInventory2().GetAmount( GrenadeItem ) );
        AddVrHudText( *pSmallFont, pGrenadeCount, TextOrigin, Right, Up,
                      WorldPerPixel, 94.0f, 0.0f, 20.0f, XCOLOR_WHITE );
    }
}

} // anonymous namespace
#endif

xbool player::GetVrHandTransform( u32 Hand, matrix4& Transform )
{
#if defined( A51_ENABLE_OPENXR )
    if( !IsVrAvatarMode() || Hand >= 2 )
        return FALSE;

    matrix4 TrackedTransform;
    if( !GetVrTrackedHandTransform( Hand, TrackedTransform ) )
    {
        m_VrCollisionHandTransformValid[Hand] = FALSE;
        return FALSE;
    }

    vector3 Position = TrackedTransform.GetTranslation();
    if( m_VrCollisionHandTransformValid[Hand] )
    {
        vector3 SweepStart =
            m_VrCollisionHandTransform[Hand].GetTranslation();
        vector3 Remaining = Position - SweepStart;
        const f32 HandRadius = 10.0f;

        /* Resolve the current render-frame controller pose, rather than
         * reusing the older pose sampled during simulation. This keeps the
         * rendered hands in step with tracking while retaining wall contact. */
        for( s32 Iteration = 0; Iteration < 2; ++Iteration )
        {
            if( Remaining.LengthSquared() <= 0.0001f )
                break;

            const vector3 SweepEnd = SweepStart + Remaining;
            g_CollisionMgr.UseLowPoly();
            g_CollisionMgr.SphereSetup( GetGuid(), SweepStart,
                                        SweepEnd, HandRadius );
            g_CollisionMgr.CheckCollisions(
                object::TYPE_ALL_TYPES,
                object::ATTR_BLOCKS_CHARACTER,
                object::ATTR_COLLISION_PERMEABLE );
            if( g_CollisionMgr.m_nCollisions == 0 )
            {
                SweepStart = SweepEnd;
                break;
            }

            const collision_mgr::collision& Collision =
                g_CollisionMgr.m_Collisions[0];
            const f32 SafeT = x_clamp( Collision.T - 0.01f,
                                       0.0f, 1.0f );
            SweepStart += Remaining * SafeT;
            Remaining = Position - SweepStart;

            const f32 IntoSurface =
                v3_Dot( Remaining, Collision.Plane.Normal );
            if( IntoSurface < 0.0f )
                Remaining -= Collision.Plane.Normal * IntoSurface;
            else
                break;
        }

        Position = SweepStart;
    }

    TrackedTransform.SetTranslation( Position );
    Transform = TrackedTransform;
    m_VrCollisionHandTransform[Hand] = Transform;
    m_VrCollisionHandTransformValid[Hand] = TRUE;
    return TRUE;
#else
    (void)Hand;
    (void)Transform;
    return FALSE;
#endif
}

xbool player::GetVrTrackedHandTransform( u32 Hand, matrix4& Transform ) const
{
#if defined( A51_ENABLE_OPENXR )
    if( !IsVrAvatarMode() || Hand >= 2 )
        return FALSE;

    vector3 Position;
    matrix4 Rotation;
    if( !GetVRHandTarget( *this, Hand, Position, Rotation ) )
        return FALSE;

    Transform = Rotation;
    Transform.SetTranslation( Position );
    return TRUE;
#else
    (void)Hand;
    (void)Transform;
    return FALSE;
#endif
}

s32 player::GetVrAvatarBoneCount( void )
{
#if defined( A51_ENABLE_OPENXR )
    if( IsVrAvatarMode() && m_Loco.IsAnimLoaded() )
        return m_Loco.m_Player.GetNBones();
#endif
    return 0;
}

const matrix4* player::ApplyVrArmIK( const matrix4* pMatrices,
                                    s32 nActiveBones )
{
#if defined( A51_ENABLE_OPENXR )
    if( !IsVrAvatarMode() || !pMatrices || nActiveBones <= 0 ||
        !m_Loco.IsAnimLoaded() || IsDead() ||
        m_CurrentAnimState == ANIM_STATE_DEATH ||
        m_CurrentAnimState == ANIM_STATE_CHANGE_MUTATION )
    {
        return pMatrices;
    }

    const anim_group* pGroup = m_hAnimGroup.GetPointer();
    if( !pGroup )
        return pMatrices;

    const s32 UpperBones[2] =
    {
        m_Loco.m_Player.GetBoneIndex( a51::xr::kMultiplayerUpperArmBones[0] ),
        m_Loco.m_Player.GetBoneIndex( a51::xr::kMultiplayerUpperArmBones[1] )
    };
    const s32 ForearmBones[2] =
    {
        m_Loco.m_Player.GetBoneIndex( a51::xr::kMultiplayerForearmBones[0] ),
        m_Loco.m_Player.GetBoneIndex( a51::xr::kMultiplayerForearmBones[1] )
    };
    const s32 HandBones[2] =
    {
        m_Loco.m_Player.GetBoneIndex( a51::xr::kMultiplayerHandBones[0] ),
        m_Loco.m_Player.GetBoneIndex( a51::xr::kMultiplayerHandBones[1] )
    };

    matrix4* pSolved = (matrix4*)smem_BufferAlloc(
        nActiveBones * sizeof( matrix4 ) );
    if( !pSolved )
        return pMatrices;
    x_memcpy( pSolved, pMatrices, nActiveBones * sizeof( matrix4 ) );

    /* The VR camera and controller targets are lowered by the gameplay
     * crouch offset. Translate the full rendered skeleton too: the crouch
     * animation bends the legs, but does not lower the animation root enough
     * to keep the feet planted. */
    const f32 CrouchOffset = m_fCurrentCrouchFactor * 70.0f;
    if( CrouchOffset > 0.0f )
    {
        for( s32 Bone = 0; Bone < nActiveBones; ++Bone )
        {
            vector3 Position = pSolved[Bone].GetTranslation();
            Position.GetY() -= CrouchOffset;
            pSolved[Bone].SetTranslation( Position );
        }
    }

    xbool bApplied = ( CrouchOffset > 0.0f );
    a51::xr::body_tracking_pose BodyPose{};
    const xbool bBodyTrackingActive =
        g_XRSession.GetBodyTrackingPose( BodyPose ) &&
        BodyPose.HighFidelity && ( BodyPose.Confidence >= 0.25f );
    vector3 BodyTargets[a51::xr::BODY_JOINT_FULL_BODY_COUNT];
    xbool BodyTargetValid[a51::xr::BODY_JOINT_FULL_BODY_COUNT] = {};
    if( bBodyTrackingActive )
    {
        const matrix4 TrackingToWorld = GetVRTrackingToWorld( *this );
        for( u32 Joint = 0;
             Joint < a51::xr::BODY_JOINT_FULL_BODY_COUNT; ++Joint )
        {
            BodyTargetValid[Joint] = GetVRBodyJointTarget(
                TrackingToWorld, BodyPose, Joint, BodyTargets[Joint] );
        }

        const s32 RootBone = m_Loco.m_Player.GetBoneIndex( "B_01_Root" );
        const s32 SpineLowerBone =
            m_Loco.m_Player.GetBoneIndex( "B_01_Spine01" );
        const s32 SpineUpperBone =
            m_Loco.m_Player.GetBoneIndex( "B_01_Spine02" );
        const s32 NeckBone = m_Loco.m_Player.GetBoneIndex( "B_01_Neck" );
        if( ( RootBone >= 0 ) && ( RootBone < nActiveBones ) &&
            ( SpineLowerBone >= 0 ) &&
            BodyTargetValid[a51::xr::BODY_JOINT_HIPS] )
        {
            /* Match the tracked pelvis to the avatar hip. Body joint targets
             * then describe the torso independently of HMD orientation. */
            const vector3 AvatarHips = GetSolvedBoneJoint(
                m_Loco.m_Player, pSolved, RootBone );
            const vector3 BodyOffset = AvatarHips -
                BodyTargets[a51::xr::BODY_JOINT_HIPS];
            for( u32 Joint = 0;
                 Joint < a51::xr::BODY_JOINT_FULL_BODY_COUNT; ++Joint )
            {
                if( BodyTargetValid[Joint] )
                    BodyTargets[Joint] += BodyOffset;
            }

            /* The root bone also weights the abdomen mesh. Drive its
             * orientation from the tracked pelvis while preserving the hip
             * pivot; this changes only root-weighted vertices, so the leg
             * animation matrices remain untouched. */
            matrix4 TrackedHipsRotation;
            if( GetVRBodyJointRotation( TrackingToWorld, BodyPose,
                    a51::xr::BODY_JOINT_HIPS, TrackedHipsRotation ) )
            {
                matrix4 RootTransform = pSolved[RootBone];
                RootTransform.SetRotation( TrackedHipsRotation.GetRotation() );
                RootTransform.SetTranslation(
                    AvatarHips - RootTransform.RotateVector(
                        m_Loco.m_Player.GetBoneBindPosition( RootBone ) ) );
                pSolved[RootBone] = RootTransform;
            }

            if( BodyTargetValid[a51::xr::BODY_JOINT_LEFT_SHOULDER] &&
                BodyTargetValid[a51::xr::BODY_JOINT_RIGHT_SHOULDER] &&
                ( UpperBones[0] >= 0 ) && ( UpperBones[1] >= 0 ) &&
                ( UpperBones[0] < nActiveBones ) &&
                ( UpperBones[1] < nActiveBones ) )
            {
                const vector3 CurrentLeft = GetSolvedBoneJoint(
                    m_Loco.m_Player, pSolved, UpperBones[0] );
                const vector3 CurrentRight = GetSolvedBoneJoint(
                    m_Loco.m_Player, pSolved, UpperBones[1] );
                matrix4 ShoulderLineDelta;
                if( BuildBoneDelta(
                        CurrentRight - CurrentLeft,
                        BodyTargets[a51::xr::BODY_JOINT_RIGHT_SHOULDER] -
                        BodyTargets[a51::xr::BODY_JOINT_LEFT_SHOULDER],
                        AvatarHips, ShoulderLineDelta ) )
                {
                    ApplyBoneDeltaToSubtree( *pGroup, pSolved,
                        nActiveBones, SpineLowerBone, ShoulderLineDelta );
                }
            }

            const u32 TrackedChest = a51::xr::BODY_JOINT_CHEST;
            const u32 TrackedNeck = a51::xr::BODY_JOINT_NECK;
            if( BodyTargetValid[TrackedChest] &&
                BodyTargetValid[TrackedNeck] &&
                ( SpineUpperBone >= 0 ) && ( NeckBone >= 0 ) )
            {
                /* Solve the trunk as one continuous segment. Solving each
                 * estimated spine joint independently amplified tracker
                 * noise into a visibly over-bent abdomen and chest. */
                ApplyTrackedTorsoSegment( *pGroup, m_Loco.m_Player,
                    pSolved, nActiveBones, RootBone, SpineUpperBone,
                    SpineLowerBone,
                    BodyTargets[a51::xr::BODY_JOINT_HIPS],
                    BodyTargets[TrackedChest] );
                ApplyTrackedTorsoSegment( *pGroup, m_Loco.m_Player,
                    pSolved, nActiveBones, SpineUpperBone, NeckBone,
                    NeckBone,
                    BodyTargets[TrackedChest], BodyTargets[TrackedNeck] );
                bApplied = TRUE;
            }

            const xbool UseTrackedLegPose = BodyPose.FullBody &&
                !m_bVrButtonCrouching &&
                !m_Loco.IsVrStickAnimationMoving();
            if( UseTrackedLegPose )
            {
                /* Stick locomotion and button crouch keep the authored game
                 * leg animation. Otherwise use the runtime's full-body leg
                 * estimate, including a physical crouch. */
                const s32 ThighBones[2] =
                {
                    m_Loco.m_Player.GetBoneIndex( "B_01_Leg_L_Thigh" ),
                    m_Loco.m_Player.GetBoneIndex( "B_01_Leg_R_Thigh" )
                };
                const s32 CalfBones[2] =
                {
                    m_Loco.m_Player.GetBoneIndex( "B_01_Leg_L_Calf" ),
                    m_Loco.m_Player.GetBoneIndex( "B_01_Leg_R_Calf" )
                };
                const s32 FootBones[2] =
                {
                    m_Loco.m_Player.GetBoneIndex( "B_02_Leg_L_Foot" ),
                    m_Loco.m_Player.GetBoneIndex( "B_02_Leg_R_Foot" )
                };
                const u32 ThighJoints[2] =
                {
                    a51::xr::BODY_JOINT_LEFT_UPPER_LEG,
                    a51::xr::BODY_JOINT_RIGHT_UPPER_LEG
                };
                const u32 KneeJoints[2] =
                {
                    a51::xr::BODY_JOINT_LEFT_LOWER_LEG,
                    a51::xr::BODY_JOINT_RIGHT_LOWER_LEG
                };
                const u32 AnkleJoints[2] =
                {
                    a51::xr::BODY_JOINT_LEFT_ANKLE,
                    a51::xr::BODY_JOINT_RIGHT_ANKLE
                };

                for( u32 Leg = 0; Leg < 2; ++Leg )
                {
                    if( !BodyTargetValid[ThighJoints[Leg]] ||
                        !BodyTargetValid[KneeJoints[Leg]] ||
                        !BodyTargetValid[AnkleJoints[Leg]] )
                    {
                        continue;
                    }

                    bApplied |= ApplyTrackedLeg( *pGroup, m_Loco.m_Player,
                        pSolved, nActiveBones,
                        ThighBones[Leg], CalfBones[Leg], FootBones[Leg],
                        BodyTargets[ThighJoints[Leg]],
                        BodyTargets[KneeJoints[Leg]],
                        BodyTargets[AnkleJoints[Leg]] );
                }
            }
        }
    }
#if defined(TARGET_ANDROID)
    static u32 IKTraceFrame = 0;
    const xbool TraceVRIK = ( (++IKTraceFrame % 90u) == 0u );
#endif
    for( u32 Hand = 0; Hand < 2; ++Hand )
    {
        if( UpperBones[Hand] < 0 || ForearmBones[Hand] < 0 ||
            HandBones[Hand] < 0 || UpperBones[Hand] >= nActiveBones ||
            ForearmBones[Hand] >= nActiveBones || HandBones[Hand] >= nActiveBones )
        {
            continue;
        }

        vector3 WristTarget;
        matrix4 HandTargetRotation;
        matrix4 ConstrainedHand;
        if( !GetVrHandTransform( Hand, ConstrainedHand ) )
        {
#if defined(TARGET_ANDROID)
            if( TraceVRIK )
                __android_log_print( ANDROID_LOG_INFO, "A51VR",
                    "ik hand=%s controller pose unavailable",
                    (Hand == 0) ? "left" : "right" );
#endif
            continue;
        }
        WristTarget = ConstrainedHand.GetTranslation();
        HandTargetRotation = ConstrainedHand;

        vector3 Shoulder;
        vector3 ModelElbow;
        vector3 ElbowPole;
        vector3 Wrist;
        f32 UpperLength = 0.0f;
        f32 ForearmLength = 0.0f;
        const u32 TrackedShoulder = ( Hand == 0 )
            ? a51::xr::BODY_JOINT_LEFT_SHOULDER
            : a51::xr::BODY_JOINT_RIGHT_SHOULDER;
        const u32 TrackedElbow = ( Hand == 0 )
            ? a51::xr::BODY_JOINT_LEFT_LOWER_ARM
            : a51::xr::BODY_JOINT_RIGHT_LOWER_ARM;
        if( bBodyTrackingActive && BodyTargetValid[TrackedShoulder] &&
            BodyTargetValid[TrackedElbow] )
        {
            Shoulder = GetSolvedBoneJoint(
                m_Loco.m_Player, pSolved, UpperBones[Hand] );
            ModelElbow = GetSolvedBoneJoint(
                m_Loco.m_Player, pSolved, ForearmBones[Hand] );
            Wrist = GetSolvedBoneJoint(
                m_Loco.m_Player, pSolved, HandBones[Hand] );
            ElbowPole = BodyTargets[TrackedElbow];
            UpperLength = ( ModelElbow - Shoulder ).Length();
            ForearmLength = ( Wrist - ModelElbow ).Length();
        }
        else
        {
            Shoulder = m_Loco.m_Player.GetBonePosition( UpperBones[Hand] );
            ModelElbow = m_Loco.m_Player.GetBonePosition( ForearmBones[Hand] );
            Wrist = m_Loco.m_Player.GetBonePosition( HandBones[Hand] );
            if( CrouchOffset > 0.0f )
            {
                const vector3 AvatarOffset( 0.0f, -CrouchOffset, 0.0f );
                Shoulder += AvatarOffset;
                ModelElbow += AvatarOffset;
                Wrist += AvatarOffset;
            }
            ElbowPole = ModelElbow;
            UpperLength = ( ModelElbow - Shoulder ).Length();
            ForearmLength = ( Wrist - ModelElbow ).Length();
        }

#if defined(TARGET_ANDROID)
        if( TraceVRIK )
            __android_log_print( ANDROID_LOG_INFO, "A51VR",
                "ik hand=%s bones=%d/%d/%d target=%.1f,%.1f,%.1f wrist=%.1f,%.1f,%.1f lengths=%.1f/%.1f",
                (Hand == 0) ? "left" : "right",
                UpperBones[Hand], ForearmBones[Hand], HandBones[Hand],
                WristTarget.GetX(), WristTarget.GetY(), WristTarget.GetZ(),
                Wrist.GetX(), Wrist.GetY(), Wrist.GetZ(),
                UpperLength, ForearmLength );
#endif

        const a51::xr::ik_vec3 IkShoulder =
            { Shoulder.GetX(), Shoulder.GetY(), Shoulder.GetZ() };
        const a51::xr::ik_vec3 IkTarget =
            { WristTarget.GetX(), WristTarget.GetY(), WristTarget.GetZ() };

        const a51::xr::ik_vec3 IkPole =
            { ElbowPole.GetX(), ElbowPole.GetY(), ElbowPole.GetZ() };
        a51::xr::arm_ik_result Result{};
        if( !a51::xr::SolveTwoBoneArm( IkShoulder, IkTarget, IkPole,
                                       UpperLength, ForearmLength, Result ) ||
            !Result.Valid )
        {
            continue;
        }

        const vector3 TargetElbow( Result.Elbow.X,
                                   Result.Elbow.Y,
                                   Result.Elbow.Z );
        const vector3 ReachableWrist( Result.Wrist.X,
                                      Result.Wrist.Y,
                                      Result.Wrist.Z );

        matrix4 UpperDelta;
        if( !BuildBoneDelta( ModelElbow - Shoulder,
                             TargetElbow - Shoulder,
                             Shoulder, UpperDelta ) )
        {
            continue;
        }
        ApplyBoneDeltaToSubtree( *pGroup, pSolved, nActiveBones,
                                 UpperBones[Hand], UpperDelta );

        const vector3 ElbowAfterUpper = UpperDelta * ModelElbow;
        const vector3 WristAfterUpper = UpperDelta * Wrist;
        matrix4 ForearmDelta;
        if( !BuildBoneDelta( WristAfterUpper - ElbowAfterUpper,
                             ReachableWrist - ElbowAfterUpper,
                             ElbowAfterUpper, ForearmDelta ) )
        {
            continue;
        }
        ApplyBoneDeltaToSubtree( *pGroup, pSolved, nActiveBones,
                                 ForearmBones[Hand], ForearmDelta );

        /* The two-bone solve positions the wrist, but the hand bone still
         * retained its animation rotation. Match the controller orientation
         * at the wrist and carry that rotation through the finger subtree. */
        matrix4 CurrentHandRotation = pSolved[HandBones[Hand]];
        CurrentHandRotation.ClearTranslation();
        CurrentHandRotation.ClearScale();
        if( CurrentHandRotation.InvertRT() )
        {
            /* The correction is constant in controller space. Its local Y
             * axis runs along the grip toward the wrist, so rotating there
             * preserves every subsequent controller orientation change. */
            matrix4 WristCorrection;
            WristCorrection.Setup( vector3( 0.0f, 1.0f, 0.0f ), R_180 );
            matrix4 DesiredHandRotation =
                HandTargetRotation * WristCorrection;
            matrix4 HandDelta = DesiredHandRotation * CurrentHandRotation;
            /* Skinning applies the hand bone's bind translation after this
             * matrix. Rotate around that actual wrist joint, not the raw
             * matrix origin, or controller rotation translates the hand by
             * the bind-pose offset. */
            const vector3 HandPivot =
                pSolved[HandBones[Hand]] *
                m_Loco.m_Player.GetBoneBindPosition( HandBones[Hand] );
            HandDelta.SetTranslation(
                HandPivot - HandDelta.RotateVector( HandPivot ) );
            ApplyBoneDeltaToSubtree( *pGroup, pSolved, nActiveBones,
                                     HandBones[Hand], HandDelta );
        }
        bApplied = TRUE;
    }

    return bApplied ? pSolved : pMatrices;
#else
    (void)nActiveBones;
    return pMatrices;
#endif
}

const matrix4* player::ApplyVrHeadVisibility( const matrix4* pMatrices,
                                              s32 nActiveBones )
{
#if defined( A51_ENABLE_OPENXR )
    if( !IsVrAvatarMode() || !pMatrices || nActiveBones <= 0 ||
        !m_Loco.IsAnimLoaded() || IsDead() )
    {
        return pMatrices;
    }

    const anim_group* pGroup = m_hAnimGroup.GetPointer();
    if( !pGroup )
        return pMatrices;

    s32 HeadRoots[4] = { -1, -1, -1, -1 };
    s32 nHeadRoots = 0;
    for( s32 Bone = 0; Bone < nActiveBones; ++Bone )
    {
        const char* pName = pGroup->GetBone( Bone ).Name;
        if( !pName || !x_stristr( pName, "HEAD" ) || nHeadRoots >= 4 )
            continue;

        const s32 Parent = pGroup->GetBoneParent( Bone );
        const char* pParentName = ( Parent >= 0 )
                                ? pGroup->GetBone( Parent ).Name : NULL;
        if( pParentName && x_stristr( pParentName, "HEAD" ) )
            continue;
        HeadRoots[nHeadRoots++] = Bone;
    }
    if( nHeadRoots == 0 )
        return pMatrices;

    matrix4* pHeadless = (matrix4*)smem_BufferAlloc(
        nActiveBones * sizeof( matrix4 ) );
    if( !pHeadless )
        return pMatrices;
    x_memcpy( pHeadless, pMatrices, nActiveBones * sizeof( matrix4 ) );

    for( s32 iRoot = 0; iRoot < nHeadRoots; ++iRoot )
    {
        const s32 Root = HeadRoots[iRoot];
        vector3 HeadPivot = m_Loco.m_Player.GetBonePosition( Root );
        HeadPivot.GetY() -= m_fCurrentCrouchFactor * 70.0f;
        matrix4 CollapsedHead;
        CollapsedHead.Setup( vector3( 0.0f, 0.0f, 0.0f ),
                             quaternion( 0.0f, 0.0f, 0.0f, 1.0f ),
                             HeadPivot );

        for( s32 Bone = Root; Bone < nActiveBones; ++Bone )
        {
            s32 Parent = Bone;
            while( Parent >= 0 && Parent != Root )
                Parent = pGroup->GetBoneParent( Parent );
            if( Parent == Root )
                pHeadless[Bone] = CollapsedHead;
        }
    }

    return pHeadless;
#else
    (void)nActiveBones;
    return pMatrices;
#endif
}

void player::UpdateVrWeaponAttachment( const matrix4* pMatrices,
                                       s32 nActiveBones )
{
#if defined( A51_ENABLE_OPENXR )
    if( !IsVrAvatarMode() ||
        !pMatrices || !m_Loco.IsAnimLoaded() )
    {
        return;
    }

    /* Use the right-hand weapon socket as the shared grip calibration for
     * every firearm. Its left counterpart has a different authored rotation. */
    const s32 CalibrationHand = 1;
    const s32 AttachBone = m_Loco.m_Player.GetBoneIndex( "Attach_R" );
    const s32 HandBone = m_Loco.m_Player.GetBoneIndex(
        a51::xr::kMultiplayerHandBones[CalibrationHand] );
    if( AttachBone < 0 || AttachBone >= nActiveBones ||
        HandBone < 0 || HandBone >= nActiveBones )
        return;

    matrix4 HandBefore = m_Loco.m_Player.GetBoneL2W( HandBone );
    HandBefore.PreTranslate(
        m_Loco.m_Player.GetBoneBindPosition( HandBone ) );
    matrix4 HandBeforeInverse = HandBefore;
    if( !HandBeforeInverse.InvertRT() )
        return;
    matrix4 AttachBefore = m_Loco.m_Player.GetBoneL2W( AttachBone );
    AttachBefore.PreTranslate(
        m_Loco.m_Player.GetBoneBindPosition( AttachBone ) );
    matrix4 WeaponGripInHand = HandBeforeInverse * AttachBefore;

    for( s32 WeaponIndex = 0; WeaponIndex < INVEN_NUM_WEAPONS;
         ++WeaponIndex )
    {
        const inven_item Item =
            inventory2::WeaponIndexToItem( WeaponIndex );
        if( Item == INVEN_NULL || Item == INVEN_WEAPON_SCANNER ||
            Item == INVEN_WEAPON_MUTATION )
            continue;

        vr_weapon_runtime& Runtime = m_VrWeaponRuntime[WeaponIndex];
        if( Runtime.State != VR_WEAPON_HELD ||
            Runtime.Hand < 0 || Runtime.Hand > 1 )
            continue;

        matrix4 ControllerHand;
        if( !GetVrHandTransform( Runtime.Hand, ControllerHand ) )
            continue;

        new_weapon* pWeapon = GetWeaponPtr( Item );
        if( !pWeapon )
            continue;

        Runtime.HandGripOffset = WeaponGripInHand;
        Runtime.HandGripOffset.SetTranslation(
            vector3( 0.0f, 0.0f, 0.0f ) );
        matrix4 WeaponAxisCorrection;
        WeaponAxisCorrection.Setup(
            vector3( 1.0f, 0.0f, 0.0f ), R_90 + R_180 );
        Runtime.HandGripOffset =
            Runtime.HandGripOffset * WeaponAxisCorrection;
        matrix4 WeaponRollCorrection;
        WeaponRollCorrection.Setup(
            vector3( 0.0f, 0.0f, 1.0f ), R_180 );
        Runtime.HandGripOffset =
            Runtime.HandGripOffset * WeaponRollCorrection;
        Runtime.HandGripOffset.SetTranslation(
            vector3( 0.0f, -8.0f, 4.0f ) );
        Runtime.HandGripOffsetInitialized = TRUE;

        matrix4 WeaponAttachL2W = ControllerHand * Runtime.HandGripOffset;
        Runtime.Transform = WeaponAttachL2W;
        pWeapon->SetVrWorldTransform( WeaponAttachL2W );
        pWeapon->SetZone1( GetZone1() );
        pWeapon->SetZone2( GetZone2() );
    }
#else
    (void)pMatrices;
    (void)nActiveBones;
#endif
}

#ifdef DEBUG_GRENADE_THROWING
extern xbool   g_ShowGrenadeEventCollision;
extern vector3 g_EventPos;
extern vector3 g_NewEventPos;
#endif

void player::OnRenderTransparent(void)
{
    actor::OnRenderTransparent();

#ifndef X_EDITOR
    if ( m_CurrentAnimState == ANIM_STATE_MISSION_FAILED )
    {

        if( g_AudioMgr.GetLanguage() == XL_LANG_ENGLISH )
        {
            texture* pTexture = m_MissionFailedBmp.GetPointer();
            if( pTexture )
            {
                const xbitmap& Bitmap = pTexture->m_bitmap;
                f32 X = f32( 256 - (Bitmap.GetWidth()/2) );
                f32 Y = 100.0f;
                g_UIRenderer.DrawImage( *pTexture,
                                        vector2( X, Y ),
                                        vector2( (f32)Bitmap.GetWidth(), (f32)Bitmap.GetHeight() ),
                                        vector2( 0.0f, 0.0f ),
                                        vector2( 1.0f, 1.0f ),
                                        g_HudColor,
                                        0.0f,
                                        UI_BLEND_ALPHA,
                                        UI_SAMPLER_LINEAR_CLAMP );
            }
        }
        else
        {
            // display a localized text message instead of the bitmap            
            irect Rect( 0, 100, 512, 130 );
            RenderLine( (xwchar*)g_StringTableMgr( "ui", "IDS_MISSION_FAILED" ), Rect, 255, g_HudColor, 0, ui_font::h_center | ui_font::v_top  );
        }

        const s32 mfx = 0;
        const s32 mfy = 180;
        xcolor Color( XCOLOR_RED );
        irect Rect;
        Rect.Set( mfx, mfy + 50, 512, mfy+51 );
        Color.Set( XCOLOR_YELLOW );
        RenderLine( (xwchar*)g_StringTableMgr( g_StringMgr.GetString( m_MissionFailedTableName ), g_StringMgr.GetString( m_MissionFailedReasonName ) ), Rect, 255, Color, 0, ui_font::h_center | ui_font::v_top  );
    }
#endif

#ifndef X_RETAIL
    if( g_ShowLoreObjectCollision )
    {
        vector3 StartPos, EndPos;
        s32 i = 0;

        for( i = 0; i < MAX_LORE_ITEMS; i++ )
        {
            lore_object* pLoreObject = (lore_object*)g_ObjMgr.GetObjectByGuid(m_LoreObjectGuids[i]);

            if( !pLoreObject ) continue;

            pLoreObject->DoCollisionCheck(this, StartPos, EndPos);

            vector3 Diff = EndPos-StartPos;
            g_Dist = Diff.Length();

            // only render the debug stuff for the one that is close to us
            if( g_Dist < g_LO_RenderDist )
            {
                // default modifier to full distance in case the collision manager returns no collisions
                f32 DistModifier = 1.0f;

                // if we don't hit anything, T is undefined
                if( g_CollisionMgr.m_nCollisions > 0 )
                {
                    DistModifier = g_CollisionMgr.m_Collisions[0].T;

                    object *pHitObj = g_ObjMgr.GetObjectByGuid(g_CollisionMgr.m_Collisions[0].ObjectHitGuid);

                    if( pHitObj )
                    {
                        render::debug::Line(StartPos, pHitObj->GetPosition());
                        render::debug::Sphere(pHitObj->GetPosition(), g_LO_SphereSize, XCOLOR_GREEN);
                    }
                }

                // get our new end position
                EndPos = StartPos + (DistModifier*Diff);

                //render::debug::Sphere(Pos, 10.0f, XCOLOR_WHITE);
                render::debug::Line(StartPos, EndPos);

                if( g_CollisionMgr.m_nCollisions )
                {
                    render::debug::Sphere(EndPos, g_LO_SphereSize, XCOLOR_RED);
                }
                else
                {
                    // we can see it
                    render::debug::Sphere(EndPos, g_LO_SphereSize, XCOLOR_WHITE);
                }

                pLoreObject->OnColRender( TRUE );
            }
        }
    }

#ifdef DEBUG_GRENADE_THROWING
    if( g_ShowGrenadeEventCollision )
    {
        // get player position
        vector3 Point1 = vector3(0.0f, 0.0f, 0.0f);
        // not set yet
        vector3 Point2 = vector3(0.0f, 0.0f, 0.0f);    
        // get event position
        vector3 Point3 = g_EventPos;

        GetThrowPoints( Point1, Point2, Point3 );
        render::debug::Sphere(Point1, 5.0f);
        render::debug::Sphere(Point2, 6.0f, XCOLOR_GREEN);
        render::debug::Sphere(Point3, 7.0f, XCOLOR_YELLOW);
        render::debug::Sphere(g_NewEventPos, 8.0f, XCOLOR_RED);
    }
#endif // DEBUG_GRENADE_THROWING

#endif // X_RETAIL

    if( IsVrAvatarMode() )
    {
        hud_ammo* pVrAmmoHud = NULL;
        const slot_id HudSlot = g_ObjMgr.GetFirst( object::TYPE_HUD_OBJECT );
        if( HudSlot != SLOT_NULL )
        {
            object* pHudObject = g_ObjMgr.GetObjectBySlot( HudSlot );
            if( pHudObject )
            {
                hud_object& Hud = hud_object::GetSafeType( *pHudObject );
                player_hud& PlayerHud =
                    Hud.GetPlayerHud( g_RenderContext.LocalPlayerIndex );
                if( !PlayerHud.m_Ammo.m_AmmoHudInited )
                    PlayerHud.m_Ammo.Init();
                pVrAmmoHud = &PlayerHud.m_Ammo;
            }
        }

        for( s32 WeaponIndex = 0; WeaponIndex < INVEN_NUM_WEAPONS;
             ++WeaponIndex )
        {
            const inven_item Item =
                inventory2::WeaponIndexToItem( WeaponIndex );
            if( Item == INVEN_NULL || Item == INVEN_WEAPON_MUTATION ||
                !m_Inventory2.HasItem( Item ) )
                continue;

            new_weapon* pVrWeapon = GetWeaponPtr( Item );
            if( !pVrWeapon )
                continue;

            const new_weapon::render_state SavedState =
                pVrWeapon->GetRenderState();
            const xbool HasWorldModel = pVrWeapon->HasVrWorldModel();
            const xbool IsHeld =
                ( m_VrWeaponRuntime[WeaponIndex].State == VR_WEAPON_HELD );
            const xbool IsActiveScanner =
                ( Item == INVEN_WEAPON_SCANNER ) && m_VrScannerActive &&
                ( Item == m_CurrentWeaponItem );
            pVrWeapon->SetRenderState(
                HasWorldModel
                    ? new_weapon::RENDER_STATE_NPC
                    : new_weapon::RENDER_STATE_PLAYER );
            pVrWeapon->OnRenderTransparent();
            if( IsHeld || IsActiveScanner )
            {
                vector3 FirePosition;
                radian3 FireTrajectory;
                if( GetVrHeldWeaponShot( pVrWeapon,
                                         new_weapon::FIRE_POINT_DEFAULT,
                                         FirePosition, FireTrajectory ) )
                {
                    m_VrShownShotWeapon = Item;
                    m_VrShownShotPosition = FirePosition;
                    m_VrShownShotRotation = FireTrajectory;
                    m_VrShownShotValid = TRUE;
                    /* Use the same velocity calculation as the projectile.
                     * VR firing passes zero inherited velocity, so the
                     * arbitrary length only scales the displayed line. */
                    const vector3 FireVelocity =
                        base_projectile::ComputeInitialVelocity(
                            FireTrajectory, vector3( 0.0f, 0.0f, 0.0f ),
                            3000.0f );
                    vector3 BarrelDirection( 0.0f, 0.0f, 1.0f );
                    BarrelDirection.Rotate( FireTrajectory );
                    BarrelDirection.SafeNormalize();
                    render::debug::Line(
                        FirePosition,
                        FirePosition + FireVelocity,
                        XCOLOR_GREEN,
                        render::PRIMITIVE_DEPTH_READ_ONLY );

                    if( IsHeld && Item != INVEN_WEAPON_SCANNER &&
                        pVrAmmoHud )
                    {
                        const matrix4& WeaponTransform =
                            m_VrWeaponRuntime[WeaponIndex].Transform;
                        DrawVrAmmoHud( *this, *pVrAmmoHud, *pVrWeapon,
                                       WeaponTransform, BarrelDirection );
                    }
                }
            }
            pVrWeapon->SetRenderState( SavedState );
        }
        return;
    }

    //render weapon
    new_weapon* pWeapon = GetCurrentWeaponPtr();
    if( pWeapon )
    {
        const new_weapon::render_state SavedState = pWeapon->GetRenderState();
        if( IsVrAvatarMode() &&
            ( m_CurrentWeaponItem == INVEN_WEAPON_DESERT_EAGLE ) )
        {
            pWeapon->SetRenderState( new_weapon::RENDER_STATE_PLAYER );
        }
        else if( IsAvatar() && pWeapon->IsUsingSplitScreen() )
        {
            pWeapon->SetRenderState( new_weapon::RENDER_STATE_NPC );
        }

        pWeapon->OnRenderTransparent();

        pWeapon->SetRenderState( SavedState );
    }
}

void player::OnRenderWeapon( void )
{
    if( IsVrAvatarMode() )
    {
        /* Refresh held models from the newest tracked hand pose immediately
         * before drawing. Simulation may run slower than the headset display. */
        for( s32 Hand = 0; Hand < 2; ++Hand )
        {
            const inven_item Item = m_VrHeldWeapon[Hand];
            const s32 WeaponIndex = inventory2::ItemToWeaponIndex( Item );
            if( Item == INVEN_NULL || WeaponIndex < 0 ||
                WeaponIndex >= INVEN_NUM_WEAPONS )
                continue;

            matrix4 HandTransform;
            if( !GetVrHandTransform( Hand, HandTransform ) )
                continue;
            vr_weapon_runtime& Runtime = m_VrWeaponRuntime[WeaponIndex];
            if( Runtime.HandGripOffsetInitialized )
                HandTransform = HandTransform * Runtime.HandGripOffset;
            Runtime.Transform = HandTransform;
            new_weapon* pHeldWeapon = GetWeaponPtr( Item );
            if( pHeldWeapon )
                pHeldWeapon->SetVrWorldTransform( HandTransform );
        }

        for( s32 WeaponIndex = 0; WeaponIndex < INVEN_NUM_WEAPONS;
             ++WeaponIndex )
        {
            const inven_item Item =
                inventory2::WeaponIndexToItem( WeaponIndex );
            if( Item == INVEN_NULL || Item == INVEN_WEAPON_MUTATION ||
                !m_Inventory2.HasItem( Item ) )
                continue;

            new_weapon* pVrWeapon = GetWeaponPtr( Item );
            if( !pVrWeapon )
                continue;

            const new_weapon::render_state SavedState =
                pVrWeapon->GetRenderState();
            const xbool HasWorldModel = pVrWeapon->HasVrWorldModel();
#if defined(TARGET_ANDROID)
            if( Item == INVEN_WEAPON_DESERT_EAGLE )
            {
                static xbool ReportedVrPistolModel = FALSE;
                if( !ReportedVrPistolModel )
                {
                    __android_log_print( ANDROID_LOG_INFO, "A51VR",
                        "vr pistol visual model=%s",
                        HasWorldModel ? "NPC" : "PLAYER fallback" );
                    ReportedVrPistolModel = TRUE;
                }
            }
#endif
            pVrWeapon->SetRenderState(
                HasWorldModel
                    ? new_weapon::RENDER_STATE_NPC
                    : new_weapon::RENDER_STATE_PLAYER );
            pVrWeapon->RenderWeapon( FALSE, GetFloorColor(),
                                     ( m_CloakState == CLOAKING_ON ) );
            pVrWeapon->SetRenderState( SavedState );
        }

        /* Show one magazine either in the free hand or under the shoulder.
         * The game has no separate magazine asset, so use its matching small
         * ammo pickup geometry as the visible magazine prop. */
        inven_item MagazineWeapon = INVEN_NULL;
        s32 MagazineHand = -1;
        for( s32 Hand = 0; Hand < 2; ++Hand )
        {
            if( m_VrAmmoHandWeapon[Hand] != INVEN_NULL )
            {
                MagazineWeapon = m_VrAmmoHandWeapon[Hand];
                MagazineHand = Hand;
                break;
            }
        }

        if( MagazineWeapon == INVEN_NULL )
        {
            MagazineWeapon = m_CurrentWeaponItem;
            new_weapon* pCurrentWeapon = GetWeaponPtr( MagazineWeapon );
            if( !pCurrentWeapon ||
                ( pCurrentWeapon->GetAmmoCount(
                      new_weapon::AMMO_PRIMARY ) > 0 ) ||
                ( pCurrentWeapon->GetTotalPrimaryAmmo() <= 0 ) )
            {
                MagazineWeapon = INVEN_NULL;
            }
        }

        if( MagazineWeapon != INVEN_NULL )
        {
            new_weapon* pMagazineWeapon = GetWeaponPtr( MagazineWeapon );
            const inven_item AmmoItem =
                inventory2::WeaponToAmmo( MagazineWeapon );
            if( pMagazineWeapon && AmmoItem != INVEN_NULL )
            {
                if( m_VrAmmoVisualItem != AmmoItem )
                {
                    const f32 AmmoPerClip = (f32)pMagazineWeapon->GetAmmoPerClip(
                        new_weapon::AMMO_PRIMARY );
                    m_VrAmmoVisual.SetUpRigidGeom(
                        inventory2::ItemToPickupGeomName(
                            AmmoItem, MAX( 1.0f, AmmoPerClip ) ) );
                    m_VrAmmoVisualItem = AmmoItem;
                }

                if( m_VrAmmoVisual.GetRigidGeom() )
                {
                    matrix4 MagazineTransform;
                    xbool bRenderMagazine = FALSE;
                    if( MagazineHand >= 0 )
                    {
                        bRenderMagazine = GetVrHandTransform(
                            MagazineHand, MagazineTransform );
                    }
                    else
                    {
                        const s32 RootBone = m_Loco.m_Player.GetBoneIndex(
                            "B_01_Root" );
                        if( RootBone >= 0 )
                        {
                            MagazineTransform =
                                m_Loco.m_Player.GetBoneL2W( RootBone );
                            MagazineTransform.ClearTranslation();
                            MagazineTransform.ClearScale();
                            vector3 MagazinePos = GetPosition();
                            MagazinePos.GetY() -=
                                m_fCurrentCrouchFactor * 70.0f;
                            MagazinePos += MagazineTransform.RotateVector(
                                vector3( 0.0f, 132.0f, 26.0f ) );
                            MagazineTransform.SetTranslation( MagazinePos );
                            bRenderMagazine = TRUE;
                        }
                    }

                    if( bRenderMagazine )
                    {
                        const u32 RenderFlags =
                            ( GetFlagBits() & object::FLAG_CHECK_PLANES )
                                ? render::CLIPPED : 0;
                        m_VrAmmoVisual.Render( &MagazineTransform,
                                               RenderFlags );
                    }
                }
            }
        }
        return;
    }

    new_weapon* pWeapon = GetCurrentWeaponPtr();
    if( IsVrAvatarMode() && pWeapon &&
        ( m_CurrentWeaponItem == INVEN_WEAPON_DESERT_EAGLE ) )
    {
        /* Player render resources are initialized for the local weapon. The
         * separate NPC weapon rig is not loaded for this single-player actor. */
        pWeapon->SetRenderState( new_weapon::RENDER_STATE_PLAYER );
        pWeapon->RenderWeapon( FALSE, GetFloorColor(),
                               ( m_CloakState == CLOAKING_ON ) );
        return;
    }

    actor::OnRenderWeapon();
}

void player::OnRender( void )
{
    X_PROFILE_SCOPE_CATEGORY( "Context", "player::OnRender" );

    //
    // Make sure 1st person/3rd person weapon is in correct position for rendering!
    // This chunk of code makes sure that the weapon is in the right place for the
    // weapon pullback calculations, preventing it from being in an avatar position,
    // when it's supposed to be in the player rig's hands
    //
#ifdef X_EDITOR
    const xbool bIsSplitScreen = FALSE;
#else
    const xbool bIsSplitScreen = (g_NetworkMgr.GetLocalPlayerCount() > 1);
#endif
    if( bIsSplitScreen )
    {
        if( IsAvatar() )
        {
            // this will move the weapon into the avatar's hands
            actor::MoveWeapon( TRUE );  // 3rd person
        }
        else
        {
            // this will move the weapon into the player rig hands
            OnMoveWeapon(); // 1st person
        }
    }

    UpdateWeaponPullback();

#ifndef X_EDITOR
    // If the rendering isn't short circuited then dead players will
    // still be rendered in split screen as their corpse falls out of them.
    if( IsDead() && (g_RenderContext.NetPlayerSlot != m_NetSlot) )
    {
        return;
    }
#endif

    if( m_DeathCamera.IsActive() && IsDead() )
    {
        return;
    }

    if ( !m_bIsMutated )
    {
        if ( m_Inventory2.GetAmount( INVEN_GLOVES ) > 0.0f )
        {
            // set virtual mesh for gloves
            m_Skin.SetVMeshBit( "MESH_Arms_Hazmat",  TRUE  );
            m_Skin.SetVMeshBit( "MESH_Hands_Bare",   FALSE );
            m_Skin.SetVMeshBit( "MESH_Hands_Hazmat", TRUE  );

        }
        else
        {
            // set virtual mesh for no gloves
            m_Skin.SetVMeshBit( "MESH_Arms_Hazmat",  TRUE  );
            m_Skin.SetVMeshBit( "MESH_Hands_Bare",   TRUE  );
            m_Skin.SetVMeshBit( "MESH_Hands_Hazmat", FALSE );
        }
    }

#if defined(X_EDITOR)
    if ( g_ShowPlayerPos )
    {
        vector3 Pos = GetPosition();
        x_printfxy( 1, 2, "Player( %7.1f, %7.1f, %7.1f )", Pos.GetX(), Pos.GetY(), Pos.GetZ() );
    }
#endif

    // We need to render debug stuff at least, if sniper zoom is enabled
    RenderAimAssistDebugInfo();

    if( (RenderSniperZoom() && !IsVrAvatarMode()) || // zoom only suppresses flat first-person arms
        IsCinemaRunning() ||                    // if we are playing a cinematic, don't draw arms
        (m_bHidePlayerArms && !IsAvatar()) )    // Has a trigger or something turned off our arms?
    {
        // KSS -- FIXME -- HACK -- This will cause sniper zoom on moving platforms to now work.
        // PREVIOUSLY, you would get locked in and were not able to YAW at all.
        const matrix4& mat = GetL2W();
        (void)mat;

        return;
    }

    if( !IsAvatar() && !IsVrAvatarMode() )
    {
        // gather flags and ambient color
        xcolor Ambient;
        u32    Flags = (GetFlagBits() & object::FLAG_CHECK_PLANES) ? render::CLIPPED : 0;
        if ( g_RenderContext.m_bIsMutated && !g_RenderContext.m_bIsPipRender && m_bAllowedToGlow )
        {
            Flags  |= render::GLOWING;

            // TODO: Fill in the logic for determining if this is friend or foe.

            // TODO: This color should come from the blueprint properties (m_EnemyGlowColor)
            Ambient = xcolor(255,200,200,255);
        }
        else
        {
            Ambient = GetFloorColor();
        }


#ifdef X_EDITOR
        if( m_bRenderBBox )
        {
            if( GetAttrBits() & ATTR_EDITOR_SELECTED )
            {
                render::debug::Box( GetBBox(), XCOLOR_RED );
                render::debug::Frustum( GetRenderView() );
            }
        }
#endif // X_EDITOR

        if ( !m_bActivePlayer )
            return;

        if( m_LocalSlot == -1 )
            return;

#if defined(X_EDITOR)
        const view* ActiveView = eng_GetView();
        if ( ActiveView && ((ActiveView->GetPosition() - GetRenderView().GetPosition()).LengthSquared() > 0.5f) )
        {
           return;
        }
#endif // X_EDITOR

        void* pPtr1 = m_AnimGroup.GetPointer();
        void* pPtr2 = m_Skin.GetSkinGeom();

        // Don't render the player arms if he doesn't have a weapon
        // GaryW -> Commented out GetCurrentWeaponPtr() because it was
        // causing a bug where the player was unable to turn while standing
        // on an Anim Surface.  Bug was added to the BugBase to have the
        // appropriate person find a resultion to this problem.
        if(    pPtr1 
            && pPtr2 
            && (GetCurrentWeaponPtr() || (m_CurrentAnimState == ANIM_STATE_DEATH))
            && !(m_bIsMutated && (m_CurrentAnimState == ANIM_STATE_DEATH)) )
        {
            s32            nBones    = m_AnimPlayer.GetNBones();
            matrix4*       pBone     = (matrix4*)smem_BufferAlloc( nBones * sizeof( matrix4 ) );
            const matrix4* pAnimBone = m_AnimPlayer.GetBoneL2Ws();
            const vector3& WeaponCollisionOffset = GetCurrentWeaponCollisionOffset();
            for( s32 i=0; i<nBones; i++ )
            {
                pBone[i] = pAnimBone[i];

                pBone[i].Translate( WeaponCollisionOffset );
            }

#if !defined( CONFIG_RETAIL )
            if( m_bRenderSkeleton )
            {
                m_AnimPlayer.RenderSkeleton( m_bRenderSkeletonNames );
            }
#endif // !defined( CONFIG_RETAIL )

            // Handle fade-in on spawn
            if( m_SpawnFadeTime > 0.0f )
            {
                Flags |= render::FADING_ALPHA;
                f32 Alpha = 1.0f - (m_SpawnFadeTime / g_SpawnFadeTime);
                Alpha = MIN( Alpha, 1.0f );
                Alpha = MAX( Alpha, 0.0f );
                Ambient.A  = (u8)(Alpha*255.0f);
            }

            skin_inst& SkinInst = m_Skin;
            SkinInst.Render( &GetL2W(),
                            pBone, 
                            nBones, 
                            Flags | render::CLIPPED | render::DISABLE_SPOTLIGHT, 
                            SkinInst.GetLODMask(GetL2W()),
                            Ambient );
        }
        else
        if( !GetCurrentWeaponPtr() )
        {
            // KSS -- FIXME -- HACK -- This will cause no weapon on moving platforms to now work.
            // PREVIOUSLY, you would get locked in and were not able to YAW at all.
            const matrix4& mat = GetL2W();
            (void)mat;
        }

        if ( m_CurrentAnimState == ANIM_STATE_CHANGE_MUTATION && ( m_AnimStage > 1 ) && ( m_AnimStage < 3 ) ) //stage 1 is the switch from
        {
            //special case
            return;
        }

        //render weapon
        new_weapon* pWeapon = GetCurrentWeaponPtr();
        if ( pWeapon )
        {
            pWeapon->SetRenderState( new_weapon::RENDER_STATE_PLAYER );
            //AttachWeapon();
            pWeapon->RenderWeapon( TRUE, Ambient, FALSE );
        }
    }
    else
    {
#if defined( A51_ENABLE_OPENXR )
        if( IsVrAvatarMode() )
        {
            /* The headset is inside this third-person avatar. Hide head
             * virtual meshes by name only for this draw, then restore the
             * actor's mask before weapon/effect draws or later views. */
            const virtual_mesh_mask SavedMask = m_SkinInst.GetVMeshMask();
            geom* pAvatarGeom = m_SkinInst.GetGeom();
            static s32 s_VrBodyTraceCount = 0;
            if( s_VrBodyTraceCount < 8 )
            {
                const skin_geom* pAvatarSkin = m_SkinInst.GetSkinGeom();
                const s32 BodyVMesh = pAvatarGeom ? pAvatarGeom->GetVMeshIndex( "BODY" ) : -1;
                const u64 LODMask = pAvatarGeom
                                  ? m_SkinInst.GetLODMask( GetL2W() )
                                  : 0;
                x_DebugMsg( "[VR_BODY] render=%d geom=%s geomPtr=%p skin=%p meshes=%d bones=%d vmask=%08x lod=%08llx body=%d anim=%s loaded=%d\n",
                            s_VrBodyTraceCount,
                            m_SkinInst.GetSkinGeomName() ? m_SkinInst.GetSkinGeomName() : "(null)",
                            pAvatarGeom,
                            pAvatarSkin,
                            pAvatarGeom ? pAvatarGeom->m_nVirtualMeshes : -1,
                            pAvatarSkin ? pAvatarSkin->m_nBones : -1,
                            SavedMask.VMeshMask,
                            (unsigned long long)LODMask,
                            BodyVMesh,
                            m_hAnimGroup.GetName(),
                            m_Loco.IsAnimLoaded() );
                if( pAvatarGeom && s_VrBodyTraceCount == 0 )
                {
                    for( s32 iMesh = 0; iMesh < pAvatarGeom->m_nVirtualMeshes; ++iMesh )
                    {
                        x_DebugMsg( "[VR_BODY] vmesh[%d]=%s\n",
                                    iMesh,
                                    pAvatarGeom->GetVMeshName( iMesh ) );
                    }
                }
                ++s_VrBodyTraceCount;
            }
            if( pAvatarGeom )
            {
                const s32 nMeshes = MIN( pAvatarGeom->m_nVirtualMeshes,
                                         virtual_mesh_mask::MAX_VMESHES );
                for( s32 iMesh = 0; iMesh < nMeshes; ++iMesh )
                {
                    const char* pMeshName = pAvatarGeom->GetVMeshName( iMesh );
                    if( pMeshName &&
                        ( x_stristr( pMeshName, "HEAD" ) ||
                          x_stristr( pMeshName, "FACE" ) ||
                          x_stristr( pMeshName, "HELMET" ) ) )
                        m_SkinInst.SetVMeshBit( iMesh, FALSE );

                    /* In a crouched first-person view, keep the arms and
                     * upper torso visible but remove separately modeled
                     * lower-body pieces from the near-camera view. Some
                     * campaign skins combine these parts into BODY, so only
                     * disable virtual meshes explicitly named as legs/feet. */
                    if( pMeshName && m_fCurrentCrouchFactor > 0.01f &&
                        ( x_stristr( pMeshName, "LEG" ) ||
                          x_stristr( pMeshName, "THIGH" ) ||
                          x_stristr( pMeshName, "SHIN" ) ||
                          x_stristr( pMeshName, "KNEE" ) ||
                          x_stristr( pMeshName, "BOOT" ) ||
                          x_stristr( pMeshName, "FOOT" ) ||
                          x_stristr( pMeshName, "PANTS" ) ) )
                        m_SkinInst.SetVMeshBit( iMesh, FALSE );
                }
            }

            actor::OnRender();
            m_SkinInst.SetVMeshMask( SavedMask.VMeshMask );
        }
        else
#endif
        {
        actor::OnRender();
        }
        new_weapon* pWeapon = GetCurrentWeaponPtr();
        if( pWeapon )
            pWeapon->SetRenderState( new_weapon::RENDER_STATE_PLAYER );
    }

}

void player::OnRenderShadowCast( u64 ProjMask )
{
    if( IsAvatar() )
    {
        actor::OnRenderShadowCast( ProjMask );
    }
}

xbool player::RenderSniperZoom( void )
{
    return (m_CurrentWeaponItem == INVEN_WEAPON_SNIPER_RIFLE)
        && m_bActivePlayer
        && (   (m_CurrentAnimState == ANIM_STATE_ZOOM_IDLE)
            || (m_CurrentAnimState == ANIM_STATE_ZOOM_RUN)
            || (m_CurrentAnimState == ANIM_STATE_ZOOM_FIRE));
}

#ifdef X_EDITOR

void player::OnEditorRender( void )
{
    const view* ActiveView = eng_GetView();

    if ( ActiveView && ((ActiveView->GetPosition() - GetRenderView().GetPosition()).LengthSquared() > 0.5f) )
    {
        //
        // Draw the player orientation axes
        //
        vector3 Z           ( 0.0f,    0.0f,   150.0f  );
        vector3 EyesPosition( GetPosition() + vector3( 0.0f, 172.5f, 0.0f ) + m_EyesOffset );
        matrix4 L2W;
        L2W.Identity();
        L2W.Rotate( radian3( GetPitch(), GetYaw(), 0.0f ) );
        L2W.Translate( EyesPosition );
        Z = L2W.Transform( Z );

        const render::primitive_draw_desc Material( NULL,
                                                    render::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                                    render::PRIMITIVE_BLEND_OPAQUE,
                                                    render::PRIMITIVE_DEPTH_READ_WRITE,
                                                    render::PRIMITIVE_RASTER_SOLID,
                                                    render::PRIMITIVE_SAMPLER_LINEAR_CLAMP,
                                                    render::PRIMITIVE_LAYER_SURFACE );
        render::PrimitiveBatch Batch( Material );
        // draw a cone
        // draw each vertex in the z plane then transform it
        static const s32    nVertex    = 25;
        Batch.Reserve( nVertex * 6, nVertex * 6 );
        static const f32    Radius  = 10.0f;
        static const radian Step    = R_360 / nVertex;
        radian              Angle   = Step;
        s32                 i;
        const vector3       Center      ( L2W.Transform( vector3( 0.0f, Radius, 0.0f ) ) );
        vector3             LastVertex  ( Center );

        for ( i = 0; i < nVertex; ++i )
        {
            vector3 Vertex( 0.0f, Radius, 0.0f );
            Vertex.RotateZ( Angle );
            Vertex = L2W.Transform( Vertex );

            plane Plane( LastVertex, Vertex, Z );
            s32 ColorShade = (s32)((Plane.Normal.GetY() + 1.0f) * 127);
            const xcolor OuterColor( 0, (u8)ColorShade, (u8)ColorShade );
            Batch.AddTriangle( render::primitive_vertex( LastVertex, vector2( 0.0f, 0.0f ), OuterColor ),
                               render::primitive_vertex( Vertex, vector2( 0.0f, 0.0f ), OuterColor ),
                               render::primitive_vertex( Z, vector2( 0.0f, 0.0f ), OuterColor ) );

            Plane.Setup( Vertex, LastVertex, Center );
            ColorShade = (s32)((Plane.Normal.GetY() + 1.0f) * 127);
            const xcolor InnerColor( 0, (u8)ColorShade, (u8)ColorShade );
            Batch.AddTriangle( render::primitive_vertex( Vertex, vector2( 0.0f, 0.0f ), InnerColor ),
                               render::primitive_vertex( LastVertex, vector2( 0.0f, 0.0f ), InnerColor ),
                               render::primitive_vertex( Center, vector2( 0.0f, 0.0f ), InnerColor ) );

            LastVertex = Vertex;
            Angle += Step;
        }

        matrix4 Identity;
        Identity.Identity();
        Batch.Submit( Identity );
    }
}

#endif // X_EDITOR

const vector3& player::GetCurrentWeaponCollisionOffset( void ) const
{
    return m_WeaponCollisionOffset;
}

#ifndef X_RETAIL

void player::OnColRender( xbool bRenderHigh )
{    
    (void)bRenderHigh;
    m_Physics.RenderCollision();
} 

#endif // X_RETAIL
