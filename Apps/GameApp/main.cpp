//==============================================================================
//
//  main.cpp
//
//  Area 51 Main Program
//
//==============================================================================
//
//  Copyright (c) 2002-2003 Inevitable Entertainment Inc.  All rights reserved.
//
//==============================================================================

//==============================================================================
//  CORE INCLUDES
//==============================================================================

#include "Entropy.hpp"
#include "xCore/x_files/x_memory.hpp"
#include "Support/VR/A51Perf.hpp"

#if defined( TARGET_ANDROID )
#include <android/log.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_timer.h>
#endif

//==============================================================================
//  SYSTEM MANAGER INCLUDES
//==============================================================================

#include "ResourceMgr/ResourceMgr.hpp"
#include "Obj_mgr/obj_mgr.hpp"
#include "Render/Render.hpp"
#include "AudioMgr/AudioMgr.hpp"
#include "IOManager/io_mgr.hpp"
#include "NetworkMgr/NetworkMgr.hpp"
#include "NetworkMgr/GameMgr.hpp"
#include "NetworkMgr/MsgMgr.hpp"
#include "StateMgr/StateMgr.hpp"
#include "SaveData/SaveDataMgr.hpp"
#include "SaveData/Backend/SaveDataBackend.hpp"

//==============================================================================
//  OBJECT INCLUDES
//==============================================================================

#include "Objects/Player/Player.hpp"
#include "Objects/Corpse.hpp"
#include "Objects/LevelSettings.hpp"
#include "Objects/SpawnPoint.hpp"
#include "Objects/ParticleEmiter.hpp"
#include "Objects/AlienGlob.hpp"
#include "Objects/HudObject.hpp"
#include "Objects/Render/PostEffectMgr.hpp"

//==============================================================================
//  GAME SUBSYSTEM INCLUDES
//==============================================================================

#include "GameLib/Level.hpp"
#include "GameLib/BinLevel.hpp"
#include "GameLib/Link.hpp"
#include "GameLib/LevelLoader.hpp"
#include "GameLib/StatsMgr.hpp"
#include "GameLib/RenderContext.hpp"
#include "ZoneMgr/ZoneMgr.hpp"
#include "PlaySurfaceMgr/PlaySurfaceMgr.hpp"
#include "CollisionMgr/PolyCache.hpp"
#include "TemplateMgr/TemplateMgr.hpp"
#include "Decals/DecalMgr.hpp"
#include "TweakMgr/TweakMgr.hpp"
#include "PainMgr/PainMgr.hpp"
#include "PhysicsMgr/PhysicsMgr.hpp"
#include "Debris/debris_mgr.hpp"
#include "PerceptionMgr/PerceptionMgr.hpp"
#include "CheckPointMgr/CheckPointMgr.hpp"

//==============================================================================
//  AUDIO INCLUDES
//==============================================================================

#include "Audio/audio_stream_mgr.hpp"
#include "Audio/backend/audio_backend.hpp"
#include "Audio/audio_voice_mgr.hpp"
#include "Music_mgr/music_mgr.hpp"
#include "MusicStateMgr/MusicStateMgr.hpp"
#include "ConversationMgr/ConversationMgr.hpp"

//==============================================================================
//  SCRIPTING
//==============================================================================

#include "../Support/ScriptMgr/script_mgr.hpp"

//==============================================================================
//  SUPPORT SYSTEM INCLUDES
//==============================================================================

#include "../Support/TriggerEx/TriggerEx_Manager.hpp"
#include "../Support/Tracers/TracerMgr.hpp"
#include "../Support/Render/LightMgr.hpp"
#if defined( A51_ENABLE_OPENXR )
#include "../Support/Render/PC/GBufferMgr.hpp"
#include "../Support/Render/PC/GeomMgr/GeomMgr.hpp"
#include "../Support/Render/PC/PrimitiveMgr/PrimitiveMgr.hpp"
#include "../Support/Render/PC/DecalRenderer.hpp"
#endif
#include "Navigation/Nav_Map.hpp"
#include "Navigation/ng_connection2.hpp"
#include "Navigation/ng_node2.hpp"

//==============================================================================
//  UI AND TEXT INCLUDES
//==============================================================================

#include "UI/ui_manager.hpp"
#include "UI/ui_font.hpp"
#include "UI/ui_renderer.hpp"
#include "StringMgr/StringMgr.hpp"
#include "GameTextMgr/GameTextMgr.hpp"
#include "Menu/DebugMenu2.hpp"

//==============================================================================
//  UTILITY INCLUDES
//==============================================================================

#include "x_files/x_profile.hpp"
#include "x_files/x_workers.hpp"
#if X_WORKERS_DEBUG && X_THREADS_DEBUG && X_WORKERS_DEBUG_LOG
#include "x_files/x_threads.hpp"
#endif
#include "Auxiliary/Bitmap/aux_Bitmap.hpp"
#include "DataVault/DataVault.hpp"
#include "Config.hpp"
#include "FramePacer.hpp"
#include "FrameTiming.hpp"
#include "GameAppPlatform.hpp"
#include "Configuration/GameConfig.hpp"

#if defined( A51_ENABLE_OPENXR )
#include "VR/XRRuntime.hpp"
#include "VR/XRSession.hpp"
#include "SDLEngine/sdleng_vulkan.hpp"
#endif

#if CONFIG_IS_DEMO
xtimer g_DemoIdleTimer;
#endif

//==============================================================================
//  STRUCTURES
//==============================================================================

#ifndef WIN32
#if ENABLE_RENDER_STATS
extern render::stats s_RenderStats;
#endif
#endif

struct profile
{
    s32     Rate;
    s32     DoPrint;
};

//==============================================================================
//  GLOBAL VARIABLES - CORE SYSTEM
//==============================================================================

xbool       g_bMemReports           = FALSE;
xbool       g_game_running          = TRUE;
xbool       g_first_person          = TRUE;
s32         g_MemoryLowWater        = 0x7fffffff;
view        g_View;
u32         g_nLogicFramesAfterLoad = 0;
char        g_FullPath[256];
char        g_DataPath[256];

//==============================================================================
//  GLOBAL VARIABLES - CAMERA AND RENDERING
//==============================================================================

#if (!CONFIG_IS_DEMO)
xbool       g_FreeCam               = FALSE;
#endif
xbool       g_FreeCamPause          = TRUE;
xbool       g_MagentaColor          = FALSE;
xbool       g_RenderBoneBBoxes      = FALSE;
xbool       g_MirrorWeapon          = FALSE;

//==============================================================================
//  GLOBAL VARIABLES - GAME SETTINGS
//==============================================================================

s32         g_Difficulty            = 1; // Start out on Medium difficulty
const char* DifficultyText[]        = { "Easy", "Medium", "Hard" };
xbool       g_right_stick_swap_xy   = FALSE;
xbool       g_bBloodEnabled         = TRUE;
xbool       g_bRagdollsEnabled      = TRUE;

#if defined( A51_ENABLE_OPENXR )
a51::xr::Runtime g_XRRuntime;
a51::xr::VulkanSession g_XRSession;
xbool              g_XRGameFrameBridge = FALSE;
xbool              g_XRPreinitialized   = FALSE;
xbool              g_XRFramePrepared    = FALSE;
xbool              g_XRFrameFailed      = FALSE;
xbool              g_XRFrameStageRegistered = FALSE;
xbool              g_XRFrameStereoRendered = FALSE;
xbool              g_XRFrameShouldRender   = FALSE;
xbool              g_XRFrameMultiviewRendered = FALSE;
xbool              g_XRFrameDirectRenderFailed = FALSE;
s32                g_XRActiveEye       = -1;
u32                g_XRRenderWidth     = 0;
u32                g_XRRenderHeight    = 0;
u32                g_XRDebugFrame      = 0;
#endif

//==============================================================================
//  GLOBAL VARIABLES - CONTROLLER
//==============================================================================

#if !defined( X_RETAIL ) && !defined(ctetrick)
xbool       g_bControllerCheck      = FALSE;
#else
xbool       g_bControllerCheck      = TRUE;
#endif

//==============================================================================
//  GLOBAL VARIABLES - DEBUG/DEVELOPMENT
//==============================================================================

#if defined( ENABLE_DEBUG_MENU )
stats       g_Stats;
xbool       g_GameLogicDebug        = FALSE;
xbool       g_DevWantsToSave        = FALSE;
xbool       g_DevWantsToLoad        = FALSE;
#endif // defined( ENABLE_DEBUG_MENU )

#if !defined( CONFIG_RETAIL )
xbool       g_AimAssist_Render_Reticle      = FALSE;
xbool       g_AimAssist_Render_Bullet       = FALSE;
xbool       g_AimAssist_Render_Turn         = FALSE;
xbool       g_AimAssist_Render_Bullet_Angle = FALSE;
xbool       g_AimAssist_Render_Player_Pills = FALSE;
xbool       g_CmdLineAutoServer             = FALSE;
xbool       g_CmdLineAutoClient             = FALSE;
xbool       g_CmdLineRTFHandler             = FALSE;
s32         g_CmdLineLanguage               = -1;
f32         g_WorldTimeDilation             = 1.0f;
#endif // !defined( CONFIG_RETAIL )

#if !defined( X_RETAIL ) || defined( X_QA )
extern f32  g_TimeDilationFactor;
extern xbool g_RenderFrameRateInfo;
extern s32  g_ScreenShotSize;
extern xbool g_ScreenShotModeEnabled;
#endif

//==============================================================================
//  GLOBAL VARIABLES - AUDIO DEBUG
//==============================================================================

xbool       SHOW_STREAM_INFO        = FALSE;
xbool       SHOW_AUDIO_LEVELS       = FALSE;
xbool       SHOW_AUDIO_CHANNELS     = FALSE;

#if !defined(X_RETAIL) || defined(X_QA)
f32         g_PeakTime              = 0.0f;
f32         PEAK_HOLD_TIME          = 1.0f;
f32         g_ClipTime              = 0.0f;
f32         CLIP_HOLD_TIME          = 3.0f;
xbool       g_Clipped               = FALSE;
s32         g_Peak                  = 0;
s32         g_ChannelPeak           = 0;
f32         g_ChannelTime           = 0.0f;
f32         CHANNEL_PEAK_HOLD_TIME  = 5.0f;
f32         CHANNEL_PEAK_CLIP_TIME  = 8.0f;
s32         g_bChannelFlash         = 0;
char*       g_ChannelText[48];
f32         g_ChannelVolume[48];
s32         g_x                     = 5;
s32         g_y                     = 100;
s32         g_limit                 = 30;
#endif // X_RETAIL

#if !defined(X_RETAIL)
static xbool s_ForceGameComplete    = FALSE;
#endif

//  FUNCTION PROTOTYPES
//==============================================================================

void        LoadCamera              ( void );
void        SaveCamera              ( void );
void        Render                  ( void );

//==============================================================================
//  PLATFORM SPECIFIC INCLUDES
//==============================================================================

#include "InputMgr/GamePad.hpp"

#if defined( TARGET_DESKTOP )
#include "main_desktop.inl"
#elif defined( TARGET_MOBILE )
#include "main_mobile.inl"
#endif

//==============================================================================
//  INPUT HANDLING FUNCTIONS
//==============================================================================

//==============================================================================

static u32 GetGameInputContext( void )
{
#if defined( ENABLE_DEBUG_MENU )
    if( g_DebugMenu.IsActive() )
        return DEBUG_MENU_CONTEXT;
#endif

    if( g_StateMgr.IsPaused() || g_StateMgr.InSystemError() )
        return FRONTEND_CONTEXT;

    if( g_StateMgr.GetState() == SM_PLAYING_GAME )
        return INGAME_CONTEXT;

    return FRONTEND_CONTEXT;
}

//==============================================================================

static s32 WasPausePressed( void )
{
    return g_GameInput.GetPauseController();
}

//==============================================================================

xbool HandleInput( f32 DeltaTime )
{
    X_PROFILE_SCOPE_CATEGORY( "Context", "HandleInput" );

    // Check for exit message
    if( g_Input.GetFrameSnapshot().IsPressed( INPUT_MSG_EXIT ) )
        return( FALSE );

    #if defined( ENABLE_DEBUG_MENU )
    if( g_DebugMenu.IsActive() )
    {
        return( TRUE );
    }
    #endif // defined( ENABLE_DEBUG_MENU )

    static s32 FreeCamDebounce = 0;

    {
    #if defined( TARGET_DESKTOP )
        //if( g_Input.GetFrameSnapshot().WasPressed( INPUT_KBD_GRAVE ) )
        //{
        //    FreeCamDebounce++;
        //    if( FreeCamDebounce == 1 )
        //    {
        //        g_FreeCam ^= 1;
        //        if( !g_FreeCam )
        //        {
        //            // Move the player.
        //            player* pPlayer = SMP_UTIL_GetActivePlayer();
        //            if( pPlayer )
        //            {
        //                vector3 vPos = g_View.GetPosition();
        //                pPlayer->OnExitFreeCam( vPos );
        //            }
        //        }
        //    }
        //}
        //else
    #endif // defined(TARGET_DESKTOP)
        {
            FreeCamDebounce = 0;
        }

#if defined( ENABLE_DEBUG_MENU )
        if( g_DebugMenu.WasTogglePressed() &&
            g_StateMgr.IsPaused() != TRUE )
        {
            return( TRUE );
        }
    #endif // defined( ENABLE_DEBUG_MENU )

        if( !g_StateMgr.InSystemError() )
        {
            // check for pause
            s32 PausingController;

            PausingController = WasPausePressed();

            if( PausingController != -1 )
            {
                // Toggle the pause state.
                g_StateMgr.SetPaused( !g_StateMgr.IsPaused(), PausingController );
                g_GameInput.ClearInput();
            #if CONFIG_IS_DEMO
                g_DemoIdleTimer.Reset();
                g_DemoIdleTimer.Start();
            #endif
            }

            //  Check for pulled controllers.
            g_StateMgr.CheckControllers();

        }   // !InSystemError()
    }

    // Handle any platform specific input requirements
    if( HandleInputPlatform( DeltaTime ) == FALSE )
        return( FALSE );

    return( TRUE );
}

