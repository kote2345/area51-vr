//==============================================================================
//
//  sdleng.cpp
//
//==============================================================================

#include "x_target.hpp"

#if (defined(TARGET_DESKTOP) || defined(TARGET_MOBILE)) && defined(ENTROPY_RENDER_SDL)

//==============================================================================
//  INCLUDES
//==============================================================================

#include "sdleng_private.hpp"

#ifndef X_STDIO_HPP
#include "x_stdio.hpp"
#endif

//==============================================================================
//  GLOBAL BACKEND HANDLES
//==============================================================================

SDL_GPUDevice*  g_pSDLGPUDevice = NULL;

static sdleng_vulkan_device_info s_ExternalVulkanDevice;
static xbool s_bExternalVulkanDevice = FALSE;

xbool sdleng_SetVulkanExternalDevice( const sdleng_vulkan_device_info& Info )
{
    if( !Info.Instance || !Info.PhysicalDevice || !Info.Device ||
        !Info.Queue )
    {
        return FALSE;
    }

    s_ExternalVulkanDevice = Info;
    s_bExternalVulkanDevice = TRUE;
    return TRUE;
}

void sdleng_ClearVulkanExternalDevice( void )
{
    s_ExternalVulkanDevice = sdleng_vulkan_device_info();
    s_bExternalVulkanDevice = FALSE;
}

xbool sdleng_HasVulkanExternalDevice( void )
{
    return s_bExternalVulkanDevice;
}

xbool sdleng_VulkanMultiviewEnabled( void )
{
    return s_bExternalVulkanDevice && s_ExternalVulkanDevice.MultiviewEnabled;
}

xbool sdleng_GetVulkanDeviceInfo( sdleng_vulkan_device_info& Info )
{
    SDL_GPUVulkanDeviceInfo SDLInfo;
    if( !g_pSDLGPUDevice ||
        !SDL_GetGPUVulkanDeviceInfo( g_pSDLGPUDevice, &SDLInfo ) )
    {
        return FALSE;
    }

    Info.Instance          = SDLInfo.instance;
    Info.PhysicalDevice    = SDLInfo.physical_device;
    Info.Device            = SDLInfo.device;
    Info.Queue             = SDLInfo.queue;
    Info.QueueFamilyIndex  = SDLInfo.queue_family_index;
    Info.RenderWidth       = 0;
    Info.RenderHeight      = 0;
    Info.MultiviewEnabled  = FALSE;
    return TRUE;
}

xbool sdleng_GetVulkanFrameInfo( sdleng_vulkan_frame_info& Info )
{
    SDL_GPUVulkanFrameInfo SDLInfo;
    if( !g_pSDLGPUDevice || !sdleng_GetCommandBuffer() ||
        !sdleng_GetSwapchainTexture() ||
        !SDL_GetGPUVulkanFrameInfo( g_pSDLGPUDevice,
                                    sdleng_GetCommandBuffer(),
                                    sdleng_GetSwapchainTexture(),
                                    &SDLInfo ) )
    {
        return FALSE;
    }

    Info.CommandBuffer = SDLInfo.command_buffer;
    Info.SourceImage   = SDLInfo.source_image;
    Info.Width         = SDLInfo.width;
    Info.Height        = SDLInfo.height;
    return TRUE;
}

//==============================================================================
//  LOCAL STORAGE
//==============================================================================

static struct sdleng_locals
{
    sdleng_locals( void )
    {
        x_memset( this, 0, sizeof(sdleng_locals) );
        PresentPolicy     = SDLENG_PRESENT_VSYNC;
        ActivePresentMode = SDL_GPU_PRESENTMODE_VSYNC;
    }

    SDL_GPUCommandBuffer*       pCommandBuffer;
    SDL_GPURenderPass*          pRenderPass;
    SDL_GPUTexture*             pSwapchainTexture;
    SDL_GPUTexture*             pNativeXRBackBuffers[2];
    SDL_GPUTextureFormat        SwapchainFormat;
    u32                         BackBufferWidth;
    u32                         BackBufferHeight;
    u32                         NativeXRWidth;
    u32                         NativeXRHeight;
    u32                         AcquiredWidth;
    u32                         AcquiredHeight;
    sdleng_present_policy       PresentPolicy;
    SDL_GPUPresentMode          ActivePresentMode;
    xtick                       RenderStartTime;
    f32                         FramePacingWaitMs;
    f32                         RenderSubmitMs;

    xbool                       bInitialized;
    xbool                       bWindowClaimed;
    xbool                       bWindowInitializedByDevice;
    xbool                       bSwapchainAcquired;
    xbool                       bBackBufferRendered;
    xbool                       bSwapchainAcquireLogged;
    xbool                       bNativeXRBackBuffer;
    u32                         NativeXREye;
    void*                       NativeXRSourceImages[2];
} s;

