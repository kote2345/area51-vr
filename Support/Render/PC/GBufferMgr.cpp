//==============================================================================
//
//  GBufferMgr.cpp
//
//==============================================================================

#include "x_types.hpp"

//==============================================================================
//  INCLUDES
//==============================================================================

#include "GBufferMgr.hpp"
#include "e_Engine.hpp"

//==============================================================================
//  INTERNAL CONSTANTS
//==============================================================================

#define GBUFFER_FORMAT_FINAL_COLOR RTARGET_FORMAT_RGBA8
#if defined(A51_GBUFFER_LOW_BW)
#define GBUFFER_FORMAT_NORMAL_DEPTH RTARGET_FORMAT_RGBA8
#define GBUFFER_FORMAT_GLOW RTARGET_FORMAT_RGBA8
#else
#define GBUFFER_FORMAT_NORMAL_DEPTH RTARGET_FORMAT_RGBA16F
#define GBUFFER_FORMAT_GLOW RTARGET_FORMAT_RGBA16F
#endif
#define GBUFFER_FORMAT_DEPTH RTARGET_FORMAT_DEPTH_STENCIL

//------------------------------------------------------------------------------

enum
{
    MRT_NORMAL_DEPTH = 0,
    MRT_GLOW = 1
};

//------------------------------------------------------------------------------

enum
{
    BIND_SLOT_FINAL_COLOR = 0,
    BIND_SLOT_NORMAL_DEPTH = 1,
    BIND_SLOT_GLOW = 2,
    BIND_SLOT_COUNT
};

//------------------------------------------------------------------------------

static f32 const s_ClearColorZero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
static f32 const s_ClearColorNormalDepth[4] = { 0.5f, 0.5f, 1.0f, 1.0f };

//------------------------------------------------------------------------------

static void gbuffer_SetClearColor( f32 pDst[4], f32 const pSrc[4] )
{
    pDst[0] = pSrc[0];
    pDst[1] = pSrc[1];
    pDst[2] = pSrc[2];
    pDst[3] = pSrc[3];
}

//------------------------------------------------------------------------------

static rtarget_color_attachment_desc gbuffer_ColorAttachment( rtarget const* pTarget, rtarget_load_op loadOp,
                                                              f32 const* pClearColor )
{
    rtarget_color_attachment_desc desc;
    desc.pTarget = pTarget;
    desc.LoadOp = loadOp;
    desc.StoreOp = RTARGET_STORE_STORE;

    if ( pClearColor )
    {
        gbuffer_SetClearColor( desc.ClearColor, pClearColor );
    }

    return desc;
}

//------------------------------------------------------------------------------

static rtarget_depth_attachment_desc gbuffer_DepthAttachment( rtarget const* pTarget, rtarget_load_op loadOp )
{
    rtarget_depth_attachment_desc desc;
    desc.pTarget = pTarget;
    desc.DepthLoadOp = loadOp;
    desc.DepthStoreOp = RTARGET_STORE_STORE;
    desc.StencilLoadOp = loadOp;
    desc.StencilStoreOp = RTARGET_STORE_STORE;
    desc.ClearDepth = 1.0f;
    desc.ClearStencil = 0;
    return desc;
}

//==============================================================================
//  GLOBAL INSTANCE
//==============================================================================

GBufferMgr g_GBufferMgr;

//==============================================================================
//  CONSTRUCTION / DESTRUCTION
//==============================================================================

GBufferMgr::GBufferMgr( void )
    : m_isInitialized( FALSE ), m_isGBufferValid( FALSE ), m_areGBufferTargetsActive( FALSE ),
      m_isSceneColorRenderedThisFrame( FALSE ), m_clearGBufferOnBind( FALSE ), m_gBufferWidth( 0 ),
      m_gBufferHeight( 0 ), m_gBufferLayerCount( 1 ), m_multiviewEnabled( FALSE ),
      m_directRenderEnabled(
#if defined(A51_DIRECT_RENDER)
          TRUE
#else
          FALSE
#endif
      ),
      m_pOverrideColor( NULL ), m_pOverrideDepth( NULL )
{
}

//==============================================================================

GBufferMgr::~GBufferMgr( void )
{
}

//==============================================================================

void GBufferMgr::Init( void )
{
    if ( m_isInitialized )
    {
        return;
    }

    m_isInitialized = TRUE;
}

//==============================================================================

void GBufferMgr::Kill( void )
{
    if ( !m_isInitialized )
    {
        return;
    }

    DestroyGBuffer();
    m_isInitialized = FALSE;
}

//==============================================================================