//==============================================================================
//  AUDIO MANAGEMENT FUNCTIONS
//==============================================================================

void UpdateAudio( f32 DeltaTime )
{
    STAT_LOGGER( temp, k_stats_Sound );

    X_PROFILE_SCOPE_CATEGORY( "Context", "UpdateAudio" );

    #ifdef AUDIO_ENABLE

    g_ConverseMgr.Update( DeltaTime );
    g_MusicStateMgr.Update();
    g_MusicMgr.Update( DeltaTime );
    g_AudioMgr.Update( DeltaTime );

    #endif // AUDIO_ENABLE
}

//==============================================================================

#if !defined(X_RETAIL) || defined(X_QA)

void AudioStats( f32 DeltaTime )
{
    (void)DeltaTime;
}

#endif // X_RETAIL

//==============================================================================
//  MAIN UPDATE FUNCTIONS
//==============================================================================

#if !defined(X_RETAIL) && X_WORKERS_DEBUG && X_THREADS_DEBUG && X_WORKERS_DEBUG_LOG
static void LogThreadDebugStats( f32 DeltaTime );
#endif

static xbool ShouldAdvanceWorld( void )
{
    return
    #if (!CONFIG_IS_DEMO)
        (!g_FreeCamPause || (g_FreeCam == FALSE)) &&
    #endif
    #if defined( ENABLE_DEBUG_MENU )
        (g_DebugMenu.IsActive() == FALSE) &&
        (!eng_ScreenShotActive()) &&
    #endif
        ((g_StateMgr.IsPaused() == FALSE) || (g_NetworkMgr.IsOnline() == TRUE));
}

//==============================================================================

#if !defined( X_RETAIL ) || defined( A51_ENABLE_HEAP_PROFILE )
#if defined( TARGET_ANDROID )
#define A51_PERF_LOG(...) __android_log_print( ANDROID_LOG_INFO, "A51Perf", __VA_ARGS__ )
#else
#define A51_PERF_LOG(...) x_DebugMsg( __VA_ARGS__ )
#endif
#endif

#if defined( A51_ENABLE_HEAP_PROFILE )
namespace a51::perf
{
struct cpu_stage_stats
{
    u64   TotalTicks;
    xtick MaxTicks;
    u32   Calls;
};

static cpu_stage_stats s_CpuStageStats[RENDER_STAGE_COUNT] = {};

void RecordCpuStage( cpu_stage Stage, xtick DurationTicks )
{
    if( (Stage < 0) || (Stage >= RENDER_STAGE_COUNT) )
        return;

    cpu_stage_stats& Stats = s_CpuStageStats[Stage];
    Stats.TotalTicks += static_cast<u64>( DurationTicks );
    Stats.MaxTicks = MAX( Stats.MaxTicks, DurationTicks );
    Stats.Calls++;
}
} // namespace a51::perf

static void A51PerfLogCpuStages( xbool bSimulation )
{
    static const char* s_SimulationNames[] = {
        "physics", "objects", "aliens", "scripts", "triggers", "network"
    };
    static const char* s_RenderNames[] = {
        "render_game", "objects", "prepare", "collect", "playsurfaces",
        "decals", "lights", "submit_geom", "special", "geom_build",
        "geom_upload", "geom_gbuffer", "geom_direct", "post_effects"
    };

    const s32 First = bSimulation ? a51::perf::SIM_PHYSICS : a51::perf::RENDER_GAME;
    const s32 Count = bSimulation ? a51::perf::SIM_STAGE_COUNT
                                  : (a51::perf::RENDER_STAGE_COUNT - a51::perf::RENDER_GAME);
    const char** pNames = bSimulation ? s_SimulationNames : s_RenderNames;
    char Details[1200] = {};
    size_t Used = 0;

    for( s32 i = 0; i < Count; ++i )
    {
        const a51::perf::cpu_stage Stage = static_cast<a51::perf::cpu_stage>( First + i );
        a51::perf::cpu_stage_stats& Stats = a51::perf::s_CpuStageStats[Stage];
        const f32 AverageMs = Stats.Calls
            ? x_TicksToMs( static_cast<xtick>( Stats.TotalTicks / Stats.Calls ) ) : 0.0f;
        const f32 MaximumMs = Stats.Calls ? x_TicksToMs( Stats.MaxTicks ) : 0.0f;
        const int Written = std::snprintf( Details + Used, sizeof( Details ) - Used,
                                           "%s%s=%.3f/%.3f/%u",
                                           i ? " " : "", pNames[i], AverageMs,
                                           MaximumMs, Stats.Calls );
        if( (Written > 0) && (static_cast<size_t>( Written ) < sizeof( Details ) - Used) )
            Used += static_cast<size_t>( Written );
        Stats = {};
    }

    A51_PERF_LOG( "cpu_%s %s", bSimulation ? "simulation" : "render", Details );
}
#endif

void AdvanceSimulation( f32 DeltaTime )
{
    X_PROFILE_SCOPE_CATEGORY( "Context", "AdvanceSimulation" );
#if !defined( X_RETAIL ) || defined( A51_ENABLE_HEAP_PROFILE )
    const xtick A51PerfSimulationStart = x_GetTime();
    x_mem_profile_counters A51PerfHeapStart = {};
    x_MemGetProfileCounters( A51PerfHeapStart );
#endif


    #if !defined(X_RETAIL)
    if( s_ForceGameComplete )
    {
        g_ActiveConfig.SetExitReason( GAME_EXIT_GAME_COMPLETE );
        s_ForceGameComplete = FALSE;
    }

    #if X_WORKERS_DEBUG && X_THREADS_DEBUG && X_WORKERS_DEBUG_LOG
    LogThreadDebugStats( DeltaTime );
    #endif
    #endif

    if( ShouldAdvanceWorld() )
    {
        g_nLogicFramesAfterLoad++;

        // Limit dynamic dead bodies before advancing physics so that it doesn't get overloaded
        // and or/run out of constraints
        corpse::LimitCount();

        {
            A51_PERF_SCOPE( SIM_PHYSICS, "Sim/Physics" );
            g_PhysicsMgr.Advance( DeltaTime );
        }
        slot_id const HudSlot = g_ObjMgr.GetFirst( object::TYPE_HUD_OBJECT );
        hud_object* pHud = (HudSlot != SLOT_NULL)
                         ? (hud_object*)g_ObjMgr.GetObjectBySlot( HudSlot )
                         : NULL;
        if( pHud )
        {
            pHud->BeginIconSnapshot();
        }
        {
            A51_PERF_SCOPE( SIM_OBJECTS, "Sim/Objects" );
            STAT_LOGGER( temp, k_stats_OnAdvance );
            g_ObjMgr.AdvanceSimulation( DeltaTime );
        }
        if( pHud )
        {
            pHud->CommitIconSnapshot();
        }
        {
            A51_PERF_SCOPE( SIM_ALIENS, "Sim/AlienGlob" );
            g_AlienGlobMgr.Advance( DeltaTime );
        }
        {
            A51_PERF_SCOPE( SIM_SCRIPTS, "Sim/Scripts" );
            g_ScriptMgr.Update( DeltaTime );
        }
    }

    if(
    #if defined( ENABLE_DEBUG_MENU )
        (g_DebugMenu.IsActive() == FALSE) &&
        (!eng_ScreenShotActive()) &&
    #endif // defined( ENABLE_DEBUG_MENU )
        ((g_StateMgr.IsPaused() == FALSE) || (g_NetworkMgr.IsOnline() == TRUE))
      )
    {
        {
            A51_PERF_SCOPE( SIM_TRIGGERS, "Sim/Triggers" );
            g_TriggerExMgr.OnUpdate( DeltaTime );
        }
    }

    {
        A51_PERF_SCOPE( SIM_NETWORK, "Sim/Network" );
        g_NetworkMgr.AdvanceSimulation( DeltaTime );
    }

#if !defined( X_RETAIL ) || defined( A51_ENABLE_HEAP_PROFILE )
    // Keep profiling logs bounded: one aggregate line per second instead of
    // logging from the simulation loop on every frame.
    static xtick s_A51PerfWindowStart = 0;
    static u32   s_A51PerfSamples    = 0;
    static f32   s_A51PerfTotalMs    = 0.0f;
    static f32   s_A51PerfMaxMs      = 0.0f;
    static u64   s_A51PerfMallocCalls  = 0;
    static u64   s_A51PerfReallocCalls = 0;
    static u64   s_A51PerfFreeCalls    = 0;
    const xtick A51PerfNow = x_GetTime();
    x_mem_profile_counters A51PerfHeapEnd = {};
    x_MemGetProfileCounters( A51PerfHeapEnd );
    s_A51PerfMallocCalls  += A51PerfHeapEnd.MallocCalls  - A51PerfHeapStart.MallocCalls;
    s_A51PerfReallocCalls += A51PerfHeapEnd.ReallocCalls - A51PerfHeapStart.ReallocCalls;
    s_A51PerfFreeCalls    += A51PerfHeapEnd.FreeCalls    - A51PerfHeapStart.FreeCalls;
    if( s_A51PerfWindowStart == 0 )
        s_A51PerfWindowStart = A51PerfNow;
    const f32 A51PerfSampleMs = x_TicksToMs( A51PerfNow - A51PerfSimulationStart );
    s_A51PerfSamples++;
    s_A51PerfTotalMs += A51PerfSampleMs;
    s_A51PerfMaxMs = MAX( s_A51PerfMaxMs, A51PerfSampleMs );
    const f32 A51PerfWindowMs = x_TicksToMs( A51PerfNow - s_A51PerfWindowStart );
    if( A51PerfWindowMs >= 1000.0f )
    {
        A51_PERF_LOG( "simulation samples=%u avg_ms=%.3f max_ms=%.3f heap_avg_per_sample=%.1f/%.1f/%.1f",
                    s_A51PerfSamples,
                    s_A51PerfSamples ? ( s_A51PerfTotalMs / s_A51PerfSamples ) : 0.0f,
                    s_A51PerfMaxMs,
                    s_A51PerfSamples ? (double)s_A51PerfMallocCalls / s_A51PerfSamples : 0.0,
                    s_A51PerfSamples ? (double)s_A51PerfReallocCalls / s_A51PerfSamples : 0.0,
                    s_A51PerfSamples ? (double)s_A51PerfFreeCalls / s_A51PerfSamples : 0.0 );
#if defined( A51_ENABLE_HEAP_PROFILE )
        A51PerfLogCpuStages( TRUE );
#endif
        s_A51PerfWindowStart = A51PerfNow;
        s_A51PerfSamples    = 0;
        s_A51PerfTotalMs    = 0.0f;
        s_A51PerfMaxMs      = 0.0f;
        s_A51PerfMallocCalls  = 0;
        s_A51PerfReallocCalls = 0;
        s_A51PerfFreeCalls    = 0;
    }
#endif
}

//==============================================================================

void UpdateFrameServices( f32 FrameDeltaTime )
{
    // Wall-clock and platform-facing services. These run once per presented
    // frame and must not integrate authoritative world motion.
    X_PROFILE_SCOPE_CATEGORY( "Context", "UpdateFrameServices" );

    #ifndef X_RETAIL
    g_PolyCache.Update();
    #endif

    #if defined( ENABLE_DEBUG_MENU )
    g_DebugMenu.Update( FrameDeltaTime );
    #endif

    g_StateMgr.Update( FrameDeltaTime );
    g_GameTextMgr.Update( FrameDeltaTime );
    UpdateAudio( FrameDeltaTime );

    //handle save/load
    if ( g_BinLevelMgr.WantsToSave() )
    {
        g_BinLevelMgr.SaveRuntimeDynamicData();
        g_VarMgr.SaveRuntimeData();
    }
    else if ( g_BinLevelMgr.WantsToLoad() )
    {
        g_BinLevelMgr.LoadRuntimeDynamicData();
        g_VarMgr.LoadRuntimeData();
    }
}

//==============================================================================

void UpdateRenderServices( f32 FrameDeltaTime )
{
    // Advance render-side services once per frame.
    X_PROFILE_SCOPE_CATEGORY( "Context", "UpdateRenderServices" );

    render::Update( FrameDeltaTime );
    g_TracerMgr.OnUpdate( FrameDeltaTime );
    g_LightMgr.OnUpdate( FrameDeltaTime );
    g_PostEffectMgr.OnUpdate( FrameDeltaTime );
    g_DecalMgr.OnUpdate( FrameDeltaTime );
}

//==============================================================================