xbool sdleng_GetVulkanFrameInfoForEye( u32 Eye,
                                       sdleng_vulkan_frame_info& Info )
{
    if( Eye >= 2 || !g_pSDLGPUDevice || !sdleng_GetCommandBuffer() )
    {
        x_DebugMsg( "A51XR SDL frame-info failed eye=%u device=%p command=%p\n",
                    Eye, g_pSDLGPUDevice, s.pCommandBuffer );
        return FALSE;
    }

    SDL_GPUTexture* pTexture = s.pNativeXRBackBuffers[Eye];
    if( !pTexture )
    {
        x_DebugMsg( "A51XR SDL frame-info failed eye=%u native-target=null\n",
                    Eye );
        return FALSE;
    }

    SDL_GPUVulkanFrameInfo SDLInfo;
    if( !SDL_GetGPUVulkanFrameInfo( g_pSDLGPUDevice,
                                    sdleng_GetCommandBuffer(),
                                    pTexture,
                                    &SDLInfo ) )
    {
        x_DebugMsg( "A51XR SDL frame-info failed eye=%u texture=%p\n",
                    Eye, pTexture );
        return FALSE;
    }

    Info.CommandBuffer = SDLInfo.command_buffer;
    Info.SourceImage   = s.NativeXRSourceImages[Eye]
                       ? s.NativeXRSourceImages[Eye]
                       : SDLInfo.source_image;
    Info.Width         = s.NativeXRSourceImages[Eye] ? s.NativeXRWidth  : SDLInfo.width;
    Info.Height        = s.NativeXRSourceImages[Eye] ? s.NativeXRHeight : SDLInfo.height;
    x_DebugMsg( "A51XR SDL frame-info eye=%u texture=%p queriedSrc=%p cachedSrc=%p cmd=%p outSrc=%p size=%ux%u\n",
                Eye, pTexture, SDLInfo.source_image,
                s.NativeXRSourceImages[Eye], Info.CommandBuffer,
                Info.SourceImage, Info.Width, Info.Height );
    return TRUE;
}

xbool sdleng_SetVulkanRenderEye( u32 Eye )
{
    if( !s.bNativeXRBackBuffer || Eye >= 2 ||
        !s.pCommandBuffer || s.pRenderPass ||
        !s.pNativeXRBackBuffers[Eye] )
    {
        x_DebugMsg( "A51XR SDL select-eye failed eye=%u native=%d cmd=%p pass=%p target=%p\n",
                    Eye, s.bNativeXRBackBuffer, s.pCommandBuffer,
                    s.pRenderPass,
                    (Eye < 2) ? s.pNativeXRBackBuffers[Eye] : NULL );
        return FALSE;
    }

    s.NativeXREye       = Eye;
    s.pSwapchainTexture = s.pNativeXRBackBuffers[Eye];
    s.AcquiredWidth     = s.NativeXRWidth;
    s.AcquiredHeight    = s.NativeXRHeight;
    s.BackBufferWidth   = s.NativeXRWidth;
    s.BackBufferHeight  = s.NativeXRHeight;
    SDL_GPUVulkanFrameInfo SDLInfo;
    if( !SDL_GetGPUVulkanFrameInfo( g_pSDLGPUDevice,
                                    s.pCommandBuffer,
                                    s.pNativeXRBackBuffers[Eye],
                                    &SDLInfo ) )
    {
        x_DebugMsg( "A51XR SDL select-eye failed eye=%u target=%p frame-info\n",
                    Eye, s.pNativeXRBackBuffers[Eye] );
        return FALSE;
    }
    s.NativeXRSourceImages[Eye] = SDLInfo.source_image;
    x_DebugMsg( "A51XR SDL select-eye eye=%u target=%p source=%p cmd=%p size=%ux%u\n",
                Eye, s.pSwapchainTexture, SDLInfo.source_image,
                SDLInfo.command_buffer, s.BackBufferWidth, s.BackBufferHeight );
    return TRUE;
}

//==============================================================================
//  HELPERS
//==============================================================================

enum
{
    SDLENG_DEFAULT_BACKBUFFER_WIDTH  = 1024,
    SDLENG_DEFAULT_BACKBUFFER_HEIGHT = 768
};

//==============================================================================

static
u32 sdleng_NormalizeExtent( s32 Value, u32 Fallback )
{
    return (Value > 0) ? (u32)Value : Fallback;
}

//==============================================================================

static
void sdleng_ResetFrameState( void )
{
    s.pCommandBuffer     = NULL;
    s.pRenderPass        = NULL;
    s.pSwapchainTexture  = NULL;
    s.AcquiredWidth      = 0;
    s.AcquiredHeight     = 0;
    s.bSwapchainAcquired = FALSE;
    s.bBackBufferRendered = FALSE;
}

//==============================================================================

static
void sdleng_ResetDeviceState( void )
{
    sdleng_ResetFrameState();

    s.SwapchainFormat            = SDL_GPU_TEXTUREFORMAT_INVALID;
    s.ActivePresentMode          = SDL_GPU_PRESENTMODE_VSYNC;
    s.BackBufferWidth            = 0;
    s.BackBufferHeight           = 0;
    s.NativeXRWidth              = 0;
    s.NativeXRHeight             = 0;
    s.FramePacingWaitMs          = 0.0f;
    s.RenderSubmitMs             = 0.0f;
    s.bInitialized               = FALSE;
    s.bWindowClaimed             = FALSE;
    s.bWindowInitializedByDevice = FALSE;
    s.bSwapchainAcquireLogged    = FALSE;
    s.pNativeXRBackBuffers[0]    = NULL;
    s.pNativeXRBackBuffers[1]    = NULL;
    s.NativeXRSourceImages[0]    = NULL;
    s.NativeXRSourceImages[1]    = NULL;
    s.bNativeXRBackBuffer        = FALSE;
    s.NativeXREye                = 0;
}

//==============================================================================