void GBufferMgr::BeginFrame( void )
{
    m_isSceneColorRenderedThisFrame = FALSE;
    m_clearGBufferOnBind = TRUE;
}

//==============================================================================

void GBufferMgr::SetMultiviewEnabled( xbool enabled )
{
    if( m_multiviewEnabled == enabled )
        return;

    m_multiviewEnabled = enabled;
    m_gBufferLayerCount = enabled ? 2 : 1;
    DestroyGBuffer();
}

//==============================================================================
//  G-BUFFER LIFETIME
//==============================================================================

xbool GBufferMgr::CreateTarget( rtarget& target, rtarget_format format, f32 const* pClearColor, char const* pErrorMsg )
{
    rtarget_desc desc;
    desc.Width          = m_gBufferWidth;
    desc.Height         = m_gBufferHeight;
    desc.LayerCount     = m_gBufferLayerCount;
    desc.Format         = format;
    desc.SampleCount    = 1;
    desc.SampleQuality  = 0;
    desc.bBindAsTexture = TRUE;
    desc.pDebugName     = pErrorMsg;

    if ( pClearColor )
    {
        gbuffer_SetClearColor( desc.ClearColor, pClearColor );
    }

    if ( rtarget_Create( target, desc ) )
    {
        return TRUE;
    }

    DestroyGBuffer();
    x_throw( pErrorMsg );
    return FALSE;
}

//==============================================================================

xbool GBufferMgr::InitGBuffer( u32 width, u32 height )
{
    if ( !rtarget_GetBackBuffer() )
    {
        return FALSE;
    }

    if ( m_isGBufferValid && ( m_gBufferWidth == width ) && ( m_gBufferHeight == height ) &&
         ( m_gBufferLayerCount == ( m_multiviewEnabled ? 2u : 1u ) ) )
    {
        return TRUE;
    }

    DestroyGBuffer();

    m_gBufferWidth = width;
    m_gBufferHeight = height;

    if ( !CreateTarget( m_sceneColorTarget, GBUFFER_FORMAT_FINAL_COLOR, s_ClearColorZero,
                        "Failed to create GBuffer Scene Color target" ) )
    {
        return FALSE;
    }

    if ( !m_directRenderEnabled )
    {
        if ( !CreateTarget( m_gBufferTarget[MRT_NORMAL_DEPTH], GBUFFER_FORMAT_NORMAL_DEPTH, s_ClearColorNormalDepth,
                            "Failed to create GBuffer Normal-Depth target" ) )
        {
            return FALSE;
        }

        if ( !CreateTarget( m_gBufferTarget[MRT_GLOW], GBUFFER_FORMAT_GLOW, s_ClearColorZero,
                            "Failed to create GBuffer Glow target" ) )
        {
            return FALSE;
        }
    }

    if ( !CreateTarget( m_gBufferDepth, GBUFFER_FORMAT_DEPTH, NULL, "Failed to create GBuffer Depth-Stencil" ) )
    {
        return FALSE;
    }

    m_isGBufferValid = TRUE;
    m_clearGBufferOnBind = TRUE;
    // This function may be called for both the desktop mirror and XR target
    // during one frame. Keep the path diagnostic one-shot to avoid turning a
    // resize/rebind into a per-frame log stream on the game thread.
    static xbool s_pathLogged = FALSE;
    if ( !s_pathLogged )
    {
        x_DebugMsg( "GBufferMgr: path=%s size=%ux%u layers=%u auxiliary=%s\n",
                    m_directRenderEnabled ? "direct" : "gbuffer",
                    width,
                    height,
                    m_multiviewEnabled ? 2u : 1u,
                    m_directRenderEnabled ? "disabled" : "normal-depth+glow" );
        s_pathLogged = TRUE;
    }
    return TRUE;
}

//==============================================================================

void GBufferMgr::SetDirectRenderEnabled( xbool enabled )
{
    if ( m_directRenderEnabled == enabled )
    {
        return;
    }

    m_directRenderEnabled = enabled;
    DestroyGBuffer();
}

//==============================================================================

void GBufferMgr::DestroyGBuffer( void )
{
    if ( m_areGBufferTargetsActive )
    {
        rtarget_EndPass();
        m_areGBufferTargetsActive = FALSE;
    }

    rtarget_Destroy( m_sceneColorTarget );

    for ( u32 i = 0; i < g_gBufferMrtCount; ++i )
    {
        rtarget_Destroy( m_gBufferTarget[i] );
    }

    rtarget_Destroy( m_gBufferDepth );

    m_isGBufferValid = FALSE;
    m_isSceneColorRenderedThisFrame = FALSE;
    m_clearGBufferOnBind = FALSE;
    m_gBufferWidth = 0;
    m_gBufferHeight = 0;
}