static xbool ClearBackBuffer( void )
{
    rtarget_backbuffer_pass_desc PassDesc;
    PassDesc.bUseDepth = FALSE;
    if( !rtarget_BeginBackBufferPass( PassDesc ) )
    {
        x_DebugMsg( "GameApp: failed to begin clear-only backbuffer pass\n" );
        eng_ResetAfterException();
        return FALSE;
    }

    rtarget_EndPass();
    return TRUE;
}

#if defined( A51_ENABLE_OPENXR )
static void XRStage_BeginFrame( void );
static void XRStage_BeforePresent( void );
static void XRStage_AfterPresent( void );
static void XRInput_Capture( input_event_buffer& Events );

static xbool XRGetUIClipTransform( const a51::xr::eye_view& Eye,
                                   f32& Scale, f32& Center )
{
    const f32 Left = -x_tan( Eye.FovLeft );
    const f32 Right = -x_tan( Eye.FovRight );
    const f32 Width = Left - Right;
    if( x_abs( Width ) <= 0.0001f )
        return FALSE;

    const f32 HalfFOV = MAX( x_abs( Eye.FovLeft ),
                             x_abs( Eye.FovRight ) );
    Scale = (2.0f * x_tan( HalfFOV )) / Width;
    Center = (Left + Right) / Width;
    return x_isvalid( Scale ) && x_isvalid( Center );
}

static const eng_frame_stage s_XRFrameStage =
{
    XRStage_BeginFrame,
    XRStage_BeforePresent,
    3000,
    XRStage_AfterPresent
};

static void XRInput_Capture( input_event_buffer& Events )
{
    if( g_XRGameFrameBridge )
        g_XRSession.CaptureInput(
            Events, GetGameInputContext() == INGAME_CONTEXT );
}

static xbool InitializeOpenXRBeforeEngine( void )
{
    a51::xr::runtime_create_info XRInfo;
    if( !g_XRRuntime.Initialize( XRInfo ) )
    {
        x_DebugMsg( "OpenXR initialization failed: %s\n",
                    g_XRRuntime.GetLastError() );
        return FALSE;
    }

    a51::xr::system_info const& System = g_XRRuntime.GetSystemInfo();
    x_DebugMsg( "OpenXR system: %s (%ux%u)\n",
                System.SystemName,
                System.MaxSwapchainWidth,
                System.MaxSwapchainHeight );

    /* Android VR owns the Vulkan context from the beginning. SDL is only
     * used as the engine's command/render API on top of these handles. */
    if( !g_XRSession.Initialize( g_XRRuntime ) )
    {
        x_DebugMsg( "OpenXR Vulkan session initialization failed: %s\n",
                    g_XRSession.GetLastError() );
        g_XRRuntime.Shutdown();
        return FALSE;
    }

    a51::xr::vulkan_device_info XRDevice;
    if( !g_XRSession.GetVulkanDeviceInfo( XRDevice ) )
    {
        x_DebugMsg( "OpenXR Vulkan device handles are unavailable\n" );
        g_XRSession.Shutdown();
        g_XRRuntime.Shutdown();
        return FALSE;
    }

    sdleng_vulkan_device_info SDLDevice = {};
    SDLDevice.Instance         = XRDevice.Instance;
    SDLDevice.PhysicalDevice   = XRDevice.PhysicalDevice;
    SDLDevice.Device           = XRDevice.Device;
    SDLDevice.Queue            = XRDevice.Queue;
    SDLDevice.QueueFamilyIndex = XRDevice.QueueFamilyIndex;
    SDLDevice.MultiviewEnabled = XRDevice.MultiviewEnabled;
    SDLDevice.DirectRenderEnabled = g_XRSession.UsesMutableSrgbImages();
    SDLDevice.DirectRenderBgra = g_XRSession.UsesBgraSwapchain();
    SDLDevice.DirectRenderArray = g_XRSession.UsesArraySwapchain();
    g_XRSession.GetRecommendedRenderSize( SDLDevice.RenderWidth,
                                          SDLDevice.RenderHeight );
    if( !sdleng_SetVulkanExternalDevice( SDLDevice ) )
    {
        x_DebugMsg( "SDL could not adopt the OpenXR Vulkan device\n" );
        g_XRSession.Shutdown();
        g_XRRuntime.Shutdown();
        return FALSE;
    }

    g_XRGameFrameBridge = TRUE;
    g_XRPreinitialized = TRUE;
    return TRUE;
}

static void XRStage_BeginFrame( void )
{
    ++g_XRDebugFrame;
    g_XRFrameStereoRendered = FALSE;
    g_XRFrameMultiviewRendered = FALSE;
    g_XRFrameShouldRender = FALSE;
    g_XRFrameDirectRenderFailed = FALSE;
    g_GBufferMgr.SetTargetOverride( NULL, NULL );
        if( !g_XRGameFrameBridge )
    {
        g_GBufferMgr.SetVRResolveOutputEnabled( FALSE );
        return;
    }

    const a51::xr::session_state State = g_XRSession.GetState();
    if( (State != a51::xr::session_state::Ready) &&
        (State != a51::xr::session_state::Running) )
    {
        g_GBufferMgr.SetVRResolveOutputEnabled( FALSE );
        return;
    }

    const xbool bFrameBegun = g_XRSession.BeginFrame();
    if( !bFrameBegun )
    {
        x_DebugMsg( "OpenXR: failed to begin render frame: %s\n",
                    g_XRSession.GetLastError() );
    }
    g_XRFrameShouldRender = g_XRSession.ShouldRenderFrame();
    if( !g_XRFrameShouldRender )
        return;

    a51::xr::eye_view StereoEye[2];
    f32 Scale0 = 1.0f;
    f32 Center0 = 0.0f;
    f32 Scale1 = 1.0f;
    f32 Center1 = 0.0f;
    if( g_XRSession.GetEyeView( 0, StereoEye[0] ) &&
        g_XRSession.GetEyeView( 1, StereoEye[1] ) )
    {
        XRGetUIClipTransform( StereoEye[0], Scale0, Center0 );
        XRGetUIClipTransform( StereoEye[1], Scale1, Center1 );
    }
    g_UIRenderer.SetStereoClipTransforms( Scale0, Center0,
                                           Scale1, Center1 );
    g_UIRenderer.SetHudElementScale( 0.50f );

    /* Every visible OpenXR frame renders into an acquired swapchain image.
     * Eye zero is the direct target for the flat menu/quad layer; gameplay
     * switches between the two acquired eye images in RenderGameStereoXR. */
    void* XRImage = NULL;
    u32 Width = 0;
    u32 Height = 0;
    sdleng_vulkan_frame_info Frame = {};
    if( !g_XRSession.UsesMutableSrgbImages() ||
        !sdleng_HasVulkanExternalDevice() ||
        !g_XRSession.GetAcquiredImage( 0, XRImage ) ||
        !sdleng_SetVulkanEyeImage( 0, XRImage ) ||
        !sdleng_SetVulkanRenderEye( 0 ) ||
        !sdleng_GetVulkanFrameInfoForEye( 0, Frame ) )
    {
        g_XRFrameDirectRenderFailed = TRUE;
        x_DebugMsg( "OpenXR: direct swapchain target setup failed; skipping frame\n" );
        return;
    }

    g_GBufferMgr.SetVRResolveOutputEnabled( TRUE );
    /* Create the depth target in its final two-layer shape before the first
     * frame resize. This avoids creating a one-layer target and immediately
     * destroying it when the multiview scene pass starts. */
    if( g_XRSession.UsesArraySwapchain() )
        g_GBufferMgr.SetMultiviewEnabled( TRUE );
    if( !g_GBufferMgr.ResizeGBuffer( Frame.Width, Frame.Height ) )
    {
        g_XRFrameDirectRenderFailed = TRUE;
        x_DebugMsg( "OpenXR: direct depth target setup failed; skipping frame\n" );
        return;
    }
    Width = Frame.Width;
    Height = Frame.Height;
    g_XRRenderWidth = Width;
    g_XRRenderHeight = Height;
}

static void XRStage_BeforePresent( void )
{
    g_XRFramePrepared = FALSE;
    g_XRFrameFailed   = FALSE;

    const a51::xr::session_state State = g_XRSession.GetState();
    const xbool bXRStateReady =
        (State == a51::xr::session_state::Ready) ||
        (State == a51::xr::session_state::Running);


    if( !g_XRGameFrameBridge || !bXRStateReady )
        return;

    if( !g_XRFrameShouldRender || g_XRFrameDirectRenderFailed )
    {
        /* FinishFrame releases any acquired images and submits an empty frame.
         * Never fall back to rendering/copying the SDL window image. */
        g_XRFramePrepared = TRUE;
        return;
    }

    sdleng_vulkan_frame_info SDLFrame = {};
    if( g_XRFrameMultiviewRendered )
    {
        const rtarget* pSceneColor = g_GBufferMgr.GetGBufferTarget( GBufferTarget::FinalColor );
        if( !pSceneColor || (pSceneColor->Desc.LayerCount < 2) ||
            !sdleng_GetVulkanFrameInfoForEye( 0, SDLFrame ) )
        {
            g_XRFrameFailed = TRUE;
            return;
        }

        /* Resolve both scene layers in one multiview pass. The composite
         * pipeline must use the same view mask as this render pass. */
        rtarget_backbuffer_pass_desc ResolvePass;
        ResolvePass.ViewMask = 0x3;
        ResolvePass.bUseDepth = FALSE;
        ResolvePass.ColorLoadOp = RTARGET_LOAD_DONT_CARE;
        if( !rtarget_BeginBackBufferPass( ResolvePass ) )
        {
            g_XRFrameFailed = TRUE;
            return;
        }
        composite_BlitMultiview( *pSceneColor, COMPOSITE_BLEND_COPY,
                                 1.0f,
                                 RSTATE_SAMPLER_PRESET_POINT_CLAMP );
        rtarget_EndPass();
    }
    else if( !sdleng_GetVulkanFrameInfo( SDLFrame ) )
    {
        x_DebugMsg( "OpenXR: SDL Vulkan frame handles unavailable\n" );
        g_XRFrameFailed = TRUE;
        return;
    }

    a51::xr::vulkan_frame_info XRFrame;
    XRFrame.CommandBuffer = SDLFrame.CommandBuffer;
    XRFrame.SourceImage   = SDLFrame.SourceImage;
    XRFrame.Width         = SDLFrame.Width;
    XRFrame.Height        = SDLFrame.Height;

    /* Front-end screens and startup movies are authored as a flat 2D
     * surface. Present them as a world-space quad instead of stretching the
     * UI over the whole projection frustum. Gameplay keeps true stereo. */
    const xbool bUseQuadLayer = !g_XRFrameMultiviewRendered;
    const char* pXRRenderPath = bUseQuadLayer ? "quad"
                              : "multiview-array";
    const f32 QuadWidthMeters = 1.6f;
    const f32 QuadHeightMeters = QuadWidthMeters *
        (f32)XRFrame.Height / (f32)XRFrame.Width;
    const xbool bPrepared = bUseQuadLayer
                      ? g_XRSession.PrepareQuadFrame( XRFrame,
                            QuadWidthMeters, QuadHeightMeters, 1.8f )
                      : g_XRSession.PrepareMultiviewFrame( XRFrame );
    if( !bPrepared )
    {
        x_DebugMsg( "OpenXR game frame preparation failed: %s\n",
                    g_XRSession.GetLastError() );
        g_XRFrameFailed = TRUE;
        return;
    }

    g_XRFramePrepared = TRUE;
    }

static void XRStage_AfterPresent( void )
{
        if( !g_XRFramePrepared )
    {
        if( g_XRFrameFailed )
            g_XRSession.CancelFrame();
        g_XRFrameFailed = FALSE;
        g_XRFrameStereoRendered = FALSE;
        g_XRFrameMultiviewRendered = FALSE;
        return;
    }

    const xbool bFinished = g_XRSession.FinishFrame();
        if( !bFinished )
    {
        x_DebugMsg( "OpenXR frame submission failed: %s\n",
                    g_XRSession.GetLastError() );
        g_XRSession.Shutdown();
        g_XRGameFrameBridge = FALSE;
    }

    /* This callback is also used by the standalone movie-player loop,
     * which calls eng_EndFrame() instead of EndFrameWithXR(). */
    g_XRFramePrepared      = FALSE;
    g_XRFrameStereoRendered = FALSE;
    g_XRFrameMultiviewRendered = FALSE;
}

static xbool EndFrameWithXR( void )
{
    /* XRStage_BeforePresent runs from eng_EndFrame after the game, UI and
     * post-processing stages have recorded their work.  The XR copy is
     * therefore ordered after the final menu/loading-screen pixels. */
    g_XRFramePrepared = FALSE;
    g_XRFrameFailed   = FALSE;

    if( !eng_EndFrame() )
    {
        if( g_XRFramePrepared )
            g_XRSession.CancelFrame();
        return FALSE;
    }

    if( g_XRFramePrepared )
    {
        if( !g_XRSession.FinishFrame() )
        {
            x_DebugMsg( "OpenXR frame submission failed: %s\n",
                        g_XRSession.GetLastError() );
            g_XRSession.Shutdown();
            g_XRGameFrameBridge = FALSE;
        }
        g_XRFramePrepared = FALSE;
        g_XRFrameStereoRendered = FALSE;
        g_XRFrameMultiviewRendered = FALSE;
        return TRUE;
    }
    g_XRFrameStereoRendered = FALSE;
    g_XRFrameMultiviewRendered = FALSE;
    return TRUE;
}
#else
static xbool EndFrameWithXR( void )
{
    return eng_EndFrame();
}
#endif

