//==============================================================================
//
//  XRRuntime.hpp
//
//  Platform-neutral OpenXR instance/system bootstrap. Graphics bindings and
//  frame submission are deliberately kept behind this boundary so the same
//  XR layer can serve PCVR and Quest without changing gameplay code.
//
//==============================================================================

#ifndef A51_XR_RUNTIME_HPP
#define A51_XR_RUNTIME_HPP

#include "x_types.hpp"

namespace a51::xr
{

enum class runtime_state : u8
{
    Uninitialized,
    InstanceReady,
    SystemReady,
    SessionReady,
    Stopped,
    Failed,
};

struct runtime_create_info
{
    const char* ApplicationName;
    const char* EngineName;
    u32         ApplicationVersion;
    u32         EngineVersion;

    runtime_create_info();
};

struct system_info
{
    u64  SystemId;
    u32  VendorId;
    char SystemName[128];
    u32  MaxSwapchainWidth;
    u32  MaxSwapchainHeight;
    u32  MaxLayerCount;

    system_info();
};

class Runtime
{
public:
                    Runtime         ( void );
                    ~Runtime        ( void );

    xbool           Initialize      ( const runtime_create_info& Info );
    void            Shutdown        ( void );

    runtime_state   GetState        ( void ) const;
    system_info const&
                    GetSystemInfo   ( void ) const;
    void*           GetInstanceHandle( void ) const;
    u64             GetSystemId     ( void ) const;
    const char*     GetLastError    ( void ) const;

private:
                    Runtime         ( const Runtime& ) = delete;
    Runtime&        operator=      ( const Runtime& ) = delete;

    void*           m_pInstance;
    runtime_state   m_State;
    system_info     m_System;
    char            m_LastError[256];
};

// Called by XRRuntime.cpp and implemented by the target-specific loader glue.
xbool PlatformInitializeLoader( void );
void  PlatformShutdownLoader  ( void );
xbool PlatformCreateInstance ( const runtime_create_info& Info,
                               void**                    ppInstance,
                               char*                     pError,
                               s32                       ErrorSize );
void  PlatformDestroyInstance( void* pInstance );
xbool PlatformGetSystemInfo  ( void*       pInstance,
                               system_info& Info,
                               char*        pError,
                               s32          ErrorSize );

} // namespace a51::xr

#endif // A51_XR_RUNTIME_HPP