//==============================================================================

xbool GBufferMgr::ResizeGBuffer( u32 width, u32 height )
{
    return InitGBuffer( width, height );
}

//==============================================================================

xbool GBufferMgr::SetGBufferTargets( void )
{
    if ( !m_isGBufferValid )
    {
        x_throw( "SetGBufferTargets called before a valid G-Buffer was created" );
        return FALSE;
    }

    if ( m_areGBufferTargetsActive && !m_clearGBufferOnBind )
    {
        if ( !m_pOverrideColor )
        {
            m_isSceneColorRenderedThisFrame = TRUE;
        }
        return TRUE;
    }

    if ( !rtarget_GetBackBuffer() )
    {
        x_throw( "Back buffer is not available" );
        return FALSE;
    }

    rtarget const* pSceneColor = GetActiveSceneColor();
    rtarget const* pDepthTarget = GetActiveDepthTarget();
    if ( !pSceneColor || !pDepthTarget )
    {
        return FALSE;
    }

    if ( !rtarget_HasRenderTarget( *pSceneColor ) || !rtarget_HasDepthStencil( *pDepthTarget ) )
    {
        return FALSE;
    }

    if ( !m_directRenderEnabled &&
         ( !rtarget_HasRenderTarget( m_gBufferTarget[MRT_NORMAL_DEPTH] ) ||
           !rtarget_HasRenderTarget( m_gBufferTarget[MRT_GLOW] ) ) )
    {
        return FALSE;
    }

    rtarget_load_op const loadOp = m_clearGBufferOnBind ? RTARGET_LOAD_CLEAR : RTARGET_LOAD_LOAD;

    rtarget_color_attachment_desc boundTargets[BIND_SLOT_COUNT];
    boundTargets[BIND_SLOT_FINAL_COLOR] = gbuffer_ColorAttachment( pSceneColor, loadOp, s_ClearColorZero );
    boundTargets[BIND_SLOT_NORMAL_DEPTH] =
        gbuffer_ColorAttachment( &m_gBufferTarget[MRT_NORMAL_DEPTH], loadOp, s_ClearColorNormalDepth );
    boundTargets[BIND_SLOT_GLOW] = gbuffer_ColorAttachment( &m_gBufferTarget[MRT_GLOW], loadOp, s_ClearColorZero );

    rtarget_depth_attachment_desc depth = gbuffer_DepthAttachment( pDepthTarget, loadOp );

    rtarget_EndPass();
    m_areGBufferTargetsActive = FALSE;
    rtarget_pass_desc pass;
    pass.pColors = boundTargets;
    pass.ColorCount = m_directRenderEnabled ? 1 : BIND_SLOT_COUNT;
    pass.pDepthStencil = &depth;
    pass.ViewMask = m_multiviewEnabled ? 0x3u : 0u;
    if ( !rtarget_BeginPass( pass ) )
    {
        return FALSE;
    }

    view const* pView = eng_GetView();
    if ( pView )
    {
        eng_SetViewport( *pView );
    }

    m_areGBufferTargetsActive = TRUE;
    m_clearGBufferOnBind = FALSE;
    if ( !m_pOverrideColor )
    {
        m_isSceneColorRenderedThisFrame = TRUE;
    }
    return TRUE;
}

//==============================================================================

void GBufferMgr::EndPass( void )
{
    rtarget_EndPass();
    m_areGBufferTargetsActive = FALSE;
}

//==============================================================================

void GBufferMgr::SetFinalColorTarget( void )
{
    if ( !m_isGBufferValid )
    {
        return;
    }

    rtarget const* pSceneColor = GetActiveSceneColor();
    rtarget const* pDepthTarget = GetActiveDepthTarget();
    if ( !pSceneColor || !pDepthTarget )
    {
        return;
    }

    if ( !rtarget_HasRenderTarget( *pSceneColor ) || !rtarget_HasDepthStencil( *pDepthTarget ) )
    {
        return;
    }

    rtarget_color_attachment_desc color = gbuffer_ColorAttachment( pSceneColor, RTARGET_LOAD_LOAD, NULL );
    rtarget_depth_attachment_desc depth = gbuffer_DepthAttachment( pDepthTarget, RTARGET_LOAD_LOAD );

    rtarget_EndPass();
    m_areGBufferTargetsActive = FALSE;
    rtarget_pass_desc pass;
    pass.pColors = &color;
    pass.ColorCount = 1;
    pass.pDepthStencil = &depth;
    pass.ViewMask = m_multiviewEnabled ? 0x3u : 0u;
    rtarget_BeginPass( pass );
    m_areGBufferTargetsActive = FALSE;
}