//==============================================================================

static xbool FinishClearOnlyFrame( void )
{
    if( !ClearBackBuffer() )
    {
        return FALSE;
    }

    return EndFrameWithXR();
}

//==============================================================================

static xbool SampleFrameTiming( FrameTiming& FrameClock,
                                f32&         FrameDeltaSeconds,
                                const char*  pFrameName,
                                xbool&       bFrameAccepted )
{
    FrameTimingSample const Sample = FrameClock.Sample();
    bFrameAccepted = (Sample.Status == FrameTimingStatus::Valid);

    if( bFrameAccepted )
    {
        FrameDeltaSeconds = Sample.AcceptedDeltaSeconds;
        return TRUE;
    }

    FrameDeltaSeconds = 0.0f;
    if( Sample.Status == FrameTimingStatus::Hitch )
    {
        x_DebugMsg( "GameApp: %s hitch delta %f; discarding frame\n", pFrameName, Sample.RawDeltaSeconds );
    }
    else
    {
        x_DebugMsg( "GameApp: invalid %s delta %f; discarding frame\n", pFrameName, Sample.RawDeltaSeconds );
    }

    return FinishClearOnlyFrame();
}

//==============================================================================

static xbool BeginTimedFrame( FramePacer& Pacer,
                              FrameTiming& FrameClock,
                              f32&        FrameDeltaSeconds )
{
    while( TRUE )
    {
        global_settings const& Settings = g_StateMgr.GetActiveSettings();
        Pacer.Configure( Settings.GetFrameRateLimit(), eng_GetPresentMode() );
        Pacer.WaitForNextFrame();

        if( !eng_BeginFrame() )
        {
            return FALSE;
        }

        xbool bFrameAccepted = FALSE;
        if( !SampleFrameTiming( FrameClock, FrameDeltaSeconds, "frame", bFrameAccepted ) )
        {
            return FALSE;
        }

        if( bFrameAccepted )
        {
            return TRUE;
        }
    }
}

//==============================================================================

static xbool BeginLoadingFrame( FrameTiming& FrameClock,
                                f32&    FrameDeltaSeconds )
{
    while( TRUE )
    {
        if( !eng_BeginFrame() )
        {
            return FALSE;
        }

        xbool bFrameAccepted = FALSE;
        if( !SampleFrameTiming( FrameClock, FrameDeltaSeconds, "loading frame", bFrameAccepted ) )
        {
            return FALSE;
        }

        if( bFrameAccepted )
        {
            return TRUE;
        }
    }
}

//==============================================================================

xbool UpdateFrontEnd( FramePacer& Pacer, FrameTiming& FrontEndClock )
{
    f32 DeltaTime = 0.0f;
    if( !BeginTimedFrame( Pacer, FrontEndClock, DeltaTime ) )
    {
        return FALSE;
    }

    {
        X_PROFILE_SCOPE_CATEGORY( "Frame", "Frame.Update" );

        g_FrontendInput.Update( DeltaTime );

        g_StateMgr.CheckControllers();

        #ifdef TARGET_DESKTOP
        if( g_Input.GetFrameSnapshot().IsPressed( INPUT_MSG_EXIT ) )
        {
            FinishClearOnlyFrame();
            return FALSE;
        }
        #endif
        g_NetworkMgr.UpdateFrame( DeltaTime );
        g_StateMgr.Update( DeltaTime );
        g_StateMgr.Render();

        UpdateAudio( DeltaTime );
    }

    if( !ClearBackBuffer() )
    {
        return FALSE;
    }

    return EndFrameWithXR();
}

//==============================================================================

xbool UpdateLevelLoadingFrame( FrameTiming& FrontEndClock )
{
    static const f32 k_LevelLoadTimeBudgetSeconds = 0.008f;

    f32 DeltaTime = 0.0f;
    if( !BeginLoadingFrame( FrontEndClock, DeltaTime ) )
    {
        return FALSE;
    }

    {
        X_PROFILE_SCOPE_CATEGORY( "Frame", "Frame.Update" );

        g_LevelLoader.UpdateLevelLoad( k_LevelLoadTimeBudgetSeconds );
        g_FrontendInput.Update( DeltaTime );
        g_NetworkMgr.UpdateFrame( DeltaTime );
        g_StateMgr.UpdateLevelLoading( DeltaTime );
        g_StateMgr.Render();
        UpdateAudio( DeltaTime );
    }

    if( !ClearBackBuffer() )
    {
        return FALSE;
    }

    return EndFrameWithXR();
}

//==============================================================================
//  VIEW AND RENDERING FUNCTIONS
//==============================================================================

void SetupViewAndFog( zone_mgr::zone_id StartZone )
{
    X_PROFILE_SCOPE_CATEGORY( "Context", "SetupView" );

    texture::handle FogPalette;

    // get the default fog palette and z buffer settings
    xbool   QuickFog = FALSE;
    slot_id SlotID = g_ObjMgr.GetFirst( object::TYPE_LEVEL_SETTINGS );
    if ( SlotID != SLOT_NULL )
    {
        object* pObject = g_ObjMgr.GetObjectBySlot( SlotID );
        ASSERT( pObject );
        level_settings& Settings = level_settings::GetSafeType( *pObject );
        g_View.SetZLimits( 10.0f, Settings.GetFarPlane() );

        FogPalette = Settings.GetFogPalette();
    }
    else
    {
        g_View.SetZLimits( 10.0f, 8000.0f );
    }

    // if the zone we're in has a different fog from the default, use
    // that one instead
    const char* pFog = g_ZoneMgr.GetZoneFog(StartZone,QuickFog);
    if ( *pFog != '\0' )
    {
        FogPalette.SetName( pFog );
    }

    // set the pixel scale (aspect ratio)
    #ifndef X_RETAIL
    if ( eng_ScreenShotActive() )
    {
        g_View.SetPixelScale( 1.0f );
    }
    else
    #endif // X_RETAIL
    {
        g_View.SetPixelScale();
    }

    // Set the viewport
    eng_SetView( g_View );

    // Set the fog
    render::SetCustomFogPalette( FogPalette, QuickFog, g_RenderContext.LocalPlayerIndex );
}

//==============================================================================

#if !defined(X_RETAIL) && X_WORKERS_DEBUG && X_THREADS_DEBUG && X_WORKERS_DEBUG_LOG

static xbool s_LogThreadDebugStats = TRUE;

static void LogThreadDebugStats( f32 DeltaTime )
{
    static f32 s_LogTimer = 0.0f;

    if( !s_LogThreadDebugStats )
        return;

    s_LogTimer += DeltaTime;
    if( s_LogTimer < 1.0f )
        return;

    s_LogTimer = 0.0f;

    x_thread_debug_snapshot ThreadSnapshot;
    x_worker_debug_snapshot WorkerSnapshot;
    s32                     i;

    x_GetThreadDebugSnapshot( ThreadSnapshot );
    x_WorkersGetDebugSnapshot( WorkerSnapshot );

    x_DebugMsg( "x_workers: %s n:%d kill:%d jobs F/P/R/D:%d/%d/%d/%d sub/done:%d/%d\n",
                WorkerSnapshot.IsInitialized ? "ON" : "OFF",
                WorkerSnapshot.nWorkers,
                WorkerSnapshot.IsKilling,
                WorkerSnapshot.nJobsFree,
                WorkerSnapshot.nJobsPending,
                WorkerSnapshot.nJobsRunning,
                WorkerSnapshot.nJobsDone,
                WorkerSnapshot.nJobsSubmitted,
                WorkerSnapshot.nJobsCompleted );

    x_DebugMsg( "x_worker_services: F/R/D:%d/%d/%d start/done:%d/%d\n",
                WorkerSnapshot.nServicesFree,
                WorkerSnapshot.nServicesRunning,
                WorkerSnapshot.nServicesDone,
                WorkerSnapshot.nServicesStarted,
                WorkerSnapshot.nServicesCompleted );

    if( !ThreadSnapshot.IsInitialized )
    {
        x_DebugMsg( "x_threads: OFF\n" );
        return;
    }

    x_DebugMsg( "x_threads: n:%d active:%d\n", ThreadSnapshot.nThreads, ThreadSnapshot.ActiveThreadId );

    for( i=0; i<ThreadSnapshot.nThreads; i++ )
    {
        const x_thread_debug_info& Info    = ThreadSnapshot.Threads[i];
        const char*                pActive = (Info.ThreadId == ThreadSnapshot.ActiveThreadId) ? "*" : " ";

        x_DebugMsg( "  %s id:%02d sys:%05d pri:%2d %s %s%s\n",
                    pActive,
                    Info.ThreadId,
                    Info.SystemId,
                    Info.Priority,
                    x_GetThreadStateName( Info.Status ),
                    Info.pName,
                    Info.NeedToTerminate ? " TERM" : "" );
    }
}

#endif // !defined(X_RETAIL) && X_WORKERS_DEBUG && X_THREADS_DEBUG && X_WORKERS_DEBUG_LOG

//==============================================================================

#if defined( A51_ENABLE_OPENXR )
static void RenderGameStereoXR( void );

static xbool XRBuildEyeView( const view& BaseView, u32 Eye, s32 Width,
                             s32 Height, view& EyeView )
{
    a51::xr::eye_view XREye;
    if( !g_XRSession.GetEyeView( Eye, XREye ) )
        return FALSE;

    EyeView = BaseView;
    EyeView.SetViewport( 0, 0, Width, Height );

    matrix4 EyeLocal;
    EyeLocal.Identity();
    const vector3 EyeOffset( XREye.Position[0] * 100.0f,
                             XREye.Position[1] * 100.0f,
                            -XREye.Position[2] * 100.0f );
    /* EyeOffset is in head-local coordinates. The base view already carries
     * the HMD orientation, so V2W composition rotates this translation once. */
    EyeLocal.SetTranslation( EyeOffset );
    EyeView.SetV2W( EyeView.GetV2W() * EyeLocal );

    const vector3 CameraPosition = EyeView.GetPosition();

    const f32 XFOV = 2.0f * MAX( x_abs( XREye.FovLeft ), x_abs( XREye.FovRight ) );
    const f32 YFOV = 2.0f * MAX( x_abs( XREye.FovUp ), x_abs( XREye.FovDown ) );
    if( YFOV > 0.01f ) EyeView.SetYFOV( YFOV );
    if( XFOV > 0.01f ) EyeView.SetXFOV( XFOV );
    EyeView.SetAsymmetricFOV( XREye.FovLeft, XREye.FovRight,
                              XREye.FovUp, XREye.FovDown );
    return TRUE;
}
#endif