static
void sdleng_UpdateBackBufferSizeFromWindow( u32 FallbackWidth, u32 FallbackHeight )
{
    if( sdleng_WindowGetSDLWindow() )
        sdleng_WindowRefreshClientSize();

    s32 ClientWidth  = 0;
    s32 ClientHeight = 0;
    sdleng_WindowGetClientSize( ClientWidth, ClientHeight );

    s.BackBufferWidth  = (ClientWidth  > 0) ? (u32)ClientWidth  : FallbackWidth;
    s.BackBufferHeight = (ClientHeight > 0) ? (u32)ClientHeight : FallbackHeight;
}

//==============================================================================

static
xbool sdleng_EnsureWindow( sdleng_native_window_handle hWindow, u32 Width, u32 Height )
{
    SDL_Window* pExistingWindow = sdleng_WindowGetSDLWindow();
    if( pExistingWindow )
    {
        if( hWindow && sdleng_WindowGetHandle() && (sdleng_WindowGetHandle() != hWindow) )
        {
            x_DebugMsg( "SDLEngine: existing SDL window does not match requested native window\n" );
            return FALSE;
        }

        g_pSDLWindow = pExistingWindow;
        sdleng_UpdateBackBufferSizeFromWindow( Width, Height );
        return TRUE;
    }

    sdleng_window_desc WindowDesc;
    x_memset( &WindowDesc, 0, sizeof(WindowDesc) );
    WindowDesc.hWindow     = hWindow;
    WindowDesc.Width       = Width;
    WindowDesc.Height      = Height;
    WindowDesc.pTitle      = "Dreamlnd 51";
    WindowDesc.DisplayMode = sdleng_WindowGetDisplayMode();

    if( !sdleng_WindowInit( WindowDesc ) )
        return FALSE;

    g_pSDLWindow                 = sdleng_WindowGetSDLWindow();
    s.bWindowInitializedByDevice = TRUE;

    sdleng_UpdateBackBufferSizeFromWindow( Width, Height );
    return g_pSDLWindow != NULL;
}

//==============================================================================

static
xbool sdleng_ToPresentMode( sdleng_present_policy Policy, SDL_GPUPresentMode& PresentMode )
{
    switch( Policy )
    {
        case SDLENG_PRESENT_IMMEDIATE: PresentMode = SDL_GPU_PRESENTMODE_IMMEDIATE; return TRUE;
        case SDLENG_PRESENT_MAILBOX:   PresentMode = SDL_GPU_PRESENTMODE_MAILBOX;   return TRUE;
        case SDLENG_PRESENT_VSYNC:     PresentMode = SDL_GPU_PRESENTMODE_VSYNC;     return TRUE;
        default:
        {
            x_DebugMsg( "SDLEngine: invalid present policy %d\n", (s32)Policy );
            return FALSE;
        }
    }
}

//==============================================================================

static
const char* sdleng_GetPresentModeName( SDL_GPUPresentMode Mode )
{
    switch( Mode )
    {
        case SDL_GPU_PRESENTMODE_IMMEDIATE: return "immediate";
        case SDL_GPU_PRESENTMODE_MAILBOX:   return "mailbox";
        case SDL_GPU_PRESENTMODE_VSYNC:     return "vsync";
        default:                            return "unknown";
    }
}

//==============================================================================

static
xbool sdleng_ConfigureSwapchain( void )
{
    if( !g_pSDLGPUDevice || !g_pSDLWindow )
        return FALSE;

    if( !SDL_WindowSupportsGPUSwapchainComposition( g_pSDLGPUDevice,
                                                    g_pSDLWindow,
                                                    SDLENG_GPU_SWAPCHAIN_COMPOSITION ) )
    {
        x_DebugMsg( "SDLEngine: requested swapchain composition is unsupported\n" );
        return FALSE;
    }

    SDL_GPUPresentMode PresentMode;
    if( !sdleng_ToPresentMode( s.PresentPolicy, PresentMode ) )
        return FALSE;

    if( !SDL_WindowSupportsGPUPresentMode( g_pSDLGPUDevice,
                                           g_pSDLWindow,
                                           PresentMode ) )
    {
        x_DebugMsg( "SDLEngine: requested present mode '%s' is unsupported\n",
                    sdleng_GetPresentModeName( PresentMode ) );
        return FALSE;
    }

    if( !SDL_SetGPUSwapchainParameters( g_pSDLGPUDevice,
                                        g_pSDLWindow,
                                        SDLENG_GPU_SWAPCHAIN_COMPOSITION,
                                        PresentMode ) )
    {
        sdleng_LogError( "SDLEngine", "SDL_SetGPUSwapchainParameters" );
        return FALSE;
    }

    s.ActivePresentMode       = PresentMode;
    s.bSwapchainAcquireLogged = FALSE;

    s.SwapchainFormat = SDL_GetGPUSwapchainTextureFormat( g_pSDLGPUDevice, g_pSDLWindow );
    if( s.SwapchainFormat == SDL_GPU_TEXTUREFORMAT_INVALID )
    {
        x_DebugMsg( "SDLEngine: invalid swapchain texture format\n" );
        return FALSE;
    }

    sdleng_UpdateBackBufferSizeFromWindow( s.BackBufferWidth  ? s.BackBufferWidth  : SDLENG_DEFAULT_BACKBUFFER_WIDTH,
                                           s.BackBufferHeight ? s.BackBufferHeight : SDLENG_DEFAULT_BACKBUFFER_HEIGHT );
    x_DebugMsg( "SDLEngine: swapchain present=%s frames-in-flight=1 size=%ux%u\n",
                sdleng_GetPresentModeName( PresentMode ),
                s.BackBufferWidth,
                s.BackBufferHeight );

    return TRUE;
}

