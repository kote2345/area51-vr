//==============================================================================
//
//  A51Perf.hpp
//
//  Low frequency CPU workload profiling for the Quest diagnostic build.
//
//==============================================================================

#ifndef A51_PERF_HPP
#define A51_PERF_HPP

#include "x_time.hpp"
#include "e_RenderDraw.hpp"

#if defined( TARGET_ANDROID ) && defined( A51_ENABLE_HEAP_PROFILE )
#include <android/trace.h>
#endif

namespace a51::perf
{

enum cpu_stage
{
    SIM_PHYSICS,
    SIM_OBJECTS,
    SIM_ALIENS,
    SIM_SCRIPTS,
    SIM_TRIGGERS,
    SIM_NETWORK,
    SIM_STAGE_COUNT,

    RENDER_GAME = SIM_STAGE_COUNT,
    RENDER_OBJECTS,
    RENDER_PREPARE,
    RENDER_COLLECT,
    RENDER_PLAYSURFACES,
    RENDER_DECALS,
    RENDER_LIGHTS,
    RENDER_SUBMIT_GEOMETRY,
    RENDER_SPECIAL_OBJECTS,
    RENDER_GEOM_BUILD,
    RENDER_GEOM_UPLOAD,
    RENDER_GEOM_GBUFFER,
    RENDER_GEOM_DIRECT,
    RENDER_POST_EFFECTS,
    RENDER_STAGE_COUNT
};

#if defined( A51_ENABLE_HEAP_PROFILE )
void RecordCpuStage( cpu_stage Stage, xtick DurationTicks );

class scope
{
public:
    scope( cpu_stage Stage, const char* pName )
        : m_Stage( Stage ), m_Start( x_GetTime() ), m_GpuGroup( FALSE )
    {
#if defined( TARGET_ANDROID )
        ATrace_beginSection( pName );
#endif
        m_GpuGroup = rdraw_PushDebugGroup( pName );
    }

    ~scope()
    {
#if defined( TARGET_ANDROID )
        ATrace_endSection();
#endif
        if( m_GpuGroup )
            rdraw_PopDebugGroup();
        RecordCpuStage( m_Stage, x_GetTime() - m_Start );
    }

private:
    cpu_stage m_Stage;
    xtick     m_Start;
    xbool     m_GpuGroup;
};

#define A51_PERF_JOIN_IMPL(a,b) a##b
#define A51_PERF_JOIN(a,b) A51_PERF_JOIN_IMPL(a,b)
#define A51_PERF_SCOPE(Stage, Name) \
    a51::perf::scope A51_PERF_JOIN( _a51PerfScope_, __LINE__ )( a51::perf::Stage, Name )
#else
#define A51_PERF_SCOPE(Stage, Name) ((void)0)
#endif

} // namespace a51::perf

#endif // A51_PERF_HPP