void RenderGame( void )
{
    s32 i;

    A51_PERF_SCOPE( RENDER_GAME, "Render/Game" );
    X_PROFILE_SCOPE_CATEGORY( "Context", "Render" );
    LOG_STAT( k_stats_OtherRender );

    // Set background clear color
    #if (!CONFIG_IS_DEMO)
    if(g_MagentaColor)
    {

        static int count = 0;
        count++;
        if( count%2 )
            eng_SetBackColor( XCOLOR_BLACK );
        else
            eng_SetBackColor( xcolor(255,0,255) );
    }
    else
    #endif
    {
        eng_SetBackColor( XCOLOR_BLACK );
    }

    // Make sure we have full access to the framebuffer so that the splitscreen
    // cleansing rects can render properly
    {
        view TempView;
        {
            eng_MaximizeViewport( TempView );
            eng_SetViewport     ( TempView );
        }
    }

    // Get pointers to each of the players, we'll need them for setting up the
    // viewports.

    player* pPlayers[MAX_LOCAL_PLAYERS] = { 0 };
    slot_id ID                          = g_ObjMgr.GetFirst( object::TYPE_PLAYER );
    s32     nPlayers                    = 0;

    while( ID != SLOT_NULL )
    {
        ASSERT( nPlayers < g_ActiveConfig.GetPlayerCount() );

        object* pObj    = g_ObjMgr.GetObjectBySlot(ID);
        player* pPlayer = &player::GetSafeType( *pObj );

        if( pPlayer && (pPlayer->GetLocalSlot() != -1) )
        {
            pPlayers[ pPlayer->GetLocalSlot() ] = pPlayer;
            nPlayers++;
        }

        ID = g_ObjMgr.GetNext(ID);
    }

    // If we don't have all of the local players yet, then we don't want to try
    // to render.  So, just return.
    if( nPlayers != g_NetworkMgr.GetLocalPlayerCount() )
        return;

    s32 XRes, YRes;
    eng_GetRes( XRes, YRes );
    switch( nPlayers )
    {
        case 0:
        default:
            break;

        case 1:
        if ( MAX_LOCAL_PLAYERS >= nPlayers )
        {
            // one view, set it to the entire screen
            view& rView0 = player::GetLiveView( 0 );
            rView0.SetViewport( 0, 0, XRes, YRes );
        }
        break;

        case 2:
        if ( MAX_LOCAL_PLAYERS >= nPlayers )
        {
            // two views, set them to a horizontal split
            view& rView0 = player::GetLiveView( 0 );
            view& rView1 = player::GetLiveView( 1 );
            rView0.SetViewport( 0,0       ,XRes,YRes/2-1 ); // top
            rView1.SetViewport( 0,YRes/2+1,XRes,YRes     ); // bottom

            if( !g_StateMgr.IsPaused() && eng_Begin( "Kill last rect" ) )
            {
                g_UIRenderer.DrawRect( irect( 0,YRes/2-2,XRes,YRes/2+2 ), XCOLOR_BLACK );
                eng_End( );
            }
        }
        break;

        case 3:
        if ( MAX_LOCAL_PLAYERS >= nPlayers )
        {
            // four views, set them to a 4-way split
            view& rView0 = player::GetLiveView( 0 );
            view& rView1 = player::GetLiveView( 1 );
            view& rView2 = player::GetLiveView( 2 );
            rView0.SetViewport( 0       ,0       ,XRes/2-1,YRes/2-1 );   // upper-left
            rView1.SetViewport( XRes/2+1,0       ,XRes    ,YRes/2-1 );   // upper-right
            rView2.SetViewport( 0       ,YRes/2+1,XRes    ,YRes     );   // bottom


            if( !g_StateMgr.IsPaused() && eng_Begin( "Kill last rect" ) )
            {
                g_UIRenderer.DrawRect( irect( 0,        YRes/2-2, XRes,     YRes/2+2 ), XCOLOR_BLACK ); // horizontal line
                g_UIRenderer.DrawRect( irect( XRes/2-2, 0,        XRes/2+2, YRes/2+2 ), XCOLOR_BLACK ); // vertical line
                eng_End( );
            }
        }
        break;

        case 4:
        if ( MAX_LOCAL_PLAYERS >= nPlayers )
        {
            // four views, set them to a 4-way split
            view& rView0 = player::GetLiveView( 0 );
            view& rView1 = player::GetLiveView( 1 );
            view& rView2 = player::GetLiveView( 2 );
            view& rView3 = player::GetLiveView( 3 );

            rView0.SetViewport( 0       ,0,       XRes/2-1,YRes/2-1 );   // upper-left
            rView1.SetViewport( XRes/2+1,0,       XRes    ,YRes/2-1 );   // upper-right
            rView2.SetViewport( 0       ,YRes/2+1,XRes/2-1,YRes     );   // lower-left
            rView3.SetViewport( XRes/2+1,YRes/2+1,XRes    ,YRes     );   // lower-right


            if( !g_StateMgr.IsPaused() && eng_Begin( "Kill last rect" ) )
            {
                g_UIRenderer.DrawRect( irect( 0,        YRes/2-2, XRes,     YRes/2+2 ), XCOLOR_BLACK ); // horizontal line
                g_UIRenderer.DrawRect( irect( XRes/2-2, 0,        XRes/2+2, YRes     ), XCOLOR_BLACK ); // vertical line
                eng_End( );
            }
        }
        break;
    }

    // Make all the players inactive in anticipation of the render...
    for( i = 0; i < nPlayers; i++ )
        pPlayers[i]->SetAsActivePlayer( FALSE );

    for( i = 0; i < nPlayers; i++ )
    {
        g_RenderContext.Set( i,                                 // Local slot.
                             pPlayers[i]->net_GetSlot(),        // Net slot.
                             pPlayers[i]->net_GetTeamBits(),
                             pPlayers[i]->IsMutantVisionOn(),
                             FALSE );

        // set this player as the active one
        pPlayers[i]->SetAsActivePlayer( TRUE );

    #if (!CONFIG_IS_DEMO)
        if ( g_FreeCam == FALSE )
    #endif
        {
            g_View = pPlayers[i]->GetRenderView();

#if defined( A51_ENABLE_OPENXR )
            if( g_XRActiveEye >= 0 )
            {
                const s32 XRViewportWidth =
                    g_XRRenderWidth ? (s32)g_XRRenderWidth : XRes;
                const s32 XRViewportHeight =
                    g_XRRenderHeight ? (s32)g_XRRenderHeight : YRes;
                /* HUD layout reads the player's live view, not the local
                 * g_View copy used by the scene render. */
                player::GetLiveView( i ).SetViewport(
                    0, 0, XRViewportWidth, XRViewportHeight );
            }

            /* PlayerView already contains the tracked HMD orientation for
             * gameplay and HUD. Compose only the per-eye offset here;
             * applying the full pose again would make rendered aim diverge. */
            if( (g_XRActiveEye >= 0) && (g_XRActiveEye < 2) )
            {
                a51::xr::eye_view XREye;
                if( g_XRSession.GetEyeView( (u32)g_XRActiveEye, XREye ) )
                {
                    /* eng_GetRes() is the Android surface size (4128x2208
                     * on Quest 3), not the native per-eye OpenXR target
                     * (1680x1760).  The projection and viewport must use the
                     * same extent as the SDL/Vulkan texture that is copied
                     * into the XR swapchain. */
                    const s32 XRViewportWidth =
                        g_XRRenderWidth ? (s32)g_XRRenderWidth : XRes;
                    const s32 XRViewportHeight =
                        g_XRRenderHeight ? (s32)g_XRRenderHeight : YRes;
                    g_View.SetViewport( 0, 0,
                                        XRViewportWidth, XRViewportHeight );

                    matrix4 EyeLocal;
                    EyeLocal.Identity();
                    /* Area 51 world coordinates use centimetres while
                     * OpenXR poses are metres.  The conversion is applied
                     * only to the relative eye translation; room-scale
                     * translation remains relative to the captured origin. */
                    const vector3 EyeOffset = vector3(
                        XREye.Position[0] * 100.0f,
                        XREye.Position[1] * 100.0f,
                       -XREye.Position[2] * 100.0f );
                    /* Keep IPD in head-local coordinates. g_View already
                     * includes head rotation, so composition applies it once. */
                    EyeLocal.SetTranslation( EyeOffset );

                    /* Camera-to-world composition is base * local. Reversing
                     * this order rotates the whole map around world origin
                     * whenever the headset turns. */
                    const matrix4 EyeCamera = g_View.GetV2W() * EyeLocal;
                    g_View.SetV2W( EyeCamera );
                    const vector3 XRResultCameraPosition = g_View.GetPosition();

                    /* Keep culling bounds wide enough for the full eye and
                     * then use the exact asymmetric projection for this eye.
                     * SetAsymmetricFOV must be last because SetXFOV/SetYFOV
                     * intentionally switch the legacy symmetric mode off. */
                    const f32 XFOV = 2.0f * MAX( x_abs( XREye.FovLeft ),
                                                 x_abs( XREye.FovRight ) );
                    const f32 YFOV = 2.0f * MAX( x_abs( XREye.FovUp ),
                                                 x_abs( XREye.FovDown ) );
                    if( YFOV > 0.01f )
                        g_View.SetYFOV( YFOV );
                    if( XFOV > 0.01f )
                        g_View.SetXFOV( XFOV );
                    g_View.SetAsymmetricFOV( XREye.FovLeft,
                                             XREye.FovRight,
                                             XREye.FovUp,
                                             XREye.FovDown );
                }

            }
#endif
        }

#if defined( A51_ENABLE_OPENXR )
        if( g_XRActiveEye == 2 )
        {
            const s32 XRWidth = g_XRRenderWidth ? (s32)g_XRRenderWidth : XRes;
            const s32 XRHeight = g_XRRenderHeight ? (s32)g_XRRenderHeight : YRes;
            /* Keep the unmodified game camera.  The center camera below is
             * used for legacy culling, while the per-eye multiview matrices
             * must compose the headset pose exactly once. */
            const view BodyView( g_View );
            view CenterView( BodyView );
            if( XRBuildEyeView( BodyView, 0, XRWidth, XRHeight, CenterView ) )
            {
                CenterView.SetPosition( BodyView.GetPosition() );
                f32 HalfXFOV = 0.0f;
                f32 HalfYFOV = 0.0f;
                for( u32 Eye = 0; Eye < 2; ++Eye )
                {
                    a51::xr::eye_view XREye;
                    if( g_XRSession.GetEyeView( Eye, XREye ) )
                    {
                        HalfXFOV = MAX( HalfXFOV,
                                        MAX( x_abs( XREye.FovLeft ), x_abs( XREye.FovRight ) ) );
                        HalfYFOV = MAX( HalfYFOV,
                                        MAX( x_abs( XREye.FovUp ), x_abs( XREye.FovDown ) ) );
                    }
                }
                if( HalfXFOV > 0.01f ) CenterView.SetXFOV( HalfXFOV * 2.0f );
                if( HalfYFOV > 0.01f ) CenterView.SetYFOV( HalfYFOV * 2.0f );
                g_View = CenterView;
            }
            g_View.SetViewport( 0, 0, XRWidth, XRHeight );
        }
#endif

        zone_mgr::zone_id const PlayerViewZone =
            pPlayers[i]->GetPlayerViewZone();
        SetupViewAndFog( PlayerViewZone );

#if defined( A51_ENABLE_OPENXR )
        if( g_XRActiveEye == 2 )
        {
            const s32 XRWidth = g_XRRenderWidth ? (s32)g_XRRenderWidth : XRes;
            const s32 XRHeight = g_XRRenderHeight ? (s32)g_XRRenderHeight : YRes;
            /* g_View was changed to the center XR camera above for culling.
             * PlayerView already contains HMD orientation; add only IPD
             * separation to each shader view. */
            const view BaseView( pPlayers[i]->GetRenderView() );
            matrix4 StereoView[2];
            matrix4 StereoProjection[2];
            vector4 StereoCameraPosition[2];
            xbool bViewsReady = TRUE;
            for( u32 Eye = 0; Eye < 2; ++Eye )
            {
                view EyeView( BaseView );
                if( !XRBuildEyeView( BaseView, Eye, XRWidth, XRHeight, EyeView ) )
                {
                    bViewsReady = FALSE;
                    break;
                }
                StereoView[Eye] = EyeView.GetW2V();
                StereoProjection[Eye] = EyeView.GetV2C();
                const vector3 Position = EyeView.GetPosition();
                StereoCameraPosition[Eye] = vector4( Position[0], Position[1],
                                                     Position[2], 1.0f );
            }
            if( bViewsReady )
            {
                g_GeomMgr.SetMultiviewFrameData( StereoView, StereoProjection,
                                                 StereoCameraPosition );
                const rtarget* pFinalColor =
                    g_GBufferMgr.GetGBufferTarget( GBufferTarget::FinalColor );
                            }
        }
#endif

        // Perform any platform specific render initialization
        InitRenderPlatform();

        // render all objects
        xbool DoPortalWalk = TRUE;
        {
            A51_PERF_SCOPE( RENDER_OBJECTS, "Render/ObjectManager" );
            g_ObjMgr.Render( DoPortalWalk, g_View, PlayerViewZone );
        }

        EndRenderPlatform();
        pPlayers[i]->SetAsActivePlayer( FALSE );
    }

    // Make all the players active again so their input will function.
    for( i = 0; i < nPlayers; i++ )
        pPlayers[i]->SetAsActivePlayer( TRUE );

    #if defined( ENABLE_DEBUG_MENU )
    // Debug menu rendering
    g_DebugMenu.Render();
    #endif // defined( ENABLE_DEBUG_MENU )
}