//==============================================================================

xbool sdleng_SetPresentPolicy( sdleng_present_policy Policy )
{
    if( (Policy < SDLENG_PRESENT_VSYNC) || (Policy > SDLENG_PRESENT_IMMEDIATE) )
        return FALSE;

    if( s.PresentPolicy == Policy )
        return TRUE;

    if( s.pCommandBuffer || s.pRenderPass || s.bSwapchainAcquired )
    {
        x_DebugMsg( "SDLEngine: present mode cannot change during an active frame\n" );
        return FALSE;
    }

    const sdleng_present_policy PreviousPolicy = s.PresentPolicy;
    s.PresentPolicy = Policy;

    if( s.bWindowClaimed && !sdleng_ConfigureSwapchain() )
    {
        s.PresentPolicy = PreviousPolicy;
        return FALSE;
    }

    return TRUE;
}

//==============================================================================

xbool sdleng_IsPresentPolicySupported( sdleng_present_policy Policy )
{
    if( !g_pSDLGPUDevice || !g_pSDLWindow || !s.bWindowClaimed )
    {
        return FALSE;
    }

    SDL_GPUPresentMode PresentMode;
    if( !sdleng_ToPresentMode( Policy, PresentMode ) )
    {
        return FALSE;
    }

    return SDL_WindowSupportsGPUPresentMode( g_pSDLGPUDevice,
                                              g_pSDLWindow,
                                              PresentMode );
}

//==============================================================================

sdleng_present_policy sdleng_GetPresentPolicy( void )
{
    if( s.bWindowClaimed )
    {
        switch( s.ActivePresentMode )
        {
            case SDL_GPU_PRESENTMODE_IMMEDIATE: return SDLENG_PRESENT_IMMEDIATE;
            case SDL_GPU_PRESENTMODE_MAILBOX:   return SDLENG_PRESENT_MAILBOX;
            case SDL_GPU_PRESENTMODE_VSYNC:
            default:                            return SDLENG_PRESENT_VSYNC;
        }
    }

    return s.PresentPolicy;
}

//==============================================================================

static
xbool sdleng_SubmitCurrentCommandBuffer( void )
{
    if( s.bNativeXRBackBuffer )
    {
        x_DebugMsg( "A51XR SDL submit begin cmd=%p acquired=%d rendered=%d pass=%p eye=%u\n",
                    s.pCommandBuffer, s.bSwapchainAcquired,
                    s.bBackBufferRendered, s.pRenderPass, s.NativeXREye );
    }
    if( !s.pCommandBuffer )
        return TRUE;

    if( s.pRenderPass )
    {
        x_DebugMsg( "SDLEngine: cannot submit with an active render pass\n" );
        return FALSE;
    }

    if( !s.bSwapchainAcquired || !s.bBackBufferRendered )
    {
        x_DebugMsg( "SDLEngine: frame submit requires an acquired and rendered backbuffer\n" );
        return FALSE;
    }

    SDL_GPUCommandBuffer* pCommandBuffer = s.pCommandBuffer;
    const xtick RenderStartTime = s.RenderStartTime;
    sdleng_ResetFrameState();

    xbool bSubmitted;
    {
        X_PROFILE_SCOPE_CATEGORY( "Renderer", "Submit" );
        bSubmitted = SDL_SubmitGPUCommandBuffer( pCommandBuffer );
    }

    if( !bSubmitted )
    {
        sdleng_LogError( "SDLEngine", "SDL_SubmitGPUCommandBuffer" );
        return FALSE;
    }

    s.RenderSubmitMs = RenderStartTime
                       ? x_TicksToMs( x_GetTime() - RenderStartTime )
                       : 0.0f;
    if( s.bNativeXRBackBuffer )
    {
        x_DebugMsg( "A51XR SDL submit done cmd=%p elapsed=%.3fms\n",
                    pCommandBuffer, s.RenderSubmitMs );
    }
    return TRUE;
}

//==============================================================================
//  BACKEND LIFETIME
//==============================================================================

