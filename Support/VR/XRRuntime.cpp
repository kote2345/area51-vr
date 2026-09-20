//==============================================================================
//
//  XRRuntime.cpp
//
//==============================================================================

#include "XRRuntime.hpp"

#include "x_memory.hpp"
#include "x_string.hpp"

namespace a51::xr
{

runtime_create_info::runtime_create_info()
    : ApplicationName    ( "Area 51" )
    , EngineName         ( "Entropy" )
    , ApplicationVersion ( 1 )
    , EngineVersion      ( 1 )
{
}

system_info::system_info()
    : SystemId           ( 0 )
    , VendorId           ( 0 )
    , MaxSwapchainWidth  ( 0 )
    , MaxSwapchainHeight ( 0 )
    , MaxLayerCount      ( 0 )
{
    x_memset( SystemName, 0, sizeof(SystemName) );
}

Runtime::Runtime()
    : m_pInstance ( NULL )
    , m_State     ( runtime_state::Uninitialized )
    , m_System    ()
{
    x_memset( m_LastError, 0, sizeof(m_LastError) );
}

Runtime::~Runtime()
{
    Shutdown();
}

xbool Runtime::Initialize( const runtime_create_info& Info )
{
    if( m_State == runtime_state::SystemReady ||
        m_State == runtime_state::InstanceReady )
    {
        return TRUE;
    }

    Shutdown();
    x_memset( m_LastError, 0, sizeof(m_LastError) );

    if( !PlatformInitializeLoader() )
    {
        x_strncpy( m_LastError, "OpenXR loader initialization failed",
                   sizeof(m_LastError) );
        m_State = runtime_state::Failed;
        return FALSE;
    }

    if( !PlatformCreateInstance( Info, &m_pInstance,
                                 m_LastError, sizeof(m_LastError) ) )
    {
        PlatformShutdownLoader();
        m_State = runtime_state::Failed;
        return FALSE;
    }

    m_State = runtime_state::InstanceReady;

    if( !PlatformGetSystemInfo( m_pInstance, m_System,
                                m_LastError, sizeof(m_LastError) ) )
    {
        Shutdown();
        m_State = runtime_state::Failed;
        return FALSE;
    }

    m_State = runtime_state::SystemReady;
    return TRUE;
}

void Runtime::Shutdown( void )
{
    if( m_pInstance )
    {
        PlatformDestroyInstance( m_pInstance );
        m_pInstance = NULL;
    }

    PlatformShutdownLoader();
    m_System = system_info();

    if( m_State != runtime_state::Uninitialized )
        m_State = runtime_state::Stopped;
}

runtime_state Runtime::GetState( void ) const
{
    return m_State;
}

system_info const& Runtime::GetSystemInfo( void ) const
{
    return m_System;
}

void* Runtime::GetInstanceHandle( void ) const
{
    return m_pInstance;
}

u64 Runtime::GetSystemId( void ) const
{
    return m_System.SystemId;
}

const char* Runtime::GetLastError( void ) const
{
    return m_LastError;
}

} // namespace a51::xr