//==============================================================================

void GBufferMgr::PresentFinalColor( void )
{
    if( m_multiviewEnabled )
        return;

    if ( !m_isGBufferValid || !m_isSceneColorRenderedThisFrame || !rtarget_HasShaderResource( m_sceneColorTarget ) )
    {
        return;
    }

    if ( m_pOverrideColor )
    {
        return;
    }

    rtarget_EndPass();
    m_areGBufferTargetsActive = FALSE;

    rtarget_backbuffer_pass_desc passDesc;
    passDesc.ColorLoadOp = RTARGET_LOAD_DONT_CARE;
    passDesc.bUseDepth = FALSE;
    if ( !rtarget_BeginBackBufferPass( passDesc ) )
    {
        return;
    }

    composite_Blit( m_sceneColorTarget, COMPOSITE_BLEND_COPY, 1.0f, NULL, RSTATE_SAMPLER_PRESET_POINT_CLAMP );
}

//==============================================================================

void GBufferMgr::ClearGBuffer( void )
{
    if ( !m_isGBufferValid )
    {
        return;
    }

    if ( m_areGBufferTargetsActive )
    {
        return;
    }

    m_clearGBufferOnBind = TRUE;
}

//==============================================================================

rtarget const* GBufferMgr::GetGBufferTarget( GBufferTarget target ) const
{
    if ( !m_isGBufferValid )
    {
        return NULL;
    }

    switch ( target )
    {
        case GBufferTarget::FinalColor:
        {
            return GetActiveSceneColor();
        }
        case GBufferTarget::Depth:
        {
            return GetActiveDepthTarget();
        }
        case GBufferTarget::NormalDepth:
        {
            return m_directRenderEnabled ? NULL : &m_gBufferTarget[MRT_NORMAL_DEPTH];
        }
        case GBufferTarget::Glow:
        {
            return m_directRenderEnabled ? NULL : &m_gBufferTarget[MRT_GLOW];
        }
        default:
        {
            return NULL;
        }
    }
}

//==============================================================================

void GBufferMgr::GetGBufferSize( u32& width, u32& height ) const
{
    width = m_gBufferWidth;
    height = m_gBufferHeight;
}

//==============================================================================
//  TARGET OVERRIDE
//==============================================================================

void GBufferMgr::SetTargetOverride( rtarget const* pColor, rtarget const* pDepth )
{
    m_pOverrideColor = pColor;
    m_pOverrideDepth = pDepth;
}

//==============================================================================

xbool GBufferMgr::GetFrameTargets( frame_render_targets& targets ) const
{
    targets = frame_render_targets();

    if ( !m_isGBufferValid )
    {
        return FALSE;
    }

    targets.IsTargetOverride = m_pOverrideColor != NULL;
    targets.pSceneColor = GetActiveSceneColor();
    targets.pSceneDepth = GetActiveDepthTarget();
    targets.pGlow = ( targets.IsTargetOverride || m_directRenderEnabled ) ? NULL : &m_gBufferTarget[MRT_GLOW];

    if ( !targets.pSceneColor || !rtarget_HasRenderTarget( *targets.pSceneColor ) )
    {
        return FALSE;
    }

    if ( targets.pSceneDepth && !rtarget_HasDepthStencil( *targets.pSceneDepth ) )
    {
        return FALSE;
    }

    if ( targets.pSceneDepth && ( ( targets.pSceneDepth->Desc.Width != targets.pSceneColor->Desc.Width ) ||
                                  ( targets.pSceneDepth->Desc.Height != targets.pSceneColor->Desc.Height ) ||
                                  ( targets.pSceneDepth->Desc.SampleCount != targets.pSceneColor->Desc.SampleCount ) ) )
    {
        return FALSE;
    }

    if ( targets.pGlow &&
         ( !rtarget_HasRenderTarget( *targets.pGlow ) ||
           ( targets.pGlow->Desc.Width != targets.pSceneColor->Desc.Width ) ||
           ( targets.pGlow->Desc.Height != targets.pSceneColor->Desc.Height ) ||
           ( targets.pGlow->Desc.SampleCount != targets.pSceneColor->Desc.SampleCount ) ) )
    {
        targets.pGlow = NULL;
    }

    return TRUE;
}

//==============================================================================

rtarget const* GBufferMgr::GetActiveSceneColor( void ) const
{
    return m_pOverrideColor ? m_pOverrideColor : &m_sceneColorTarget;
}

//==============================================================================

rtarget const* GBufferMgr::GetActiveDepthTarget( void ) const
{
    return m_pOverrideColor ? m_pOverrideDepth : &m_gBufferDepth;
}