xbool sdleng_CreateDeviceForWindow( sdleng_native_window_handle hWindow, s32 Width, s32 Height )
{
    x_DebugMsg( "SDLEngine: preparing window for GPU\n" );
    if( s.bInitialized )
    {
        if( !hWindow || (sdleng_WindowGetHandle() == hWindow) )
            return TRUE;

        x_DebugMsg( "SDLEngine: device already initialized for another window\n" );
        return FALSE;
    }

    const u32 RequestedWidth  = sdleng_NormalizeExtent( Width,  SDLENG_DEFAULT_BACKBUFFER_WIDTH  );
    const u32 RequestedHeight = sdleng_NormalizeExtent( Height, SDLENG_DEFAULT_BACKBUFFER_HEIGHT );

    if( !sdleng_EnsureWindow( hWindow, RequestedWidth, RequestedHeight ) )
    {
        sdleng_DestroyDevice();
        return FALSE;
    }
    x_DebugMsg( "SDLEngine: creating SDL GPU device\n" );

#if defined(CONFIG_DEBUG) || defined(X_DEBUG)
    const bool DebugMode = true;
#else
    const bool DebugMode = false;
#endif

    if( s_bExternalVulkanDevice )
    {
        SDL_PropertiesID Properties = SDL_CreateProperties();
        SDL_GPUVulkanOptions Options;
        x_memset( &Options, 0, sizeof( Options ) );
        Options.external_instance       = s_ExternalVulkanDevice.Instance;
        Options.external_physical_device = s_ExternalVulkanDevice.PhysicalDevice;
        Options.external_device         = s_ExternalVulkanDevice.Device;
        Options.external_queue          = s_ExternalVulkanDevice.Queue;
        Options.external_queue_family_index = s_ExternalVulkanDevice.QueueFamilyIndex;

        SDL_SetStringProperty( Properties,
                               SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING,
                               SDLENG_GPU_DRIVER_NAME );
        SDL_SetBooleanProperty( Properties,
                                SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN,
                                DebugMode );
        SDL_SetBooleanProperty( Properties,
                                SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN,
                                TRUE );
        SDL_SetPointerProperty( Properties,
                                SDL_PROP_GPU_DEVICE_CREATE_VULKAN_OPTIONS_POINTER,
                                &Options );
        g_pSDLGPUDevice = SDL_CreateGPUDeviceWithProperties( Properties );
        SDL_DestroyProperties( Properties );
    }
    else
    {
        g_pSDLGPUDevice = SDL_CreateGPUDevice( SDLENG_GPU_SHADER_FORMATS,
                                               DebugMode,
                                               SDLENG_GPU_DRIVER_NAME );
    }
    if( !g_pSDLGPUDevice )
    {
        sdleng_LogError( "SDLEngine", "SDL_CreateGPUDevice" );
        sdleng_DestroyDevice();
        return FALSE;
    }
    x_DebugMsg( "SDLEngine: SDL GPU device created\n" );

    x_DebugMsg( "SDLEngine: setting frames in flight\n" );
    if( !SDL_SetGPUAllowedFramesInFlight( g_pSDLGPUDevice, 1 ) )
    {
        sdleng_LogError( "SDLEngine", "SDL_SetGPUAllowedFramesInFlight(1)" );
        sdleng_DestroyDevice();
        return FALSE;
    }

    x_DebugMsg( "SDLEngine: initializing format policy\n" );
    if( !sdleng_InitializeFormatPolicy() )
    {
        sdleng_DestroyDevice();
        return FALSE;
    }

    if( s_bExternalVulkanDevice )
    {
        /* VR has no SDL presentation path. Avoid creating or claiming an
         * Android window swapchain on the OpenXR-owned Vulkan instance. */
        s.SwapchainFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        s.BackBufferWidth = s_ExternalVulkanDevice.RenderWidth
                          ? s_ExternalVulkanDevice.RenderWidth : 1024;
        s.BackBufferHeight = s_ExternalVulkanDevice.RenderHeight
                           ? s_ExternalVulkanDevice.RenderHeight : 1024;
    }
    else
    {
        x_DebugMsg( "SDLEngine: claiming window for GPU\n" );
        if( !SDL_ClaimWindowForGPUDevice( g_pSDLGPUDevice, g_pSDLWindow ) )
        {
            sdleng_LogError( "SDLEngine", "SDL_ClaimWindowForGPUDevice" );
            sdleng_DestroyDevice();
            return FALSE;
        }
        s.bWindowClaimed = TRUE;

        x_DebugMsg( "SDLEngine: configuring swapchain\n" );
        if( !sdleng_ConfigureSwapchain() )
        {
            sdleng_DestroyDevice();
            return FALSE;
        }
    }

    if( s_bExternalVulkanDevice )
    {
        const u32 NativeWidth = s_ExternalVulkanDevice.RenderWidth
                              ? s_ExternalVulkanDevice.RenderWidth : 1024;
        const u32 NativeHeight = s_ExternalVulkanDevice.RenderHeight
                               ? s_ExternalVulkanDevice.RenderHeight : 1024;
        SDL_GPUTextureCreateInfo CreateInfo;
        x_memset( &CreateInfo, 0, sizeof( CreateInfo ) );
        CreateInfo.type                 = SDL_GPU_TEXTURETYPE_2D;
        CreateInfo.format               = s.SwapchainFormat;
        CreateInfo.usage                = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        CreateInfo.width                = NativeWidth;
        CreateInfo.height               = NativeHeight;
        CreateInfo.layer_count_or_depth = 1;
        CreateInfo.num_levels           = 1;
        CreateInfo.sample_count         = SDL_GPU_SAMPLECOUNT_1;
        for( u32 Eye = 0; Eye < 2; ++Eye )
        {
            s.pNativeXRBackBuffers[Eye] = SDL_CreateGPUTexture( g_pSDLGPUDevice,
                                                                &CreateInfo );
            if( !s.pNativeXRBackBuffers[Eye] )
            {
                sdleng_LogError( "SDLEngine", "SDL_CreateGPUTexture(native XR eye backbuffer)" );
                sdleng_DestroyDevice();
                return FALSE;
            }
        }
        s.bNativeXRBackBuffer = TRUE;
        s.NativeXRWidth       = NativeWidth;
        s.NativeXRHeight      = NativeHeight;
        s.BackBufferWidth     = NativeWidth;
        s.BackBufferHeight    = NativeHeight;
        x_DebugMsg( "SDLEngine: native OpenXR backbuffer %ux%u\n",
                    NativeWidth, NativeHeight );
    }
    x_DebugMsg( "SDLEngine: swapchain configured\n" );

    s.bInitialized = TRUE;

    x_GetProfiler().SetProperty( "Renderer.Driver", SDL_GetGPUDeviceDriver( g_pSDLGPUDevice ) );
    x_DebugMsg( "SDLEngine: GPU device initialized using driver '%s' shader formats 0x%08X\n",
                SDL_GetGPUDeviceDriver( g_pSDLGPUDevice ),
                SDL_GetGPUShaderFormats( g_pSDLGPUDevice ) );

    return TRUE;
}