#if defined( A51_ENABLE_OPENXR )
#if !defined( X_RETAIL ) || defined( A51_ENABLE_HEAP_PROFILE )
static void A51PerfRecordRenderSample( const char* pMode, f32 RenderMs,
                                       const x_mem_profile_counters& HeapStart,
                                       const x_mem_profile_counters& HeapEnd )
{
    static xtick s_A51PerfWindowStart = 0;
    static u32   s_A51PerfSamples    = 0;
    static f32   s_A51PerfTotalMs    = 0.0f;
    static f32   s_A51PerfMaxMs      = 0.0f;
    static u64   s_A51PerfMallocCalls  = 0;
    static u64   s_A51PerfReallocCalls = 0;
    static u64   s_A51PerfFreeCalls    = 0;
    static rdraw_profile_stats s_DrawStats = {};

    const xtick A51PerfNow = x_GetTime();
    if( s_A51PerfWindowStart == 0 )
        s_A51PerfWindowStart = A51PerfNow;

    s_A51PerfSamples++;
    s_A51PerfTotalMs += RenderMs;
    s_A51PerfMaxMs = MAX( s_A51PerfMaxMs, RenderMs );
    s_A51PerfMallocCalls  += HeapEnd.MallocCalls  - HeapStart.MallocCalls;
    s_A51PerfReallocCalls += HeapEnd.ReallocCalls - HeapStart.ReallocCalls;
    s_A51PerfFreeCalls    += HeapEnd.FreeCalls    - HeapStart.FreeCalls;
    rdraw_profile_stats DrawStats = {};
    rdraw_GetAndResetProfileStats( DrawStats );
    s_DrawStats.DrawCalls += DrawStats.DrawCalls;
    s_DrawStats.IndexedDrawCalls += DrawStats.IndexedDrawCalls;
    s_DrawStats.IndirectDrawCalls += DrawStats.IndirectDrawCalls;
    s_DrawStats.IndirectCommands += DrawStats.IndirectCommands;
    s_DrawStats.SubmittedVertices += DrawStats.SubmittedVertices;
    s_DrawStats.SubmittedIndices += DrawStats.SubmittedIndices;
    s_DrawStats.SubmittedInstances += DrawStats.SubmittedInstances;
    if( x_TicksToMs( A51PerfNow - s_A51PerfWindowStart ) < 1000.0f )
        return;

    A51_PERF_LOG( "render mode=%s avg_ms=%.3f max_ms=%.3f packets=%d rigid=%d skin=%d gbuffer_draws=%u instances=%u indices=%llu runs=%u/%u api_draws=%llu indexed=%llu indirect=%llu indirect_cmds=%llu verts=%llu indices_submitted=%llu instances_submitted=%llu heap_avg_per_sample=%.1f/%.1f/%.1f",
                pMode,
                s_A51PerfSamples ? ( s_A51PerfTotalMs / s_A51PerfSamples ) : 0.0f,
                s_A51PerfMaxMs,
                g_GeomMgr.GetPacketCount(),
                g_GeomMgr.GetRigidInstanceCount(),
                g_GeomMgr.GetSkinInstanceCount(),
                g_GeomMgr.GetGBufferGpuDrawCount(),
                g_GeomMgr.GetGBufferInstanceCount(),
                static_cast<unsigned long long>( g_GeomMgr.GetGBufferSubmittedIndexCount() ),
                g_GeomMgr.GetGBufferRigidIndirectRunCount(),
                g_GeomMgr.GetGBufferSkinIndirectRunCount(),
                static_cast<unsigned long long>( s_DrawStats.DrawCalls ),
                static_cast<unsigned long long>( s_DrawStats.IndexedDrawCalls ),
                static_cast<unsigned long long>( s_DrawStats.IndirectDrawCalls ),
                static_cast<unsigned long long>( s_DrawStats.IndirectCommands ),
                static_cast<unsigned long long>( s_DrawStats.SubmittedVertices ),
                static_cast<unsigned long long>( s_DrawStats.SubmittedIndices ),
                static_cast<unsigned long long>( s_DrawStats.SubmittedInstances ),
                s_A51PerfSamples ? (double)s_A51PerfMallocCalls / s_A51PerfSamples : 0.0,
                s_A51PerfSamples ? (double)s_A51PerfReallocCalls / s_A51PerfSamples : 0.0,
                s_A51PerfSamples ? (double)s_A51PerfFreeCalls / s_A51PerfSamples : 0.0 );
#if defined( A51_ENABLE_HEAP_PROFILE )
    A51PerfLogCpuStages( FALSE );
#endif

    s_A51PerfWindowStart = A51PerfNow;
    s_A51PerfSamples = 0;
    s_A51PerfTotalMs = 0.0f;
    s_A51PerfMaxMs = 0.0f;
    s_A51PerfMallocCalls = 0;
    s_A51PerfReallocCalls = 0;
    s_A51PerfFreeCalls = 0;
    s_DrawStats = {};
}
#endif

static void RenderGameStereoXR( void )
{
#if !defined( X_RETAIL ) || defined( A51_ENABLE_HEAP_PROFILE )
    const xtick A51PerfRenderStart = x_GetTime();
    x_mem_profile_counters A51PerfHeapStart = {};
    x_MemGetProfileCounters( A51PerfHeapStart );
#endif
    if( !g_XRGameFrameBridge || !sdleng_HasVulkanExternalDevice() )
    {
        g_XRFrameDirectRenderFailed = TRUE;
        return;
    }

    if( !g_XRFrameShouldRender || g_XRFrameDirectRenderFailed )
        return;

    const xbool bCanMultiview = g_XRSession.UsesArraySwapchain() &&
        g_XRSession.SupportsMultiview() &&
        sdleng_VulkanMultiviewEnabled() &&
        g_GeomMgr.SupportsMultiview() &&
        g_PrimitiveMgr.SupportsMultiview() &&
        g_DecalRenderer.SupportsMultiview();
    if( !bCanMultiview )
    {
        x_DebugMsg( "OpenXR: array swapchain requires multiview-capable scene renderers\n" );
        g_XRFrameDirectRenderFailed = TRUE;
        return;
    }

    if( bCanMultiview )
    {
        sdleng_vulkan_frame_info ArrayFrame = {};
        if( !sdleng_GetVulkanFrameInfoForEye( 0, ArrayFrame ) )
        {
            g_XRFrameDirectRenderFailed = TRUE;
            return;
        }

        g_GBufferMgr.SetVRResolveOutputEnabled( TRUE );
        g_GBufferMgr.SetMultiviewEnabled( TRUE );
        if( !g_GBufferMgr.ResizeGBuffer( ArrayFrame.Width, ArrayFrame.Height ) )
        {
            g_XRFrameDirectRenderFailed = TRUE;
            return;
        }
        const rtarget* pXRColorTarget =
            g_GBufferMgr.GetGBufferTarget( GBufferTarget::FinalColor );
        const rtarget* pDepthTarget =
            g_GBufferMgr.GetGBufferTarget( GBufferTarget::Depth );
        if( !pXRColorTarget || (pXRColorTarget->Desc.LayerCount != 2) ||
            !pDepthTarget || (pDepthTarget->Desc.LayerCount != 2) )
        {
            g_XRFrameDirectRenderFailed = TRUE;
            return;
        }
        g_GBufferMgr.SetTargetOverride( pXRColorTarget, pDepthTarget );
        g_XRRenderWidth = ArrayFrame.Width;
        g_XRRenderHeight = ArrayFrame.Height;
        g_XRActiveEye = 2;
        g_UIRenderer.SetOutputSize( (s32)ArrayFrame.Width,
                                    (s32)ArrayFrame.Height );
        g_GeomMgr.ClearMultiviewFrameData();
        g_GBufferMgr.BeginFrame();
        RenderGame();
        g_GBufferMgr.EndPass();
        g_GeomMgr.ClearMultiviewFrameData();

        /* The UI stage runs after g_StateMgr.Render(), when HUD and pause UI
         * have both populated the draw list. It overlays the SDL scene target
         * before the final resolve into OpenXR. */
        g_UIRenderer.SetStereoOverlayTarget( pXRColorTarget );
        g_XRActiveEye = -1;
        g_XRRenderWidth = 0;
        g_XRRenderHeight = 0;
        g_XRFrameStereoRendered = TRUE;
        g_XRFrameMultiviewRendered = TRUE;
#if !defined( X_RETAIL ) || defined( A51_ENABLE_HEAP_PROFILE )
        x_mem_profile_counters A51PerfHeapEnd = {};
        x_MemGetProfileCounters( A51PerfHeapEnd );
        A51PerfRecordRenderSample( "resolve-multiview",
            x_TicksToMs( x_GetTime() - A51PerfRenderStart ),
            A51PerfHeapStart, A51PerfHeapEnd );
#endif
        return;
    }

    }
#endif

//==============================================================================
//  STATISTICS AND DEBUG FUNCTIONS
//==============================================================================

#if defined( ENABLE_DEBUG_MENU )
void Stats( f32 DeltaTime )
{
    static f32 s_StatTimer = 0.0f;
    static s32 s_AmountFree = 0;
    static s32 s_LargestFree = 0;
    static s32 s_NFragments = 0;
    static s32 s_ObjectCount = 0;
    s_StatTimer += DeltaTime;

    xbool bPrint = FALSE;
    if( s_StatTimer > g_Stats.Interval )
    {
        s_StatTimer = 0.0f;

        #ifndef X_RETAIL
        if( g_Stats.EngineStats == TRUE )
        {
            eng_PrintStats();
            bPrint = TRUE;
        }
        #endif
    }

    if( g_Stats.RenderStats == TRUE )
    {
        #if ENABLE_RENDER_STATS
        s32 Mode = render::stats::OUTPUT_TO_SCREEN;
        s32 Flags = 0;

        //if( g_Stats.RenderVerbose == TRUE )
        //    Flags = render::stats::FLAG_VERBOSE;

        render::GetStats().Print( Mode, Flags );
        bPrint = TRUE;
        #endif
    }

    if (bPrint)
    {
        PrintStatsPlatform();
    }

    #if ENABLE_STATS_MGR
    stats_mgr::GetStatsMgr()->OnGameUpdate(DeltaTime);

    if ( !eng_ScreenShotActive() )
    {
        if( eng_Begin( "StatsMgr" ) )
        {
            stats_mgr::GetStatsMgr()->DrawFPS();
            stats_mgr::GetStatsMgr()->DrawCPULegend();
            stats_mgr::GetStatsMgr()->DrawGPULegend();
            if( g_Stats.MemVertBars )
            {
                stats_mgr::GetStatsMgr()->DrawSmallBars();
                stats_mgr::GetStatsMgr()->DrawSmallBarLegend();
            }
            eng_End();
        }
    }
    #endif

}
#endif // !defined(X_RETAIL) || defined(CONFIG_PROFILE)

//==============================================================================
//  CAMERA MANAGEMENT FUNCTIONS
//==============================================================================

void SaveCamera( void )
{
    X_FILE* fp;

    const view* pView = eng_GetView();

    if( !(fp = x_fopen( xfs( "%s/camera.dat", g_FullPath ), "wb" ))) ASSERT( FALSE );
    x_fwrite( pView, sizeof( view ), 1, fp );
    x_fclose( fp );
    x_DebugMsg( "Camera saved\n" );
}

//==============================================================================

void LoadCamera( void )
{
    X_FILE* fp;
    view    TheView;

    if( !(fp = x_fopen( xfs( "%s/camera.dat", g_FullPath ), "rb" ))) return;
    x_fread( &TheView, sizeof( view ), 1, fp );
    g_View = TheView;
    x_fclose( fp );
    x_DebugMsg( "Camera loaded\n" );

    // Move the player.
    player* pPlayer = SMP_UTIL_GetActivePlayer();
    if ( pPlayer )
    {
        pPlayer->OnMoveFreeCam( g_View );
    }
}

//==============================================================================
//  LANGUAGE SUPPORT FUNCTIONS
//==============================================================================

x_language CheckLanguageSupport( x_language lang )
{

    #if defined (X_EDITOR)
    (void)lang;
    return XL_LANG_ENGLISH;
    #else

    //// temp fix for programmers
    //#if defined (TARGET_DEV)
    //switch( lang )
    //{
    //case XL_LANG_ENGLISH:
    //case XL_LANG_FRENCH:
    //case XL_LANG_ITALIAN:
    //case XL_LANG_SPANISH:
    //case XL_LANG_GERMAN:
    //case XL_LANG_RUSSIAN:
    //    return lang;
    //default:
    //    return XL_LANG_ENGLISH;
    //}
    //#else
    //
    //switch( x_GetTerritory() )
    //{
    //    case XL_TERRITORY_AMERICA:
    //        return XL_LANG_ENGLISH;
    //
    //    case XL_TERRITORY_EUROPE:
    //        switch( lang )
    //        {
    //            case XL_LANG_ENGLISH:
    //            case XL_LANG_FRENCH:
    //            case XL_LANG_ITALIAN:
    //            case XL_LANG_SPANISH:
    //            case XL_LANG_GERMAN:
    //            case XL_LANG_RUSSIAN:
    //                return lang;
    //            default:
    //                return XL_LANG_ENGLISH;
    //        }
    //
    //    default:
    //        return XL_LANG_ENGLISH;
    //}
    //#endif

    switch( lang )
    {
    case XL_LANG_ENGLISH:
    case XL_LANG_FRENCH:
    case XL_LANG_ITALIAN:
    case XL_LANG_SPANISH:
    case XL_LANG_GERMAN:
    case XL_LANG_RUSSIAN:
        return lang;
    default:
        return XL_LANG_ENGLISH;
    }

    #endif   // !defined (X_EDITOR)
}

//==============================================================================
//  STARTUP AND SHUTDOWN FUNCTIONS
//==============================================================================

