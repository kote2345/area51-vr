//==============================================================================
//
//  XRRuntime_Android.cpp
//
//==============================================================================

#include "XRRuntime.hpp"

#include "SDL3/SDL_system.h"

#include <jni.h>

#define XR_USE_GRAPHICS_API_VULKAN 1
#define XR_USE_PLATFORM_ANDROID 1
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace a51::xr
{

static xbool CheckResult( XrResult Result, const char* pOperation,
                          char* pError, s32 ErrorSize )
{
    if( XR_SUCCEEDED( Result ) )
        return TRUE;

    if( pError && (ErrorSize > 0) )
    {
        std::snprintf( pError, static_cast<size_t>(ErrorSize),
                       "OpenXR %s failed with result %d", pOperation,
                       static_cast<int>(Result) );
    }
    return FALSE;
}

xbool PlatformInitializeLoader( void )
{
    PFN_xrVoidFunction Function = NULL;
    XrResult Result = xrGetInstanceProcAddr( XR_NULL_HANDLE,
                                              "xrInitializeLoaderKHR",
                                              &Function );
    if( XR_FAILED( Result ) || !Function )
        return FALSE;

    JNIEnv* Env = static_cast<JNIEnv*>( SDL_GetAndroidJNIEnv() );
    jobject Activity = static_cast<jobject>( SDL_GetAndroidActivity() );
    if( !Env || !Activity )
        return FALSE;

    JavaVM* VM = NULL;
    if( Env->GetJavaVM( &VM ) != JNI_OK || !VM )
        return FALSE;

    XrLoaderInitInfoAndroidKHR LoaderInfo{
        XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR
    };
    LoaderInfo.applicationVM      = VM;
    LoaderInfo.applicationContext = Activity;

    PFN_xrInitializeLoaderKHR InitializeLoader =
        reinterpret_cast<PFN_xrInitializeLoaderKHR>( Function );
    return XR_SUCCEEDED( InitializeLoader(
        reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>( &LoaderInfo ) ) )
         ? TRUE : FALSE;
}

void PlatformShutdownLoader( void )
{
}

xbool PlatformCreateInstance( const runtime_create_info& Info,
                               void**                    ppInstance,
                               char*                     pError,
                               s32                       ErrorSize )
{
    if( !ppInstance )
        return FALSE;

    std::vector<const char*> Extensions = {
        XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
        XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME,
    };

    // Request the Quest refresh-rate control only when the active OpenXR
    // runtime advertises it; unsupported runtimes must still start normally.
    uint32_t ExtensionCount = 0;
    if( XR_SUCCEEDED( xrEnumerateInstanceExtensionProperties(
                          NULL, 0, &ExtensionCount, NULL ) ) &&
        ( ExtensionCount > 0 ) )
    {
        std::vector<XrExtensionProperties> AvailableExtensions( ExtensionCount );
        for( XrExtensionProperties& Extension : AvailableExtensions )
        {
            Extension = { XR_TYPE_EXTENSION_PROPERTIES };
        }

        if( XR_SUCCEEDED( xrEnumerateInstanceExtensionProperties(
                              NULL, ExtensionCount, &ExtensionCount,
                              AvailableExtensions.data() ) ) )
        {
            for( const XrExtensionProperties& Extension : AvailableExtensions )
            {
                if( std::strcmp( Extension.extensionName,
                                 XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME ) == 0 )
                {
                    Extensions.push_back(
                        XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME );
                }
                if( std::strcmp( Extension.extensionName,
                                 XR_META_PERFORMANCE_METRICS_EXTENSION_NAME ) == 0 )
                    Extensions.push_back( XR_META_PERFORMANCE_METRICS_EXTENSION_NAME );
            }
        }
    }

    XrInstanceCreateInfoAndroidKHR AndroidInfo{
        XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR
    };

    JNIEnv* Env = static_cast<JNIEnv*>( SDL_GetAndroidJNIEnv() );
    jobject Activity = static_cast<jobject>( SDL_GetAndroidActivity() );
    JavaVM* VM = NULL;
    if( !Env || !Activity || Env->GetJavaVM( &VM ) != JNI_OK || !VM )
        return FALSE;
    AndroidInfo.applicationVM      = VM;
    AndroidInfo.applicationActivity = Activity;

    XrInstanceCreateInfo CreateInfo{ XR_TYPE_INSTANCE_CREATE_INFO };
    CreateInfo.next = &AndroidInfo;
    CreateInfo.enabledExtensionCount = static_cast<uint32_t>( Extensions.size() );
    CreateInfo.enabledExtensionNames = Extensions.data();
    CreateInfo.applicationInfo.applicationVersion = Info.ApplicationVersion;
    CreateInfo.applicationInfo.engineVersion       = Info.EngineVersion;
    CreateInfo.applicationInfo.apiVersion          = XR_MAKE_VERSION( 1, 0, 0 );

    std::snprintf( CreateInfo.applicationInfo.applicationName,
                   XR_MAX_APPLICATION_NAME_SIZE, "%s",
                   Info.ApplicationName ? Info.ApplicationName : "Area 51" );
    std::snprintf( CreateInfo.applicationInfo.engineName,
                   XR_MAX_ENGINE_NAME_SIZE, "%s",
                   Info.EngineName ? Info.EngineName : "Entropy" );

    XrInstance Instance = XR_NULL_HANDLE;
    if( !CheckResult( xrCreateInstance( &CreateInfo, &Instance ),
                      "xrCreateInstance", pError, ErrorSize ) )
    {
        return FALSE;
    }

    *ppInstance = reinterpret_cast<void*>( Instance );
    return TRUE;
}

void PlatformDestroyInstance( void* pInstance )
{
    if( pInstance )
        xrDestroyInstance( reinterpret_cast<XrInstance>( pInstance ) );
}

xbool PlatformGetSystemInfo( void*       pInstance,
                              system_info& Info,
                              char*        pError,
                              s32          ErrorSize )
{
    if( !pInstance )
        return FALSE;

    XrInstance Instance = reinterpret_cast<XrInstance>( pInstance );
    XrSystemGetInfo GetInfo{ XR_TYPE_SYSTEM_GET_INFO };
    GetInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    XrSystemId SystemId = XR_NULL_SYSTEM_ID;
    if( !CheckResult( xrGetSystem( Instance, &GetInfo, &SystemId ),
                      "xrGetSystem", pError, ErrorSize ) )
    {
        return FALSE;
    }

    XrSystemProperties Properties{ XR_TYPE_SYSTEM_PROPERTIES };
    if( !CheckResult( xrGetSystemProperties( Instance, SystemId, &Properties ),
                      "xrGetSystemProperties", pError, ErrorSize ) )
    {
        return FALSE;
    }

    Info.SystemId           = static_cast<u64>( SystemId );
    Info.VendorId           = Properties.vendorId;
    Info.MaxSwapchainWidth  = Properties.graphicsProperties.maxSwapchainImageWidth;
    Info.MaxSwapchainHeight = Properties.graphicsProperties.maxSwapchainImageHeight;
    Info.MaxLayerCount      = Properties.graphicsProperties.maxLayerCount;
    std::snprintf( Info.SystemName, sizeof(Info.SystemName), "%s",
                   Properties.systemName );
    return TRUE;
}

} // namespace a51::xr