//==============================================================================

void sdleng_DestroyDevice( void )
{
    const xbool bDestroyWindow = s.bWindowInitializedByDevice;

    if( s.pRenderPass )
    {
        x_DebugMsg( "SDLEngine: closing render pass during device teardown\n" );
        sdleng_EndRenderPass();
    }
    sdleng_CancelFrame();
    sdleng_WaitForIdle();

    if( g_pSDLGPUDevice )
    {
        for( u32 Eye = 0; Eye < 2; ++Eye )
        {
            if( s.pNativeXRBackBuffers[Eye] )
            {
                SDL_ReleaseGPUTexture( g_pSDLGPUDevice,
                                       s.pNativeXRBackBuffers[Eye] );
                s.pNativeXRBackBuffers[Eye] = NULL;
            }
        }
        s.bNativeXRBackBuffer = FALSE;
        s.NativeXREye = 0;
        s.NativeXRWidth = 0;
        s.NativeXRHeight = 0;
    }

    if( s.bWindowClaimed && g_pSDLGPUDevice && g_pSDLWindow )
    {
        SDL_ReleaseWindowFromGPUDevice( g_pSDLGPUDevice, g_pSDLWindow );
        s.bWindowClaimed = FALSE;
    }

    if( g_pSDLGPUDevice )
    {
        sdleng_ResetFormatPolicy();
        SDL_DestroyGPUDevice( g_pSDLGPUDevice );
        g_pSDLGPUDevice = NULL;
    }

    if( bDestroyWindow )
        sdleng_WindowKill();

    g_pSDLWindow = sdleng_WindowGetSDLWindow();
    sdleng_ResetDeviceState();
}

//==============================================================================

xbool sdleng_WaitForIdle( void )
{
    if( !g_pSDLGPUDevice )
        return TRUE;

    if( !SDL_WaitForGPUIdle( g_pSDLGPUDevice ) )
    {
        sdleng_LogError( "SDLEngine", "SDL_WaitForGPUIdle" );
        return FALSE;
    }

    return TRUE;
}

//==============================================================================
//  FRAME AND PASS LIFETIME
//==============================================================================

xbool sdleng_AcquireCommandBuffer( void )
{
    if( !s.bInitialized || !g_pSDLGPUDevice )
        return FALSE;

    if( s.pCommandBuffer )
        return TRUE;

    {
        X_PROFILE_SCOPE_CATEGORY( "Renderer", "AcquireCommandBuffer" );
        s.pCommandBuffer = SDL_AcquireGPUCommandBuffer( g_pSDLGPUDevice );
    }
    if( !s.pCommandBuffer )
    {
        sdleng_LogError( "SDLEngine", "SDL_AcquireGPUCommandBuffer" );
        return FALSE;
    }

    if( s.bNativeXRBackBuffer )
    {
        x_DebugMsg( "A51XR SDL acquire-command cmd=%p native=%d\n",
                    s.pCommandBuffer, s.bNativeXRBackBuffer );
    }
    s.RenderStartTime = 0;
    return TRUE;
}

xbool sdleng_AcquireSwapchainTexture( void )
{
    if( !s.bInitialized || !g_pSDLGPUDevice ||
        ( !g_pSDLWindow && !s.bNativeXRBackBuffer ) )
        return FALSE;

    if( s.pRenderPass )
        return FALSE;

    if( s.bSwapchainAcquired )
        return TRUE;

    if( !s.pCommandBuffer )
    {
        x_DebugMsg( "SDLEngine: swapchain acquire requires an active command buffer\n" );
        return FALSE;
    }

    if( s.bNativeXRBackBuffer )
    {
        s.NativeXREye       = 0;
        s.pSwapchainTexture = s.pNativeXRBackBuffers[s.NativeXREye];
        /* SDL still owns an Android window in this mode, but that window's
         * client extent is not the extent of the native XR render target.
         * Keep the engine viewport and UI resolution tied to the texture we
         * actually render and copy into the OpenXR swapchain. */
        s.BackBufferWidth  = s.NativeXRWidth;
        s.BackBufferHeight = s.NativeXRHeight;
        s.AcquiredWidth    = s.NativeXRWidth;
        s.AcquiredHeight   = s.NativeXRHeight;
        s.FramePacingWaitMs = 0.0f;
        s.bSwapchainAcquired = TRUE;
        x_DebugMsg( "A51XR SDL acquire-target native eye=%u target=%p size=%ux%u\n",
                    s.NativeXREye, s.pSwapchainTexture,
                    s.BackBufferWidth, s.BackBufferHeight );
        return TRUE;
    }

    sdleng_UpdateBackBufferSizeFromWindow( s.BackBufferWidth  ? s.BackBufferWidth  : SDLENG_DEFAULT_BACKBUFFER_WIDTH,
                                           s.BackBufferHeight ? s.BackBufferHeight : SDLENG_DEFAULT_BACKBUFFER_HEIGHT );

    const xtick AcquireStart = x_GetTime();
    xbool bAcquired;
    {
        X_PROFILE_SCOPE_CATEGORY( "Frame", "Frame.WaitForSwapchain" );
        bAcquired = SDL_WaitAndAcquireGPUSwapchainTexture( s.pCommandBuffer,
                                                           g_pSDLWindow,
                                                           &s.pSwapchainTexture,
                                                           &s.AcquiredWidth,
                                                           &s.AcquiredHeight );
    }
    s.FramePacingWaitMs = x_TicksToMs( x_GetTime() - AcquireStart );

    if( !bAcquired )
    {
        sdleng_LogError( "SDLEngine", "SDL_WaitAndAcquireGPUSwapchainTexture" );
        return FALSE;
    }

    if( !s.pSwapchainTexture )
    {
        return TRUE;
    }

    s.bSwapchainAcquired = TRUE;

    if( s.AcquiredWidth && s.AcquiredHeight )
    {
        s.BackBufferWidth  = s.AcquiredWidth;
        s.BackBufferHeight = s.AcquiredHeight;
    }

    if( !s.bSwapchainAcquireLogged )
    {
        x_DebugMsg( "SDLEngine: swapchain acquire=%1.3fms size=%ux%u\n",
                    s.FramePacingWaitMs,
                    s.BackBufferWidth,
                    s.BackBufferHeight );
        s.bSwapchainAcquireLogged = TRUE;
    }

    return TRUE;
}