void DoStartup( void )
{
    MEMORY_OWNER( "STARTUP" );

    ForceLink();

    //
    // Initialize general systems.
    //

    x_DebugMsg( "Entered app.\n" );

    if( !GameAppGetExecutableDirectory( g_FullPath, sizeof( g_FullPath ) ) )
        x_strcpy( g_FullPath, "." );

    if( !GameAppGetDataDirectory( g_DataPath, sizeof( g_DataPath ) ) )
        x_strcpy( g_DataPath, g_FullPath );

    x_DebugMsg( "Executable directory: %s\n", g_FullPath );
    x_DebugMsg( "Data directory: %s\n", g_DataPath );

    char SavePath[256];
    if( !GameAppGetSaveDirectory( SavePath, sizeof( SavePath ) ) )
        x_strcpy( SavePath, g_DataPath );

    x_DebugMsg( "Save directory: %s\n", SavePath );
    SaveDataBackend_SetRootDirectory( SavePath );

    // get language setting and check for default language.
    x_language DefaultLanguage = CheckLanguageSupport( x_GetConsoleLanguage() );

    #if !defined( CONFIG_RETAIL )
    if( g_CmdLineLanguage != -1 )
    {
        DefaultLanguage = CheckLanguageSupport( static_cast<x_language>( g_CmdLineLanguage ) );
    }
    #endif

    global_settings::SetDefaultLocalizationLanguage( DefaultLanguage );
    x_SetLocale( DefaultLanguage );

    g_StateMgr.GetActiveSettings().Reset( RESET_LOCALIZATION );
    g_StateMgr.GetPendingSettings().Reset( RESET_LOCALIZATION );

    g_SaveDataMgr.Init();

    // Restore startup consumers before the engine creates its display window.
    xbool const StartupSettingsLoaded = g_SaveDataMgr.LoadStartupSettings();
    g_StateMgr.GetActiveSettings().CommitStartup();

    guid_Init();

    x_DebugMsg( "Initialize io system\n" );

    g_IoMgr.Init();

    // Mount the default file system.
    g_IoMgr.SetDevicePathPrefix( xfs( "%s/", g_DataPath ), IO_DEVICE_HOST );

    // Xbox: cache to the utility partition.
    g_LevelLoader.MountDefaultFilesystems();

    // Load up the global configuration options from the ini file.
    // This MUST happen AFTER the io system has been initialized. Otherwise we can't
    // load the config.ini file on a viewer build.
    #ifndef CONFIG_RETAIL
    g_Config.Load( xfs( "%s/Config.ini", g_DataPath ) );
    if( g_CmdLineAutoClient )
        g_Config.AutoClient = TRUE;
    if( g_CmdLineAutoServer )
        g_Config.AutoServer = TRUE;
    #endif

 #if defined( A51_ENABLE_OPENXR ) && defined( TARGET_ANDROID )
    InitializeOpenXRBeforeEngine();
 #endif

    eng_Init();
    GameAppSetWindowIcon();

#if defined( A51_ENABLE_OPENXR ) && !defined( TARGET_ANDROID )
    {
        a51::xr::runtime_create_info XRInfo;
        if( !g_XRRuntime.Initialize( XRInfo ) )
        {
            x_DebugMsg( "OpenXR initialization failed: %s\n",
                        g_XRRuntime.GetLastError() );
        }
        else
        {
            a51::xr::system_info const& System = g_XRRuntime.GetSystemInfo();
            x_DebugMsg( "OpenXR system: %s (%ux%u)\n",
                        System.SystemName,
                        System.MaxSwapchainWidth,
                        System.MaxSwapchainHeight );

            sdleng_vulkan_device_info SDLDevice;
            if( sdleng_GetVulkanDeviceInfo( SDLDevice ) )
            {
                a51::xr::vulkan_device_info XRDevice;
                XRDevice.Instance         = SDLDevice.Instance;
                XRDevice.PhysicalDevice   = SDLDevice.PhysicalDevice;
                XRDevice.Device           = SDLDevice.Device;
                XRDevice.Queue            = SDLDevice.Queue;
                XRDevice.QueueFamilyIndex = SDLDevice.QueueFamilyIndex;
                XRDevice.MultiviewEnabled = SDLDevice.MultiviewEnabled;

                if( g_XRSession.Initialize( g_XRRuntime, XRDevice ) )
                {
                    g_XRGameFrameBridge = TRUE;
                    x_DebugMsg( "OpenXR: using SDL Vulkan device for game frames\n" );
                }
                else
                {
                    x_DebugMsg( "OpenXR SDL Vulkan session initialization failed: %s\n",
                                g_XRSession.GetLastError() );
                    // Preserve the previous validation path as a diagnostic
                    // fallback. It does not intercept the game's SDL frame.
                    if( !g_XRSession.Initialize( g_XRRuntime ) )
                    {
                        x_DebugMsg( "OpenXR fallback Vulkan session initialization failed: %s\n",
                                    g_XRSession.GetLastError() );
                    }
                }
            }
            else if( !g_XRSession.Initialize( g_XRRuntime ) )
            {
                x_DebugMsg( "OpenXR Vulkan session initialization failed: %s\n",
                            g_XRSession.GetLastError() );
            }
        }
    }
#endif

#if defined( A51_ENABLE_OPENXR )
    if( g_XRGameFrameBridge && !g_XRFrameStageRegistered )
    {
        input_SetCaptureCallback( XRInput_Capture );
        /* Must run after the UI (1000) and post-process finalization (2000). */
        eng_RegisterFrameStage( s_XRFrameStage );
        g_XRFrameStageRegistered = TRUE;
    }
#endif

    //
    // Fire up some of the major system managers.
    //

    g_ObjMgr.Init();
    g_SpatialDBase.Init( 400.0f );
    g_PostEffectMgr.Init();
    g_PlaySurfaceMgr.Init();
    g_DecalMgr.Init();
    g_AudioManager.Init( 5512*1024 );
    g_StateMgr.GetActiveSettings().CommitLocalization();

    // Initialize animation system
    anim_event::Init();

    #if defined(AUDIO_ENABLE)
    g_MusicMgr.Init();
    g_ConverseMgr.Init();
    #endif

    g_NetworkMgr.Init();
    g_GameTextMgr.Init();

    #if defined( ENABLE_DEBUG_MENU )
    // Init stats
    x_memset( &g_Stats, 0, sizeof( g_Stats ) );
    g_Stats.Interval = 1;
    #endif

    // Initialize the resource system
    x_DebugMsg( "Starting to initialize resource manager\n" );
    g_RscMgr.Init();
    g_RscMgr.SetRootDirectory( g_DataPath );
    g_RscMgr.SetOnDemandLoading( FALSE );
    x_DebugMsg( "Finished initializing resource manager\n" );

    g_LevelLoader.LoadDFS( "BOOT" );
    g_LevelLoader.LoadDFS( "PRELOAD" );

    // Initialize the render system
    render::Init();
    x_DebugMsg( "Finished initializing rendering\n" );

    // Init systems
    g_TracerMgr.Init();
    g_PhysicsMgr.Init();

    x_DebugMsg( "Loaded projected textures\n" );

    // Load the debug camera
    LoadCamera();
    x_DebugMsg( "Loaded camera\n" );

    // initialize ui manager
    g_UiMgr =  new ui_manager;
    g_UiMgr->Init();

    // load strings for inventory items.
    //g_StringTableMgr.LoadTable( "Inventory", xfs("%s\\%s", g_RscMgr.GetRootDirectory(), "ENG_Inventory_strings.stringbin" ) );
    g_StringTableMgr.LoadTable( "Inventory", "ENG_Inventory_strings.stringbin" );

    if( !StartupSettingsLoaded )
    {
        g_StateMgr.GetActiveSettings().Reset( RESET_ALL );
    }
    g_StateMgr.GetActiveSettings().Commit();

    // initialize state manager
    // MUST be done AFTER resource manager init
    g_StateMgr.Init();
    g_RscMgr.TagResources();

    #if !defined(X_RETAIL) && defined(RSC_MGR_COLLECT_STATS)
    {
        MEMORY_OWNER( "STATS" );
        g_RscMgr.DumpStats();
    }
    #endif // X_RETAIL

    // initialize save manager
    g_CheckPointMgr.Init(0);

    // Init the debug menu
    #if defined( ENABLE_DEBUG_MENU )
    g_DebugMenu.Init();
    #endif

    // Init scripting
    g_ScriptMgr.Init();
    g_ScriptMgr.RunFile( xfs( "%s/SCRIPTS/main.lua", g_DataPath ) );
}

//==============================================================================

void DoShutdown( void )
{
    g_ScriptMgr.Kill();
    g_SaveDataMgr.Kill();
    g_NetworkMgr.Kill();
    g_GameTextMgr.Kill();
    g_DecalMgr.Kill();
    g_PlaySurfaceMgr.Kill();
    #ifdef USE_MOVIES
    g_StateMgr.CloseMovie();
    #endif
    #ifdef AUDIO_ENABLE
    g_ConverseMgr.Kill();
    g_AudioMgr.UnloadAllPackages();
    g_AudioMgr.Kill();
    #endif
    g_StateMgr.Kill();
    g_TracerMgr.Kill();
    g_PhysicsMgr.Kill();
    delete g_UiMgr;
    g_UiMgr = NULL;
    render::Kill();
#if defined( A51_ENABLE_OPENXR )
    input_SetCaptureCallback( NULL );
    if( g_XRFrameStageRegistered )
    {
        eng_UnregisterFrameStage( s_XRFrameStage );
        g_XRFrameStageRegistered = FALSE;
    }
    g_XRGameFrameBridge = FALSE;
    g_XRFramePrepared = FALSE;
    g_XRSession.Shutdown();
    g_XRRuntime.Shutdown();
#endif
    eng_Kill();
    g_LevelLoader.UnmountDefaultFilesystems();
    g_IoMgr.Kill();
}

//==============================================================================
//  MAIN LOOP FUNCTIONS
//==============================================================================

void RunFrontEnd( void )
{
    MEMORY_OWNER( "RunFrontEnd()" );

    FramePacer Pacer;

    // load lore strings
    //g_StringTableMgr.LoadTable( "lore", xfs("%s\\%s", g_RscMgr.GetRootDirectory(), "ENG_lore_strings.stringbin") );
    g_StringTableMgr.LoadTable( "lore", "ENG_lore_strings.stringbin" );

    FrameTiming FrontEndTiming;
    FrontEndTiming.Start();

    while( (g_StateMgr.GetState() != SM_MULTI_PLAYER_LOAD_MISSION) &&
           (g_StateMgr.GetState() != SM_SINGLE_PLAYER_LOAD_MISSION) &&
           (g_StateMgr.GetState() != SM_RELOAD_CHECKPOINT) &&
           (g_StateMgr.GetState() != SM_DEMO_EXIT) )
    {
        if( g_StateMgr.HasPendingSimpleMovie() )
        {
            g_StateMgr.ProcessPendingSimpleMovie();
            FrontEndTiming.Restart();
            continue;
        }

        if( !UpdateFrontEnd( Pacer, FrontEndTiming ) )
        {
            break;
        }

#ifdef TARGET_DESKTOP
        if( g_Input.GetFrameSnapshot().IsPressed( INPUT_MSG_EXIT ) )
            break;
#endif
    }

    // unload string tables
    g_StringTableMgr.UnloadTable( "lore" );
}

//==============================================================================

//==============================================================================

void RunGame( void )
{
    MEMORY_OWNER( "INGAME" );

    FramePacer Pacer;

    #if !defined( CONFIG_RETAIL )
    g_MemoryLowWater = 0x7fffffff;
    #endif

    // Commit the settings again. Some of the fields require knowledge as to whether or
    // not we're a server.
    g_StateMgr.GetActiveSettings().Commit();
    // Find the level settings.
    slot_id         ID        = g_ObjMgr.GetFirst( object::TYPE_LEVEL_SETTINGS );
    level_settings* pSettings = (level_settings*)g_ObjMgr.GetObjectBySlot( ID );

    // All good?
    if( pSettings && pSettings->IsKindOf(level_settings::GetRTTI()) )
    {
        // Get the startup trigger.
        guid    GUID    = pSettings->GetStartupGuid();
        object* pObject = g_ObjMgr.GetObjectByGuid( GUID );

        // All good?
        if( pObject && pObject->IsKindOf(trigger_ex_object::GetRTTI()) )
        {
            trigger_ex_object &Trigger = trigger_ex_object::GetSafeType( *pObject );

            // Force it to be active and such!
            Trigger.ForceStartTrigger();

            // Run the trigger logic once.
            Trigger.OnAdvanceSimulation( 0.033f );

            // Now NUKE it!
            g_ObjMgr.DestroyObjectEx( GUID, TRUE );
        }
    }

    // Level is fully loaded and startup trigger has run, notify scripts.
    g_ScriptMgr.NotifyLevelStart();

    FrameTiming GameTiming;
    GameTiming.Start();

    // Run!  At least until we stop, that is.
    while( TRUE )
    {
        if( g_StateMgr.HasPendingSimpleMovie() )
        {
            g_StateMgr.ProcessPendingSimpleMovie();
            GameTiming.Restart();
            continue;
        }

        LOG_STAT( k_stats_CPU_Time );

        #if !defined( CONFIG_RETAIL )
        s32 LowWater = x_MemGetFree();
        if( LowWater < g_MemoryLowWater )
            g_MemoryLowWater = LowWater;
        #endif // !defined( CONFIG_RETAIL )

        f32 FrameDeltaSeconds = 0.0f;
        if( !BeginTimedFrame( Pacer, GameTiming, FrameDeltaSeconds ) )
        {
            break;
        }

        {
            X_PROFILE_SCOPE_CATEGORY( "Frame", "Frame.Update" );

            if( g_GameInput.UpdateFrame( FrameDeltaSeconds, GetGameInputContext() ) )
            {
                g_ActiveConfig.SetExitReason( GAME_EXIT_PLAYER_QUIT );
            }

            if( HandleInput( FrameDeltaSeconds ) == FALSE )
            {
                g_ActiveConfig.SetExitReason( GAME_EXIT_PLAYER_QUIT );
            }

            if( g_StateMgr.IsPaused() )
            {
                g_GameInput.ClearInput();
            }

            if( g_ActiveConfig.GetExitReason() == GAME_EXIT_CONTINUE )
            {
                g_PerceptionMgr.Update( FrameDeltaSeconds );

                f32 TimeScale = g_PerceptionMgr.GetGlobalTimeDialation();

                #if !defined( CONFIG_RETAIL )
                TimeScale *= g_WorldTimeDilation;
                #endif

                if( !x_isvalid( TimeScale ) || (TimeScale < 0.0f) )
                {
                    ASSERT( FALSE );
                    TimeScale = 0.0f;
                }

                g_NetworkMgr.BeginFrame( FrameDeltaSeconds );
                UpdateFrameServices( FrameDeltaSeconds );

                const f32 SimulationDeltaSeconds = ShouldAdvanceWorld()
                                                   ? (FrameDeltaSeconds * TimeScale)
                                                   : 0.0f;
                if( SimulationDeltaSeconds > 0.0f )
                {
                    AdvanceSimulation( SimulationDeltaSeconds );
                }

                g_NetworkMgr.EndFrame( FrameDeltaSeconds );
                UpdateRenderServices( FrameDeltaSeconds );
            }
        }

        if( g_ActiveConfig.GetExitReason() != GAME_EXIT_CONTINUE )
        {
            FinishClearOnlyFrame();
            break;
        }

        //
        // During the logic, the game could have "ended".  If it did, get out
        // NOW!  Do not pass GO.  Do not attempt to render.
        //

        #ifdef TARGET_DESKTOP
        if( g_Input.GetFrameSnapshot().IsPressed( INPUT_MSG_EXIT ) )
        {
            FinishClearOnlyFrame();
            break; // GAME OVER, DUDE!
        }
        #endif

        if( g_ActiveConfig.GetExitReason() != GAME_EXIT_CONTINUE )
        {
            FinishClearOnlyFrame();
            break; // GAME OVER, DUDE!
        }

        //
        // We are now reasonably caught up time-wise.  Lets show the situation.
        //

            // give the level a few frames for triggers and other objects to get
            // started before trying to render anything
            if( GameMgr.GameInProgress() )
            {
                if( g_nLogicFramesAfterLoad > 10 )
                {
#if defined( A51_ENABLE_OPENXR )
                    if( g_StateMgr.IsPaused() )
                    {
                        /* Render pause screens to the acquired XR image and
                         * submit that image as a world-space quad. */
                        g_UIRenderer.SetOutputSize( (s32)g_XRRenderWidth,
                                                    (s32)g_XRRenderHeight );
                    }
                    else
                    {
                        RenderGameStereoXR();
                    }
#else
                    RenderGame();
#endif
                }
            else if( !ClearBackBuffer() )
            {
                break;
            }

            // render the pause
            g_StateMgr.Render();

            // Some extra stuff...
            {
                #if defined( ENABLE_DEBUG_MENU )
                Stats( FrameDeltaSeconds );
                //AudioStats( FrameDeltaTime );
                #endif // X_RETAIL
            }
            if( !EndFrameWithXR() )
            {
                break;
            }
        }
        else
        {
            if( g_ActiveConfig.GetExitReason()==GAME_EXIT_CONTINUE )
            {
                g_ActiveConfig.SetExitReason( GAME_EXIT_ADVANCE_LEVEL );
            }
            FinishClearOnlyFrame();
            break;
        }
    }
    //
    // Was the user in the pause menu - unexpected exit condition.
    //
    if( g_StateMgr.IsPaused() )
    {
        // reset the pause flag
        g_StateMgr.ClearPause();
        // set the state to idle whilst we wait
        g_StateMgr.SetState( SM_IDLE );
    }

    // Notify scripts that the level is ending.
    g_ScriptMgr.NotifyLevelEnd();

    //
    // Enable the user interface for the primary user.
    //
    g_UiMgr->EnableUser( g_UiUserID, TRUE );

    #ifndef CONFIG_RETAIL
    // Stop our automated client/server stuff while trying to test the game exit
    // code.
    g_Config.AutoClient = FALSE;
    g_Config.AutoServer = FALSE;
    #endif
}