//==============================================================================

xbool sdleng_BeginRenderPass( const SDL_GPUColorTargetInfo*        pColorTargets,
                              u32                                  ColorTargetCount,
                              const SDL_GPUDepthStencilTargetInfo* pDepthStencilTarget )
{
    if( !s.pCommandBuffer )
    {
        x_DebugMsg( "SDLEngine: render pass requires an active frame lifecycle\n" );
        return FALSE;
    }

    if( s.pRenderPass )
        return FALSE;

    if( ColorTargetCount && !pColorTargets )
        return FALSE;

    if( !ColorTargetCount && !pDepthStencilTarget )
        return FALSE;

    static xprofile_counter RenderPassCountMetric =
        x_GetProfiler().RegisterCounter( "RenderPassCalls", "Renderer" );
    RenderPassCountMetric.Add();
    const xbool bFineTiming = x_GetProfiler().IsFineTimingEnabled();
    const xtick RenderStart = x_GetTime();
    const xtick BeginStart = bFineTiming ? RenderStart : 0;
    s.pRenderPass = SDL_BeginGPURenderPass( s.pCommandBuffer,
                                            pColorTargets,
                                            ColorTargetCount,
                                            pDepthStencilTarget );
    const xtick BeginEnd = bFineTiming ? x_GetTime() : 0;
    if( bFineTiming )
    {
        static xprofile_zone RenderPassMetric =
            x_GetProfiler().RegisterZone( "RenderPassAPI", "RendererAPI" );
        RenderPassMetric.Record( BeginEnd - BeginStart );
    }
    if( !s.pRenderPass )
    {
        sdleng_LogError( "SDLEngine", "SDL_BeginGPURenderPass" );
        return FALSE;
    }

    if( s.bNativeXRBackBuffer && ColorTargetCount &&
        pColorTargets[0].texture )
    {
        SDL_GPUVulkanFrameInfo FrameInfo;
        const bool bHaveFrameInfo =
            SDL_GetGPUVulkanFrameInfo( g_pSDLGPUDevice,
                                       s.pCommandBuffer,
                                       pColorTargets[0].texture,
                                       &FrameInfo );
        x_DebugMsg( "A51XR SDL begin-pass eye=%u pass=%p color=%p activeTarget=%p queried=%d source=%p cmd=%p\n",
                    s.NativeXREye, s.pRenderPass,
                    pColorTargets[0].texture, s.pSwapchainTexture,
                    bHaveFrameInfo,
                    bHaveFrameInfo ? FrameInfo.source_image : NULL,
                    bHaveFrameInfo ? FrameInfo.command_buffer : NULL );
    }

    if( s.RenderStartTime == 0 )
    {
        s.RenderStartTime = RenderStart;
    }

    for( u32 iTarget = 0; iTarget < ColorTargetCount; iTarget++ )
    {
        if( pColorTargets[iTarget].texture == s.pSwapchainTexture )
        {
            s.bBackBufferRendered = TRUE;
            break;
        }
    }

    if( s.bNativeXRBackBuffer )
    {
        x_DebugMsg( "A51XR SDL pass-start eye=%u color0=%p active=%p backbuffer=%d\n",
                    s.NativeXREye,
                    ColorTargetCount ? pColorTargets[0].texture : NULL,
                    s.pSwapchainTexture,
                    s.bBackBufferRendered );
    }

    sdleng_ResetPipelineBinding();
    sdleng_ResetBufferBindings();
    sdleng_ResetShaderBindings();
    sdleng_ResetGraphicsBindingDebug();
    return TRUE;
}

//==============================================================================

void sdleng_EndRenderPass( void )
{
    if( !s.pRenderPass )
        return;

    if( s.bNativeXRBackBuffer )
        x_DebugMsg( "A51XR SDL end-pass eye=%u pass=%p target=%p\n",
                    s.NativeXREye, s.pRenderPass, s.pSwapchainTexture );

    static xprofile_counter RenderPassCountMetric =
        x_GetProfiler().RegisterCounter( "RenderPassCalls", "Renderer" );
    RenderPassCountMetric.Add();
    const xbool bFineTiming = x_GetProfiler().IsFineTimingEnabled();
    const xtick EndStart = bFineTiming ? x_GetTime() : 0;
    SDL_EndGPURenderPass( s.pRenderPass );
    const xtick EndEnd = bFineTiming ? x_GetTime() : 0;
    if( bFineTiming )
    {
        static xprofile_zone RenderPassMetric =
            x_GetProfiler().RegisterZone( "RenderPassAPI", "RendererAPI" );
        RenderPassMetric.Record( EndEnd - EndStart );
    }
    s.pRenderPass = NULL;
    sdleng_ResetPipelineBinding();
    sdleng_ResetBufferBindings();
    sdleng_ResetShaderBindings();
    sdleng_ResetGraphicsBindingDebug();
}