//==============================================================================
//  MAIN APPLICATION ENTRY POINT
//==============================================================================

void AppMain( s32 argc, char* argv[] )
{
    // Parse out the command line arguments
#ifndef X_RETAIL

    extern xbool AUDIO_TWEAK;
    {
        for( s32 i=1; i<argc; i++ )
        {
            if( x_stricmp( argv[i], "autoserver" ) == 0 )
                g_CmdLineAutoServer = TRUE;

            if( x_stricmp( argv[i], "autoclient" ) == 0 )
                g_CmdLineAutoClient = TRUE;

            if( x_stricmp( argv[i], "audiotweak" ) == 0 )
                AUDIO_TWEAK = TRUE;

            if( x_stricmp( argv[i], "rtfhandler" ) == 0 )
                g_CmdLineRTFHandler = TRUE;

            if( x_stricmp( argv[i], "language" ) == 0 )
            {
                i++;
                if( i<argc )
                {
                    if( x_stricmp( argv[i], "eng" ) == 0 )
                    {
                        g_CmdLineLanguage = XL_LANG_ENGLISH;
                    }
                    if( x_stricmp( argv[i], "fre" ) == 0 )
                    {
                        g_CmdLineLanguage = XL_LANG_FRENCH;
                    }
                    if( x_stricmp( argv[i], "ger" ) == 0 )
                    {
                        g_CmdLineLanguage = XL_LANG_GERMAN;
                    }
                    if( x_stricmp( argv[i], "ita" ) == 0 )
                    {
                        g_CmdLineLanguage = XL_LANG_ITALIAN;
                    }
                    if( x_stricmp( argv[i], "spa" ) == 0 )
                    {
                        g_CmdLineLanguage = XL_LANG_SPANISH;
                    }
                    if( x_stricmp( argv[i], "dut" ) == 0 )
                    {
                        g_CmdLineLanguage = XL_LANG_DUTCH;
                    }
                    if( x_stricmp( argv[i], "jpn" ) == 0 )
                    {
                        g_CmdLineLanguage = XL_LANG_JAPANESE;
                    }
                    if( x_stricmp( argv[i], "kor" ) == 0 )
                    {
                        g_CmdLineLanguage = XL_LANG_KOREAN;
                    }
                    if( x_stricmp( argv[i], "por" ) == 0 )
                    {
                        g_CmdLineLanguage = XL_LANG_PORTUGUESE;
                    }
                    if( x_stricmp( argv[i], "chi" ) == 0 )
                    {
                        g_CmdLineLanguage = XL_LANG_TCHINESE;
                    }
                }
            }
        }
    }
#endif

    (void)argc;
    (void)argv;
    xbool bFullLevelLoad = TRUE;

    MEMORY_OWNER( "DYNAMIC" );

    //
    // Do core startup
    //
    DoStartup();
    // Initialize editable settings from the values restored during startup.
    g_StateMgr.InitPendingSettings();

    FramePacer AppFramePacer;

    //
    // We're starting off with no exit condition. This will force the 'RunFrontEnd' to
    // start afresh. RunFrontEnd will decide where we need to go next.
    //
    g_ActiveConfig.SetExitReason( GAME_EXIT_CONTINUE );
    //
    // Loop forever playing our fabulous game
    //
    s32 nLoops = 0;
    sm_states CooldownState;

    while( TRUE )
    {
        nLoops++;

        //
        // Did a level trigger request a new level or do we
        // need to talk to the frontend?
        //
        LOG_MEMMARK( "RunFrontEnd" );
        RunFrontEnd();

        while( g_StateMgr.HasPendingSimpleMovie() )
        {
            g_StateMgr.ProcessPendingSimpleMovie();
        }

        if( g_NetworkMgr.IsServer() )
        {
            CooldownState = SM_SERVER_COOLDOWN;
        }
        else
        {
            CooldownState = SM_CLIENT_COOLDOWN;
        }

        LOG_MEMMARK( "LoadLevel" );
        // Bail if the app is closed
#ifdef TARGET_DESKTOP
        if( g_Input.GetFrameSnapshot().IsPressed( INPUT_MSG_EXIT ) )
            break;
#endif

        xbool LoadEntireLevel = bFullLevelLoad;

        // Is this a new game or are we restoring from save data?
        if( g_StateMgr.IsRestoredGame() )
        {
            // Not a full level load...
            bFullLevelLoad = FALSE;
            LoadEntireLevel = TRUE;
        }

        // Keep the front end, slideshow, audio, input and network alive while
        // the level loader advances one bounded stage at a time.
        g_LevelLoader.BeginLevelLoad( LoadEntireLevel );

        FrameTiming LevelLoadFrontEndTiming;
        LevelLoadFrontEndTiming.Start();

        while( !g_LevelLoader.IsLevelLoadComplete() )
        {
            if( !UpdateLevelLoadingFrame( LevelLoadFrontEndTiming ) )
            {
                g_ActiveConfig.SetExitReason( GAME_EXIT_PLAYER_QUIT );
                break;
            }
        }

        if( (g_ActiveConfig.GetExitReason() == GAME_EXIT_INVALID_MISSION) ||
            (g_ActiveConfig.GetExitReason() == GAME_EXIT_INVALID_CAMPAIGN_MISSION) )
        {
            g_StateMgr.SetState( CooldownState );
            continue;
        }

        // Tell network manager load is complete!
        g_NetworkMgr.LoadMissionComplete();


        FrameTiming SyncFrontEndTiming;
        SyncFrontEndTiming.Start();
        //
        // We have to wait until the statemgr has said that everything is ready to go. This is what detects whether or
        // not the sync phase has completed.
        //
        while( (g_StateMgr.GetState() != SM_PLAYING_GAME) && (g_ActiveConfig.GetExitReason() == GAME_EXIT_CONTINUE) )
        {
            if( !UpdateFrontEnd( AppFramePacer, SyncFrontEndTiming ) )
            {
                g_ActiveConfig.SetExitReason( GAME_EXIT_PLAYER_QUIT );
                break;
            }
        }

        // Finish the level initialization that depends on front-end sync.
        g_LevelLoader.LoadLevelFinish();

        // During the level load process it is possible for an error such as duplicate logon to occur.
        // When this happens the exit reason will be set to something other than GAME_EXIT_CONTINUE.
        // This has the effect of denying the game from creating all the necessary objects such as players etc.
        // We must therefore only attempt a checkpoint restore if we are sure there were no errors on load.
        if( g_ActiveConfig.GetExitReason() == GAME_EXIT_CONTINUE )
        {
            // Full level load?
            if( bFullLevelLoad )
            {
                // Restore the player inventory (if necessary)
                g_StateMgr.RestorePlayerInventory();
            }
            else
            {
                // Suck in the check point info.
                g_CheckPointMgr.Restore( FALSE );
            }
        }

        LOG_MEMMARK( "RunGame" );
        if( g_ActiveConfig.GetExitReason() == GAME_EXIT_CONTINUE )
        {
            // Only for campaign and full level loads set the initial checkpoint.
            if( (GameMgr.GetGameType() == GAME_CAMPAIGN) && bFullLevelLoad )
            {
                // Start again!
                g_CheckPointMgr.Reinit( g_ActiveConfig.GetLevelID() );

                // Set the initial checkpoint!
                g_CheckPointMgr.SetCheckPoint( NULL_GUID, NULL_GUID, -1, -1 );
                g_StateMgr.SilentSaveProfile();
            }

            // Play the game until we hit an exit condition
            RunGame();
        }

        // Clean out any existing feedback info.
        // This prevents us from rumbling when input updates next time.
        g_Input.ClearFeedback();

        // Bail if the app is closed
#ifdef TARGET_DESKTOP
        if( g_Input.GetFrameSnapshot().IsPressed( INPUT_MSG_EXIT ) )
        {
            g_LevelLoader.UnloadLevel( TRUE );
            break;
        }
#endif

        // Default to complete level load.
        bFullLevelLoad = TRUE;

        g_StateMgr.SetPaused( FALSE, g_StateMgr.GetActiveControllerID() );
        // Special cases for campaign games.
        if( GameMgr.GetGameType() == GAME_CAMPAIGN )
        {
            // Decide what to do based on the exit reason!
            switch( g_ActiveConfig.GetExitReason() )
            {
                case GAME_EXIT_ADVANCE_LEVEL:
                    // Backup the player inventory!
                    g_StateMgr.BackupPlayerInventory();
                    break;

                case GAME_EXIT_RELOAD_CHECKPOINT:
                    // Just load the objects.
                    bFullLevelLoad = FALSE;
                    break;

                default:
                    break;
            }
        }

        FrameTiming CooldownFrontEndTiming;
        CooldownFrontEndTiming.Start();

        //
        // We have to wait until the statemgr has said all the subsystems have cooled down. This makes sure
        // that we do not have any game traffic going on while the level is unloading.
        //
        if( g_StateMgr.GetState() != CooldownState )
        {
            g_StateMgr.SetState( CooldownState );
        }

        g_NetworkMgr.UpdateFrame( 0.01f );
        g_NetworkMgr.UpdateFrame( 0.01f );
        g_NetworkMgr.UpdateFrame( 0.01f );

        //
        // Unload the level
        //
        LOG_MEMMARK( "UnloadLevel" );
        g_LevelLoader.UnloadLevel( bFullLevelLoad );
        LOG_FLUSH();
        while( g_StateMgr.GetState() == CooldownState )
        {
            if( !UpdateFrontEnd( AppFramePacer, CooldownFrontEndTiming ) )
            {
                g_ActiveConfig.SetExitReason( GAME_EXIT_PLAYER_QUIT );
                break;
            }
        }
    }

    //
    // Shutdown systems involved in Startup
    //
    DoShutdown();
}

//==============================================================================

#if defined( TARGET_POSIX )
int main( int argc, char* argv[] )
{
    x_Init( argc, argv );
    AppMain( (s32)argc, argv );
    return eng_ExitPoint();
}
#endif // defined( TARGET_POSIX )

//==============================================================================