//==============================================================================

xbool sdleng_EndFrame( void )
{
    if( s.bNativeXRBackBuffer )
    {
        x_DebugMsg( "A51XR SDL end-frame cmd=%p target=%p rendered=%d\n",
                    s.pCommandBuffer, s.pSwapchainTexture,
                    s.bBackBufferRendered );
    }
    return sdleng_SubmitCurrentCommandBuffer();
}

//==============================================================================

void sdleng_CancelFrame( void )
{
    if( !s.pCommandBuffer )
    {
        return;
    }

    if( s.pRenderPass )
    {
        x_DebugMsg( "SDLEngine: cannot cancel a command buffer with an active render pass\n" );
        ASSERT( FALSE );
        return;
    }

    SDL_GPUCommandBuffer* pCommandBuffer = s.pCommandBuffer;
    const xbool bSwapchainAcquired = s.bSwapchainAcquired;
    sdleng_ResetFrameState();

    if( bSwapchainAcquired )
    {
        if( !SDL_SubmitGPUCommandBuffer( pCommandBuffer ) )
            sdleng_LogError( "SDLEngine", "SDL_SubmitGPUCommandBuffer" );
    }
    else
    {
        if( !SDL_CancelGPUCommandBuffer( pCommandBuffer ) )
            sdleng_LogError( "SDLEngine", "SDL_CancelGPUCommandBuffer" );
    }

}

//==============================================================================

void sdleng_SetBackBufferViewport( void )
{
    if( !s.pRenderPass )
        return;

    SDL_GPUViewport Viewport;
    Viewport.x         = 0.0f;
    Viewport.y         = 0.0f;
    Viewport.w         = (f32)(s.AcquiredWidth  ? s.AcquiredWidth  : s.BackBufferWidth);
    Viewport.h         = (f32)(s.AcquiredHeight ? s.AcquiredHeight : s.BackBufferHeight);
    Viewport.min_depth = 0.0f;
    Viewport.max_depth = 1.0f;

    static xprofile_counter DynamicStateCountMetric =
        x_GetProfiler().RegisterCounter( "DynamicStateCalls", "Renderer" );
    DynamicStateCountMetric.Add();
    const xbool bFineTiming = x_GetProfiler().IsFineTimingEnabled();
    const xtick Start = bFineTiming ? x_GetTime() : 0;
    SDL_SetGPUViewport( s.pRenderPass, &Viewport );
    const xtick End = bFineTiming ? x_GetTime() : 0;
    if( bFineTiming )
    {
        static xprofile_zone DynamicStateMetric =
            x_GetProfiler().RegisterZone( "DynamicStateAPI", "RendererAPI" );
        DynamicStateMetric.Record( End - Start );
    }
}

//==============================================================================

xbool sdleng_InFrame( void )
{
    return s.pCommandBuffer != NULL;
}

//==============================================================================

xbool sdleng_InRenderPass( void )
{
    return s.pRenderPass != NULL;
}

//==============================================================================

xbool sdleng_WasBackBufferRendered( void )
{
    return s.bBackBufferRendered;
}

//==============================================================================
//  ACCESSORS
//==============================================================================

SDL_GPUDevice* sdleng_GetDevice( void )
{
    return g_pSDLGPUDevice;
}

//==============================================================================

SDL_Window* sdleng_GetWindow( void )
{
    return g_pSDLWindow;
}

//==============================================================================

SDL_GPUCommandBuffer* sdleng_GetCommandBuffer( void )
{
    return s.pCommandBuffer;
}

//==============================================================================

SDL_GPURenderPass* sdleng_GetRenderPass( void )
{
    return s.pRenderPass;
}

//==============================================================================

SDL_GPUTexture* sdleng_GetSwapchainTexture( void )
{
    return s.pSwapchainTexture;
}

//==============================================================================

SDL_GPUTextureFormat sdleng_GetSwapchainFormat( void )
{
    return s.SwapchainFormat;
}

//==============================================================================

f32 sdleng_GetFramePacingWaitMs( void )
{
    return s.FramePacingWaitMs;
}

//==============================================================================

f32 sdleng_GetRenderSubmitMs( void )
{
    return s.RenderSubmitMs;
}

//==============================================================================

void sdleng_GetBackBufferSize( s32& Width, s32& Height )
{
    if( !s.bSwapchainAcquired )
    {
        sdleng_UpdateBackBufferSizeFromWindow( s.BackBufferWidth  ? s.BackBufferWidth  : SDLENG_DEFAULT_BACKBUFFER_WIDTH,
                                               s.BackBufferHeight ? s.BackBufferHeight : SDLENG_DEFAULT_BACKBUFFER_HEIGHT );
    }

    Width  = (s32)s.BackBufferWidth;
    Height = (s32)s.BackBufferHeight;
}

//==============================================================================
#endif // (defined(TARGET_DESKTOP) || defined(TARGET_MOBILE)) && defined(ENTROPY_RENDER_SDL)
//==============================================================================
