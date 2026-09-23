//==============================================================================
//
//  XRSession.cpp
//
//==============================================================================

#include "XRSession.hpp"

#include "XRRuntime.hpp"
#include "e_Input.hpp"

#define XR_USE_GRAPHICS_API_VULKAN 1
#if defined(TARGET_ANDROID)
#define XR_USE_PLATFORM_ANDROID 1
#include <jni.h>
#endif

#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cmath>
#include <vector>
#if defined(TARGET_ANDROID)
#include <android/log.h>
#include "x_time.hpp"
#endif

namespace a51::xr
{

namespace
{

#if defined(TARGET_ANDROID)
void RecordSwapchainWaitMs( f32 WaitMs )
{
    static xtick s_WindowStart = 0;
    static u32   s_Samples = 0;
    static f32   s_TotalMs = 0.0f;
    static f32   s_MaxMs = 0.0f;
    const xtick Now = x_GetTime();
    if( !s_WindowStart )
        s_WindowStart = Now;
    ++s_Samples;
    s_TotalMs += WaitMs;
    s_MaxMs = MAX( s_MaxMs, WaitMs );
    if( x_TicksToMs( Now - s_WindowStart ) < 1000.0f )
        return;
    __android_log_print( ANDROID_LOG_INFO, "A51Perf",
        "xr_swapchain_wait samples=%u avg_ms=%.3f max_ms=%.3f",
        s_Samples, s_Samples ? s_TotalMs / s_Samples : 0.0f, s_MaxMs );
    s_WindowStart = Now;
    s_Samples = 0;
    s_TotalMs = 0.0f;
    s_MaxMs = 0.0f;
}
#endif

void SetError( char* pError, size_t ErrorSize, const char* pFormat, ... )
{
    if( !pError || (ErrorSize == 0) )
        return;

    va_list Arguments;
    va_start( Arguments, pFormat );
    std::vsnprintf( pError, ErrorSize, pFormat, Arguments );
    va_end( Arguments );
}

xbool CheckXr( XrResult Result, const char* pOperation,
               char* pError, size_t ErrorSize )
{
    if( XR_SUCCEEDED( Result ) )
        return TRUE;

    SetError( pError, ErrorSize, "OpenXR %s failed with result %d",
              pOperation, static_cast<int>( Result ) );
    return FALSE;
}

xbool CheckVk( VkResult Result, const char* pOperation,
               char* pError, size_t ErrorSize )
{
    if( Result == VK_SUCCESS )
        return TRUE;

    SetError( pError, ErrorSize, "Vulkan %s failed with result %d",
              pOperation, static_cast<int>( Result ) );
    return FALSE;
}

template<typename T>
xbool LoadXrFunction( XrInstance Instance, const char* pName, T& Function,
                      char* pError, size_t ErrorSize )
{
    PFN_xrVoidFunction VoidFunction = NULL;
    if( !CheckXr( xrGetInstanceProcAddr( Instance, pName, &VoidFunction ),
                  pName, pError, ErrorSize ) || !VoidFunction )
    {
        SetError( pError, ErrorSize, "OpenXR function %s is unavailable", pName );
        return FALSE;
    }

    Function = reinterpret_cast<T>( VoidFunction );
    return TRUE;
}

VkFormat SelectColorFormat( const std::vector<int64_t>& Formats )
{
    const VkFormat Preferred[] = {
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_FORMAT_B8G8R8A8_SRGB,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8A8_UNORM,
    };

    for( VkFormat Candidate : Preferred )
    {
        if( std::find( Formats.begin(), Formats.end(),
                       static_cast<int64_t>( Candidate ) ) != Formats.end() )
        {
            return Candidate;
        }
    }

    // The runtime owns the supported-format list, so any format it reports
    // is valid for the swapchain. Keep the bootstrap portable to runtimes
    // that expose only a non-preferred color format.
    return Formats.empty() ? VK_FORMAT_UNDEFINED
                           : static_cast<VkFormat>( Formats[0] );
}

XrQuaternionf InverseUnitQuaternion( const XrQuaternionf& Q )
{
    XrQuaternionf Result = { -Q.x, -Q.y, -Q.z, Q.w };
    return Result;
}

XrQuaternionf MultiplyQuaternion( const XrQuaternionf& A,
                                   const XrQuaternionf& B )
{
    XrQuaternionf Result;
    Result.x = (A.w * B.x) + (B.w * A.x) +
               (A.y * B.z) - (A.z * B.y);
    Result.y = (A.w * B.y) + (B.w * A.y) +
               (A.z * B.x) - (A.x * B.z);
    Result.z = (A.w * B.z) + (B.w * A.z) +
               (A.x * B.y) - (A.y * B.x);
    Result.w = (A.w * B.w) - (B.x * A.x) -
               (A.y * B.y) - (A.z * B.z);
    return Result;
}

XrVector3f RotateVector( const XrQuaternionf& Q, const XrVector3f& V )
{
    const XrQuaternionf VectorQuaternion = { V.x, V.y, V.z, 0.0f };
    const XrQuaternionf Rotated = MultiplyQuaternion(
        MultiplyQuaternion( Q, VectorQuaternion ),
        InverseUnitQuaternion( Q ) );
    return { Rotated.x, Rotated.y, Rotated.z };
}

XrPosef RelativePose( const XrPosef& Origin, const XrPosef& Pose )
{
    const XrQuaternionf Inverse = InverseUnitQuaternion( Origin.orientation );
    const XrVector3f Delta = {
        Pose.position.x - Origin.position.x,
        Pose.position.y - Origin.position.y,
        Pose.position.z - Origin.position.z };

    XrPosef Result;
    Result.orientation = MultiplyQuaternion( Inverse, Pose.orientation );
    Result.position = RotateVector( Inverse, Delta );
    return Result;
}

XrQuaternionf YawOnly( const XrQuaternionf& Orientation )
{
    const XrVector3f Forward = RotateVector( Orientation, { 0.0f, 0.0f, -1.0f } );
    f32 Yaw = 0.0f;
    if( (Forward.x * Forward.x + Forward.z * Forward.z) > 0.0001f )
    {
        Yaw = std::atan2( -Forward.x, -Forward.z );
    }
    else
    {
        const XrVector3f Right = RotateVector( Orientation, { 1.0f, 0.0f, 0.0f } );
        Yaw = std::atan2( -Right.z, Right.x );
    }

    return { 0.0f, std::sin( Yaw * 0.5f ), 0.0f,
             std::cos( Yaw * 0.5f ) };
}

XrPosef CentreYawAnchor( const XrView& Left, const XrView& Right )
{
    XrPosef Result = Left.pose;
    Result.orientation = YawOnly( Left.pose.orientation );
    Result.position.x = (Left.pose.position.x + Right.pose.position.x) * 0.5f;
    Result.position.y = (Left.pose.position.y + Right.pose.position.y) * 0.5f;
    Result.position.z = (Left.pose.position.z + Right.pose.position.z) * 0.5f;
    return Result;
}

} // anonymous namespace

struct VulkanSession::Impl
{
#if defined(TARGET_ANDROID)
    struct PerformanceCounter
    {
        XrPath Path;
        char   Name[96];
    };
    PFN_xrEnumeratePerformanceMetricsCounterPathsMETA EnumeratePerformanceCounters = NULL;
    PFN_xrSetPerformanceMetricsStateMETA SetPerformanceMetricsState = NULL;
    PFN_xrQueryPerformanceMetricsCounterMETA QueryPerformanceCounter = NULL;
    std::vector<PerformanceCounter> PerformanceCounters;
    xtick LastPerformanceLog = 0;
#endif
    struct Swapchain
    {
        XrSwapchain Handle = XR_NULL_HANDLE;
        u32 Width = 0;
        u32 Height = 0;
        VkFormat Format = VK_FORMAT_UNDEFINED;
        std::vector<XrSwapchainImageVulkan2KHR> Images;
        std::vector<VkImageView> ImageViews;
        std::vector<VkFramebuffer> Framebuffers;
    };

    XrInstance Instance = XR_NULL_HANDLE;
    XrSystemId SystemId = XR_NULL_SYSTEM_ID;
    XrSession Session = XR_NULL_HANDLE;
#if defined(TARGET_ANDROID)
    PFN_xrRequestDisplayRefreshRateFB RequestDisplayRefreshRate = NULL;
    PFN_xrGetDisplayRefreshRateFB GetDisplayRefreshRate = NULL;
#endif
    XrSpace Space = XR_NULL_HANDLE;
    XrViewConfigurationType ViewConfigurationType =
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    XrSessionState SessionState = XR_SESSION_STATE_UNKNOWN;

    VkInstance VkInstanceHandle = VK_NULL_HANDLE;
    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
    VkDevice Device = VK_NULL_HANDLE;
    VkQueue Queue = VK_NULL_HANDLE;
    u32 QueueFamilyIndex = 0;
    VkCommandPool CommandPool = VK_NULL_HANDLE;
    VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
    VkFence Fence = VK_NULL_HANDLE;
    VkRenderPass RenderPass = VK_NULL_HANDLE;

    std::vector<XrViewConfigurationView> ViewConfigurationViews;
    std::vector<XrView> Views;
    std::vector<Swapchain> Swapchains;
    std::vector<XrCompositionLayerProjectionView> ProjectionViews;
    std::vector<u32> AcquiredImageIndices;
    std::vector<xbool> AcquiredImages;
    XrCompositionLayerProjection ProjectionLayer{
        XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    XrCompositionLayerQuad QuadLayer{ XR_TYPE_COMPOSITION_LAYER_QUAD };
    XrActionSet InputActionSet = XR_NULL_HANDLE;
    XrAction LeftStickAction = XR_NULL_HANDLE;
    XrAction RightStickAction = XR_NULL_HANDLE;
    XrAction LeftTriggerAction = XR_NULL_HANDLE;
    XrAction RightTriggerAction = XR_NULL_HANDLE;
    XrAction LeftXAction = XR_NULL_HANDLE;
    XrAction LeftYAction = XR_NULL_HANDLE;
    XrAction RightAAction = XR_NULL_HANDLE;
    XrAction RightBAction = XR_NULL_HANDLE;
    XrAction LeftStickClickAction = XR_NULL_HANDLE;
    XrAction RightStickClickAction = XR_NULL_HANDLE;
    XrAction LeftMenuAction = XR_NULL_HANDLE;
    xbool InputActionsReady = FALSE;
    xbool PreviousLeftX = FALSE;
    xbool PreviousLeftY = FALSE;
    xbool PreviousRightA = FALSE;
    xbool PreviousRightB = FALSE;
    xbool PreviousLeftStickClick = FALSE;
    xbool PreviousRightStickClick = FALSE;
    xbool PreviousLeftMenu = FALSE;
    f32 PreviousLeftTrigger = 0.0f;
    f32 PreviousRightTrigger = 0.0f;
    f32 PreviousLeftStick[2] = { 0.0f, 0.0f };
    f32 PreviousRightStick[2] = { 0.0f, 0.0f };
    xbool UseQuadLayer = FALSE;
    /* Same tracking-origin model as the working Simpsons OpenXR path:
     * capture a yaw-only centre between the two eyes, then compose every
     * render eye from its pose relative to that stable origin. */
    xbool TrackingOriginValid = FALSE;
    XrPosef TrackingOrigin{
        { 0.0f, 0.0f, 0.0f, 1.0f },
        { 0.0f, 0.0f, 0.0f } };
    XrFrameState PreparedFrameState{ XR_TYPE_FRAME_STATE };
    xbool FrameBegun = FALSE;
    xbool ExternalVulkan = FALSE;
    xbool MultiviewEnabled = FALSE;
    xbool MutableSrgbImages = FALSE;
    xbool ArraySwapchain = FALSE;

    PFN_xrGetVulkanGraphicsRequirements2KHR GetVulkanGraphicsRequirements = NULL;
    PFN_xrGetVulkanGraphicsDevice2KHR GetVulkanGraphicsDevice = NULL;
    PFN_xrCreateVulkanInstanceKHR CreateVulkanInstance = NULL;
    PFN_xrCreateVulkanDeviceKHR CreateVulkanDevice = NULL;
    xbool SessionRunning = FALSE;
};

VulkanSession::VulkanSession()
    : m_pImpl ( NULL )
    , m_State ( session_state::Uninitialized )
    , m_ExternalDevice{}
    , m_bUseExternalDevice ( FALSE )
{
    m_LastError[0] = '\0';
}

VulkanSession::~VulkanSession()
{
    Shutdown();
}

xbool VulkanSession::Initialize( Runtime& RuntimeObject )
{
    m_bUseExternalDevice = FALSE;
    return InitializeInternal( RuntimeObject );
}

xbool VulkanSession::Initialize( Runtime& RuntimeObject,
                                 const vulkan_device_info& DeviceInfo )
{
    m_ExternalDevice = DeviceInfo;
    m_bUseExternalDevice = TRUE;
    return InitializeInternal( RuntimeObject );
}

xbool VulkanSession::InitializeInternal( Runtime& RuntimeObject )
{
    const xbool UseExternalDevice = m_bUseExternalDevice;
    Shutdown();
    m_LastError[0] = '\0';

    if( RuntimeObject.GetState() != runtime_state::SystemReady )
    {
        SetError( m_LastError, sizeof(m_LastError),
                  "OpenXR runtime is not ready for a graphics session" );
        m_State = session_state::Failed;
        return FALSE;
    }

    m_pImpl = new Impl;
    m_pImpl->ExternalVulkan = UseExternalDevice;
    m_pImpl->Instance = reinterpret_cast<XrInstance>(
        RuntimeObject.GetInstanceHandle() );
    m_pImpl->SystemId = static_cast<XrSystemId>( RuntimeObject.GetSystemId() );

    if( !LoadXrFunction( m_pImpl->Instance,
                         "xrGetVulkanGraphicsRequirements2KHR",
                         m_pImpl->GetVulkanGraphicsRequirements,
                         m_LastError, sizeof(m_LastError) ) ||
        !LoadXrFunction( m_pImpl->Instance,
                         "xrGetVulkanGraphicsDevice2KHR",
                         m_pImpl->GetVulkanGraphicsDevice,
                         m_LastError, sizeof(m_LastError) ) ||
        !LoadXrFunction( m_pImpl->Instance, "xrCreateVulkanInstanceKHR",
                         m_pImpl->CreateVulkanInstance,
                         m_LastError, sizeof(m_LastError) ) ||
        !LoadXrFunction( m_pImpl->Instance, "xrCreateVulkanDeviceKHR",
                         m_pImpl->CreateVulkanDevice,
                         m_LastError, sizeof(m_LastError) ) )
    {
        Shutdown();
        m_State = session_state::Failed;
        return FALSE;
    }

    XrGraphicsRequirementsVulkan2KHR Requirements{
        XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR };
    if( !CheckXr( m_pImpl->GetVulkanGraphicsRequirements(
                      m_pImpl->Instance, m_pImpl->SystemId, &Requirements ),
                  "xrGetVulkanGraphicsRequirements2KHR", m_LastError,
                  sizeof(m_LastError) ) )
    {
        Shutdown();
        m_State = session_state::Failed;
        return FALSE;
    }

    if( m_pImpl->ExternalVulkan )
    {
        m_pImpl->VkInstanceHandle = reinterpret_cast<VkInstance>(
            m_ExternalDevice.Instance );
        m_pImpl->PhysicalDevice = reinterpret_cast<VkPhysicalDevice>(
            m_ExternalDevice.PhysicalDevice );
        m_pImpl->Device = reinterpret_cast<VkDevice>( m_ExternalDevice.Device );
        m_pImpl->Queue = reinterpret_cast<VkQueue>( m_ExternalDevice.Queue );
        m_pImpl->QueueFamilyIndex = m_ExternalDevice.QueueFamilyIndex;
        m_pImpl->MultiviewEnabled = m_ExternalDevice.MultiviewEnabled;

        XrVulkanGraphicsDeviceGetInfoKHR ExternalDeviceInfo{
            XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR };
        ExternalDeviceInfo.systemId = m_pImpl->SystemId;
        ExternalDeviceInfo.vulkanInstance = m_pImpl->VkInstanceHandle;
        VkPhysicalDevice RuntimePhysicalDevice = VK_NULL_HANDLE;
        if( !CheckXr( m_pImpl->GetVulkanGraphicsDevice(
                          m_pImpl->Instance, &ExternalDeviceInfo,
                          &RuntimePhysicalDevice ),
                      "xrGetVulkanGraphicsDevice2KHR", m_LastError,
                      sizeof(m_LastError) ) ||
            (RuntimePhysicalDevice != m_pImpl->PhysicalDevice) )
        {
            SetError( m_LastError, sizeof(m_LastError),
                      "SDL Vulkan device does not match the OpenXR graphics device" );
            Shutdown();
            m_State = session_state::Failed;
            return FALSE;
        }
    }
    else
    {
    VkApplicationInfo ApplicationInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
    ApplicationInfo.pApplicationName = "Area 51";
    ApplicationInfo.applicationVersion = 1;
    ApplicationInfo.pEngineName = "Entropy";
    ApplicationInfo.engineVersion = 1;
    ApplicationInfo.apiVersion =
        (Requirements.maxApiVersionSupported >= VK_API_VERSION_1_1)
            ? VK_API_VERSION_1_1
            : ( Requirements.minApiVersionSupported ? Requirements.minApiVersionSupported
                                                    : VK_API_VERSION_1_0 );

    VkInstanceCreateInfo InstanceCreateInfo{
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    InstanceCreateInfo.pApplicationInfo = &ApplicationInfo;

    XrVulkanInstanceCreateInfoKHR XrInstanceCreateInfo{
        XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR };
    XrInstanceCreateInfo.systemId = m_pImpl->SystemId;
    XrInstanceCreateInfo.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
    XrInstanceCreateInfo.vulkanCreateInfo = &InstanceCreateInfo;

    VkResult VulkanResult = VK_ERROR_INITIALIZATION_FAILED;
    if( !CheckXr( m_pImpl->CreateVulkanInstance(
                      m_pImpl->Instance, &XrInstanceCreateInfo,
                      &m_pImpl->VkInstanceHandle, &VulkanResult ),
                  "xrCreateVulkanInstanceKHR", m_LastError,
                  sizeof(m_LastError) ) ||
        !CheckVk( VulkanResult, "instance creation", m_LastError,
                  sizeof(m_LastError) ) )
    {
        Shutdown();
        m_State = session_state::Failed;
        return FALSE;
    }

    XrVulkanGraphicsDeviceGetInfoKHR DeviceInfo{
        XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR };
    DeviceInfo.systemId = m_pImpl->SystemId;
    DeviceInfo.vulkanInstance = m_pImpl->VkInstanceHandle;
    if( !CheckXr( m_pImpl->GetVulkanGraphicsDevice(
                      m_pImpl->Instance, &DeviceInfo,
                      &m_pImpl->PhysicalDevice ),
                  "xrGetVulkanGraphicsDevice2KHR", m_LastError,
                  sizeof(m_LastError) ) )
    {
        Shutdown();
        m_State = session_state::Failed;
        return FALSE;
    }

    u32 QueueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties( m_pImpl->PhysicalDevice,
                                               &QueueFamilyCount, NULL );
    if( QueueFamilyCount == 0 )
    {
        SetError( m_LastError, sizeof(m_LastError),
                  "Vulkan physical device has no queue families" );
        Shutdown();
        m_State = session_state::Failed;
        return FALSE;
    }

    std::vector<VkQueueFamilyProperties> QueueFamilies( QueueFamilyCount );
    vkGetPhysicalDeviceQueueFamilyProperties( m_pImpl->PhysicalDevice,
                                               &QueueFamilyCount,
                                               QueueFamilies.data() );
    for( u32 i = 0; i < QueueFamilyCount; ++i )
    {
        if( QueueFamilies[i].queueCount &&
            (QueueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) )
        {
            m_pImpl->QueueFamilyIndex = i;
            break;
        }
    }

    if( !QueueFamilies[m_pImpl->QueueFamilyIndex].queueCount ||
        !(QueueFamilies[m_pImpl->QueueFamilyIndex].queueFlags &
          VK_QUEUE_GRAPHICS_BIT) )
    {
        SetError( m_LastError, sizeof(m_LastError),
                  "Vulkan physical device has no graphics queue" );
        Shutdown();
        m_State = session_state::Failed;
        return FALSE;
    }

    const f32 QueuePriority = 1.0f;
    VkDeviceQueueCreateInfo QueueCreateInfo{
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    QueueCreateInfo.queueFamilyIndex = m_pImpl->QueueFamilyIndex;
    QueueCreateInfo.queueCount = 1;
    QueueCreateInfo.pQueuePriorities = &QueuePriority;

    VkPhysicalDeviceMultiviewFeatures SupportedMultiview{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES };
    VkPhysicalDeviceFeatures2 SupportedFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    SupportedFeatures.pNext = &SupportedMultiview;

    VkPhysicalDeviceMultiviewProperties MultiviewProperties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES };
    VkPhysicalDeviceProperties2 DeviceProperties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    DeviceProperties.pNext = &MultiviewProperties;

    PFN_vkGetPhysicalDeviceFeatures2 GetPhysicalDeviceFeatures2 =
        reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
            vkGetInstanceProcAddr( m_pImpl->VkInstanceHandle, "vkGetPhysicalDeviceFeatures2" ) );
    PFN_vkGetPhysicalDeviceProperties2 GetPhysicalDeviceProperties2 =
        reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
            vkGetInstanceProcAddr( m_pImpl->VkInstanceHandle, "vkGetPhysicalDeviceProperties2" ) );
    if( GetPhysicalDeviceFeatures2 && GetPhysicalDeviceProperties2 )
    {
        GetPhysicalDeviceFeatures2( m_pImpl->PhysicalDevice, &SupportedFeatures );
        GetPhysicalDeviceProperties2( m_pImpl->PhysicalDevice, &DeviceProperties );
    }

    VkPhysicalDeviceMultiviewFeatures EnabledMultiview{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES };
    EnabledMultiview.multiview =
        (GetPhysicalDeviceFeatures2 && GetPhysicalDeviceProperties2 &&
         SupportedMultiview.multiview &&
         (MultiviewProperties.maxMultiviewViewCount >= 2)) ? VK_TRUE : VK_FALSE;

    VkDeviceCreateInfo DeviceCreateInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    DeviceCreateInfo.queueCreateInfoCount = 1;
    DeviceCreateInfo.pQueueCreateInfos = &QueueCreateInfo;
    DeviceCreateInfo.pNext = EnabledMultiview.multiview ? &EnabledMultiview : NULL;
    m_pImpl->MultiviewEnabled = EnabledMultiview.multiview ? TRUE : FALSE;

    XrVulkanDeviceCreateInfoKHR XrDeviceCreateInfo{
        XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR };
    XrDeviceCreateInfo.systemId = m_pImpl->SystemId;
    XrDeviceCreateInfo.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
    // XR_KHR_vulkan_enable2 requires the physical device selected by
    // xrGetVulkanGraphicsDevice2KHR to be passed back when the runtime
    // creates the logical device. Without this handle the Quest runtime
    // rejects xrCreateVulkanDeviceKHR with XR_ERROR_HANDLE_INVALID.
    XrDeviceCreateInfo.vulkanPhysicalDevice = m_pImpl->PhysicalDevice;
    XrDeviceCreateInfo.vulkanCreateInfo = &DeviceCreateInfo;

    VulkanResult = VK_ERROR_INITIALIZATION_FAILED;
    if( !CheckXr( m_pImpl->CreateVulkanDevice(
                      m_pImpl->Instance, &XrDeviceCreateInfo,
                      &m_pImpl->Device, &VulkanResult ),
                  "xrCreateVulkanDeviceKHR", m_LastError,
                  sizeof(m_LastError) ) ||
        !CheckVk( VulkanResult, "device creation", m_LastError,
                  sizeof(m_LastError) ) )
    {
        Shutdown();
        m_State = session_state::Failed;
        return FALSE;
    }
    vkGetDeviceQueue( m_pImpl->Device, m_pImpl->QueueFamilyIndex, 0,
                      &m_pImpl->Queue );
    }

    XrGraphicsBindingVulkan2KHR GraphicsBinding{
        XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR };
    GraphicsBinding.instance = m_pImpl->VkInstanceHandle;
    GraphicsBinding.physicalDevice = m_pImpl->PhysicalDevice;
    GraphicsBinding.device = m_pImpl->Device;
    GraphicsBinding.queueFamilyIndex = m_pImpl->QueueFamilyIndex;
    GraphicsBinding.queueIndex = 0;

    XrSessionCreateInfo SessionCreateInfo{ XR_TYPE_SESSION_CREATE_INFO };
    SessionCreateInfo.next = &GraphicsBinding;
    SessionCreateInfo.systemId = m_pImpl->SystemId;
    if( !CheckXr( xrCreateSession( m_pImpl->Instance, &SessionCreateInfo,
                                   &m_pImpl->Session ),
                  "xrCreateSession", m_LastError, sizeof(m_LastError) ) )
    {
        Shutdown();
        m_State = session_state::Failed;
        return FALSE;
    }

#if defined(TARGET_ANDROID)
    // Quest exposes app/compositor timing, utilization, frequencies and other
    // counters through META_performance_metrics. Probe each entry point so an
    // unsupported runtime continues without changing session startup.
    PFN_xrVoidFunction PerfFunction = NULL;
    if( XR_SUCCEEDED( xrGetInstanceProcAddr( m_pImpl->Instance,
            "xrEnumeratePerformanceMetricsCounterPathsMETA", &PerfFunction ) ) && PerfFunction )
        m_pImpl->EnumeratePerformanceCounters = reinterpret_cast<PFN_xrEnumeratePerformanceMetricsCounterPathsMETA>( PerfFunction );
    PerfFunction = NULL;
    if( XR_SUCCEEDED( xrGetInstanceProcAddr( m_pImpl->Instance,
            "xrSetPerformanceMetricsStateMETA", &PerfFunction ) ) && PerfFunction )
        m_pImpl->SetPerformanceMetricsState = reinterpret_cast<PFN_xrSetPerformanceMetricsStateMETA>( PerfFunction );
    PerfFunction = NULL;
    if( XR_SUCCEEDED( xrGetInstanceProcAddr( m_pImpl->Instance,
            "xrQueryPerformanceMetricsCounterMETA", &PerfFunction ) ) && PerfFunction )
        m_pImpl->QueryPerformanceCounter = reinterpret_cast<PFN_xrQueryPerformanceMetricsCounterMETA>( PerfFunction );

    if( m_pImpl->EnumeratePerformanceCounters && m_pImpl->SetPerformanceMetricsState &&
        m_pImpl->QueryPerformanceCounter )
    {
        XrPerformanceMetricsStateMETA MetricsState{ XR_TYPE_PERFORMANCE_METRICS_STATE_META };
        MetricsState.enabled = XR_TRUE;
        if( XR_SUCCEEDED( m_pImpl->SetPerformanceMetricsState( m_pImpl->Session, &MetricsState ) ) )
        {
            uint32_t CounterCount = 0;
            if( XR_SUCCEEDED( m_pImpl->EnumeratePerformanceCounters( m_pImpl->Instance, 0, &CounterCount, NULL ) ) && CounterCount )
            {
                std::vector<XrPath> Paths( CounterCount );
                if( XR_SUCCEEDED( m_pImpl->EnumeratePerformanceCounters( m_pImpl->Instance, CounterCount, &CounterCount, Paths.data() ) ) )
                {
                    m_pImpl->PerformanceCounters.reserve( CounterCount );
                    for( XrPath Path : Paths )
                    {
                        Impl::PerformanceCounter Counter{};
                        Counter.Path = Path;
                        uint32_t NameLength = 0;
                        if( XR_SUCCEEDED( xrPathToString( m_pImpl->Instance, Path, sizeof(Counter.Name), &NameLength, Counter.Name ) ) )
                            m_pImpl->PerformanceCounters.push_back( Counter );
                    }
                }
            }
            __android_log_print( ANDROID_LOG_INFO, "A51Perf", "runtime metrics enabled counters=%u", (unsigned)m_pImpl->PerformanceCounters.size() );
        }
    }
    else
        __android_log_print( ANDROID_LOG_INFO, "A51Perf", "runtime metrics extension unavailable" );

    // Resolve the optional Quest extension. Do not gate the 72 Hz request on
    // xrEnumerateDisplayRefreshRatesFB: Meta documents that list as deprecated
    // and permits requests for supported rates it does not enumerate.
    PFN_xrVoidFunction RefreshRateFunction = NULL;
    if( XR_SUCCEEDED( xrGetInstanceProcAddr(
                         m_pImpl->Instance, "xrRequestDisplayRefreshRateFB",
                         &RefreshRateFunction ) ) && RefreshRateFunction )
    {
        m_pImpl->RequestDisplayRefreshRate = reinterpret_cast<
            PFN_xrRequestDisplayRefreshRateFB>( RefreshRateFunction );
    }

    RefreshRateFunction = NULL;
    if( XR_SUCCEEDED( xrGetInstanceProcAddr(
                         m_pImpl->Instance, "xrGetDisplayRefreshRateFB",
                         &RefreshRateFunction ) ) && RefreshRateFunction )
    {
        m_pImpl->GetDisplayRefreshRate = reinterpret_cast<
            PFN_xrGetDisplayRefreshRateFB>( RefreshRateFunction );
    }
#endif

    /*
     * OpenXR input is deliberately translated into the engine's existing
     * Xbox-style gadgets.  This keeps the gameplay/UI mapping shared with
     * desktop gamepads and avoids adding a second input API to the game.
     * Controller input is optional: a runtime that exposes no Touch profile
     * must not prevent the render session from starting.
     */
    {
        XrActionSetCreateInfo ActionSetInfo{
            XR_TYPE_ACTION_SET_CREATE_INFO };
        std::snprintf( ActionSetInfo.actionSetName,
                       XR_MAX_ACTION_SET_NAME_SIZE, "area51_controls" );
        std::snprintf( ActionSetInfo.localizedActionSetName,
                       XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE,
                       "Area 51 Controls" );
        ActionSetInfo.priority = 0;

        xbool InputSetupOk =
            XR_SUCCEEDED( xrCreateActionSet( m_pImpl->Instance,
                                              &ActionSetInfo,
                                              &m_pImpl->InputActionSet ) );

        auto MakeAction = [&]( XrActionType Type,
                               const char* pName,
                               const char* pLocalized,
                               XrAction& Action ) -> xbool
        {
            if( !InputSetupOk )
                return FALSE;

            XrActionCreateInfo ActionInfo{ XR_TYPE_ACTION_CREATE_INFO };
            ActionInfo.actionType = Type;
            std::snprintf( ActionInfo.actionName,
                           XR_MAX_ACTION_NAME_SIZE, "%s", pName );
            std::snprintf( ActionInfo.localizedActionName,
                           XR_MAX_LOCALIZED_ACTION_NAME_SIZE,
                           "%s", pLocalized );
            return XR_SUCCEEDED( xrCreateAction( m_pImpl->InputActionSet,
                                                  &ActionInfo, &Action ) );
        };

        InputSetupOk = MakeAction( XR_ACTION_TYPE_VECTOR2F_INPUT,
                                   "left_stick", "Left Stick",
                                   m_pImpl->LeftStickAction ) && InputSetupOk;
        InputSetupOk = MakeAction( XR_ACTION_TYPE_VECTOR2F_INPUT,
                                   "right_stick", "Right Stick",
                                   m_pImpl->RightStickAction ) && InputSetupOk;
        InputSetupOk = MakeAction( XR_ACTION_TYPE_FLOAT_INPUT,
                                   "left_trigger", "Left Trigger",
                                   m_pImpl->LeftTriggerAction ) && InputSetupOk;
        InputSetupOk = MakeAction( XR_ACTION_TYPE_FLOAT_INPUT,
                                   "right_trigger", "Right Trigger",
                                   m_pImpl->RightTriggerAction ) && InputSetupOk;
        InputSetupOk = MakeAction( XR_ACTION_TYPE_BOOLEAN_INPUT,
                                   "left_x", "Left X",
                                   m_pImpl->LeftXAction ) && InputSetupOk;
        InputSetupOk = MakeAction( XR_ACTION_TYPE_BOOLEAN_INPUT,
                                   "left_y", "Left Y",
                                   m_pImpl->LeftYAction ) && InputSetupOk;
        InputSetupOk = MakeAction( XR_ACTION_TYPE_BOOLEAN_INPUT,
                                   "right_a", "Right A",
                                   m_pImpl->RightAAction ) && InputSetupOk;
        InputSetupOk = MakeAction( XR_ACTION_TYPE_BOOLEAN_INPUT,
                                   "right_b", "Right B",
                                   m_pImpl->RightBAction ) && InputSetupOk;
        InputSetupOk = MakeAction( XR_ACTION_TYPE_BOOLEAN_INPUT,
                                   "left_stick_click", "Left Stick Click",
                                   m_pImpl->LeftStickClickAction ) && InputSetupOk;
        InputSetupOk = MakeAction( XR_ACTION_TYPE_BOOLEAN_INPUT,
                                   "right_stick_click", "Right Stick Click",
                                   m_pImpl->RightStickClickAction ) && InputSetupOk;
        InputSetupOk = MakeAction( XR_ACTION_TYPE_BOOLEAN_INPUT,
                                   "left_menu", "Left Menu",
                                   m_pImpl->LeftMenuAction ) && InputSetupOk;

        XrPath LeftHand = XR_NULL_PATH;
        XrPath RightHand = XR_NULL_PATH;
        const char* const HandPaths[] = {
            "/user/hand/left",
            "/user/hand/right"
        };
        InputSetupOk = InputSetupOk &&
            XR_SUCCEEDED( xrStringToPath( m_pImpl->Instance,
                                           HandPaths[0], &LeftHand ) ) &&
            XR_SUCCEEDED( xrStringToPath( m_pImpl->Instance,
                                           HandPaths[1], &RightHand ) );

        std::vector<XrActionSuggestedBinding> Bindings;
        auto AddBinding = [&]( XrAction Action, const char* pPath ) -> xbool
        {
            XrPath Path = XR_NULL_PATH;
            if( !XR_SUCCEEDED( xrStringToPath( m_pImpl->Instance,
                                               pPath, &Path ) ) )
            {
                return FALSE;
            }
            XrActionSuggestedBinding Binding{};
            Binding.action = Action;
            Binding.binding = Path;
            Bindings.push_back( Binding );
            return TRUE;
        };

        InputSetupOk = InputSetupOk &&
            AddBinding( m_pImpl->LeftStickAction,
                        "/user/hand/left/input/thumbstick" ) &&
            AddBinding( m_pImpl->RightStickAction,
                        "/user/hand/right/input/thumbstick" ) &&
            AddBinding( m_pImpl->LeftTriggerAction,
                        "/user/hand/left/input/trigger/value" ) &&
            AddBinding( m_pImpl->RightTriggerAction,
                        "/user/hand/right/input/trigger/value" ) &&
            AddBinding( m_pImpl->LeftXAction,
                        "/user/hand/left/input/x/click" ) &&
            AddBinding( m_pImpl->LeftYAction,
                        "/user/hand/left/input/y/click" ) &&
            AddBinding( m_pImpl->RightAAction,
                        "/user/hand/right/input/a/click" ) &&
            AddBinding( m_pImpl->RightBAction,
                        "/user/hand/right/input/b/click" ) &&
            AddBinding( m_pImpl->LeftStickClickAction,
                        "/user/hand/left/input/thumbstick/click" ) &&
            AddBinding( m_pImpl->RightStickClickAction,
                        "/user/hand/right/input/thumbstick/click" ) &&
            AddBinding( m_pImpl->LeftMenuAction,
                        "/user/hand/left/input/menu/click" );

        auto SuggestForProfile = [&]( const char* pProfile ) -> xbool
        {
            XrPath ProfilePath = XR_NULL_PATH;
            if( !XR_SUCCEEDED( xrStringToPath( m_pImpl->Instance,
                                               pProfile, &ProfilePath ) ) )
            {
                return FALSE;
            }

            XrInteractionProfileSuggestedBinding Suggested{
                XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
            Suggested.interactionProfile = ProfilePath;
            Suggested.suggestedBindings = Bindings.data();
            Suggested.countSuggestedBindings =
                static_cast<u32>( Bindings.size() );
            return XR_SUCCEEDED( xrSuggestInteractionProfileBindings(
                m_pImpl->Instance, &Suggested ) );
        };

        xbool const SuggestedLegacy =
            InputSetupOk && SuggestForProfile(
                "/interaction_profiles/oculus/touch_controller" );
        xbool const SuggestedMeta =
            InputSetupOk && SuggestForProfile(
                "/interaction_profiles/meta/touch_controller_plus" );

        if( InputSetupOk && (SuggestedLegacy || SuggestedMeta) )
        {
            XrSessionActionSetsAttachInfo AttachInfo{
                XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
            AttachInfo.countActionSets = 1;
            AttachInfo.actionSets = &m_pImpl->InputActionSet;
            InputSetupOk = XR_SUCCEEDED( xrAttachSessionActionSets(
                m_pImpl->Session, &AttachInfo ) );
        }

        m_pImpl->InputActionsReady = InputSetupOk;
    }

    XrReferenceSpaceCreateInfo SpaceCreateInfo{
        XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    SpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    SpaceCreateInfo.poseInReferenceSpace.orientation.w = 1.0f;
    if( !CheckXr( xrCreateReferenceSpace( m_pImpl->Session, &SpaceCreateInfo,
                                          &m_pImpl->Space ),
                  "xrCreateReferenceSpace", m_LastError,
                  sizeof(m_LastError) ) )
    {
        Shutdown();
        m_State = session_state::Failed;
        return FALSE;
    }

    u32 ViewCount = 0;
    if( !CheckXr( xrEnumerateViewConfigurationViews(
                      m_pImpl->Instance, m_pImpl->SystemId,
                      m_pImpl->ViewConfigurationType, 0, &ViewCount, NULL ),
                  "xrEnumerateViewConfigurationViews", m_LastError,
                  sizeof(m_LastError) ) || ViewCount == 0 )
    {
        SetError( m_LastError, sizeof(m_LastError),
                  "OpenXR returned no stereo views" );
        Shutdown();
        m_State = session_state::Failed;
        return FALSE;
    }

    m_pImpl->ViewConfigurationViews.resize( ViewCount );
    for( XrViewConfigurationView& View : m_pImpl->ViewConfigurationViews )
        View = { XR_TYPE_VIEW_CONFIGURATION_VIEW };
    if( !CheckXr( xrEnumerateViewConfigurationViews(
                      m_pImpl->Instance, m_pImpl->SystemId,
                      m_pImpl->ViewConfigurationType, ViewCount, &ViewCount,
                      m_pImpl->ViewConfigurationViews.data() ),
                  "xrEnumerateViewConfigurationViews", m_LastError,
                  sizeof(m_LastError) ) )
    {
        Shutdown();
        m_State = session_state::Failed;
        return FALSE;
    }
    m_pImpl->Views.resize( ViewCount );
    for( XrView& View : m_pImpl->Views )
        View = { XR_TYPE_VIEW };

    u32 FormatCount = 0;
    if( !CheckXr( xrEnumerateSwapchainFormats( m_pImpl->Session, 0,
                                               &FormatCount, NULL ),
                  "xrEnumerateSwapchainFormats", m_LastError,
                  sizeof(m_LastError) ) || FormatCount == 0 )
    {
        Shutdown();
        m_State = session_state::Failed;
        return FALSE;
    }
    std::vector<int64_t> Formats( FormatCount );
    if( !CheckXr( xrEnumerateSwapchainFormats( m_pImpl->Session, FormatCount,
                                               &FormatCount, Formats.data() ),
                  "xrEnumerateSwapchainFormats", m_LastError,
                  sizeof(m_LastError) ) )
    {
        Shutdown();
        m_State = session_state::Failed;
        return FALSE;
    }
    const VkFormat ColorFormat = SelectColorFormat( Formats );
    if( ColorFormat == VK_FORMAT_UNDEFINED )
    {
        SetError( m_LastError, sizeof(m_LastError),
                  "OpenXR runtime exposes no supported RGBA swapchain format" );
        Shutdown();
        m_State = session_state::Failed;
        return FALSE;
    }

    XrSwapchain ArrayHandle = XR_NULL_HANDLE;
    const xbool bCanUseArray = m_pImpl->MultiviewEnabled &&
        (ViewCount == 2) &&
        (m_pImpl->ViewConfigurationViews[0].recommendedImageRectWidth ==
         m_pImpl->ViewConfigurationViews[1].recommendedImageRectWidth) &&
        (m_pImpl->ViewConfigurationViews[0].recommendedImageRectHeight ==
         m_pImpl->ViewConfigurationViews[1].recommendedImageRectHeight);
    if( !bCanUseArray )
    {
        SetError( m_LastError, sizeof(m_LastError),
                  "direct OpenXR rendering requires a two-view multiview array swapchain" );
        Shutdown();
        m_State = session_state::Failed;
        return FALSE;
    }
    if( bCanUseArray )
    {
        XrSwapchainCreateInfo ArrayInfo{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
        ArrayInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                               XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT;
        ArrayInfo.format = static_cast<int64_t>( ColorFormat );
        ArrayInfo.sampleCount = 1;
        ArrayInfo.width = m_pImpl->ViewConfigurationViews[0].recommendedImageRectWidth;
        ArrayInfo.height = m_pImpl->ViewConfigurationViews[0].recommendedImageRectHeight;
        ArrayInfo.faceCount = 1;
        ArrayInfo.arraySize = ViewCount;
        ArrayInfo.mipCount = 1;
        const XrResult ArrayResult = xrCreateSwapchain(
            m_pImpl->Session, &ArrayInfo, &ArrayHandle );
        m_pImpl->ArraySwapchain = XR_SUCCEEDED( ArrayResult );
        x_DebugMsg( "OpenXR direct multiview swapchain: %s (result=%d)\n",
                    m_pImpl->ArraySwapchain ? "enabled" : "unavailable",
                    static_cast<int>( ArrayResult ) );
        if( !m_pImpl->ArraySwapchain )
        {
            SetError( m_LastError, sizeof(m_LastError),
                      "OpenXR rejected the mutable two-layer swapchain (%d)",
                      static_cast<int>( ArrayResult ) );
            Shutdown();
            m_State = session_state::Failed;
            return FALSE;
        }
    }

    m_pImpl->Swapchains.resize( m_pImpl->ArraySwapchain ? 1u : ViewCount );
    if( m_pImpl->ArraySwapchain )
        m_pImpl->Swapchains[0].Handle = ArrayHandle;
    for( u32 ViewIndex = 0; ViewIndex < m_pImpl->Swapchains.size(); ++ViewIndex )
    {
        Impl::Swapchain& Swapchain = m_pImpl->Swapchains[ViewIndex];
        const XrViewConfigurationView& View =
            m_pImpl->ViewConfigurationViews[ViewIndex];
        Swapchain.Width = View.recommendedImageRectWidth;
        Swapchain.Height = View.recommendedImageRectHeight;
        Swapchain.Format = ColorFormat;

        XrSwapchainCreateInfo SwapchainCreateInfo{
            XR_TYPE_SWAPCHAIN_CREATE_INFO };
        /* Prefer a mutable sRGB image so SDL can render display-ready game
         * output through its compatible UNORM color attachment view. */
        SwapchainCreateInfo.usageFlags =
            XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
            XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT;
        SwapchainCreateInfo.format = static_cast<int64_t>( ColorFormat );
        SwapchainCreateInfo.sampleCount = 1;
        SwapchainCreateInfo.width = Swapchain.Width;
        SwapchainCreateInfo.height = Swapchain.Height;
        SwapchainCreateInfo.faceCount = 1;
        SwapchainCreateInfo.arraySize = m_pImpl->ArraySwapchain ? ViewCount : 1;
        SwapchainCreateInfo.mipCount = 1;
        XrResult SwapchainResult = m_pImpl->ArraySwapchain ? XR_SUCCESS :
            xrCreateSwapchain( m_pImpl->Session, &SwapchainCreateInfo,
                               &Swapchain.Handle );
        if( XR_SUCCEEDED( SwapchainResult ) )
        {
            m_pImpl->MutableSrgbImages =
                (ColorFormat == VK_FORMAT_R8G8B8A8_SRGB) ||
                (ColorFormat == VK_FORMAT_B8G8R8A8_SRGB);
        }
        else
        {
            /* A runtime may expose sRGB but reject mutable usage. This keeps
             * session setup viable; frames are skipped unless direct UNORM
             * rendering is supported. */
            SwapchainCreateInfo.usageFlags &= ~XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT;
            SwapchainResult = xrCreateSwapchain(
                m_pImpl->Session, &SwapchainCreateInfo, &Swapchain.Handle );
            if( !CheckXr( SwapchainResult, "xrCreateSwapchain",
                          m_LastError, sizeof(m_LastError) ) )
            {
                Shutdown();
                m_State = session_state::Failed;
                return FALSE;
            }
            m_pImpl->MutableSrgbImages = FALSE;
        }

        u32 ImageCount = 0;
        if( !CheckXr( xrEnumerateSwapchainImages( Swapchain.Handle, 0,
                                                  &ImageCount, NULL ),
                      "xrEnumerateSwapchainImages", m_LastError,
                      sizeof(m_LastError) ) || ImageCount == 0 )
        {
            Shutdown();
            m_State = session_state::Failed;
            return FALSE;
        }
        Swapchain.Images.resize( ImageCount );
        for( XrSwapchainImageVulkan2KHR& Image : Swapchain.Images )
            Image = { XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR };
        if( !CheckXr( xrEnumerateSwapchainImages(
                          Swapchain.Handle, ImageCount, &ImageCount,
                          reinterpret_cast<XrSwapchainImageBaseHeader*>(
                              Swapchain.Images.data()) ),
                      "xrEnumerateSwapchainImages", m_LastError,
                      sizeof(m_LastError) ) )
        {
            Shutdown();
            m_State = session_state::Failed;
            return FALSE;
        }

                for( u32 ImageIndex = 0; ImageIndex < ImageCount; ++ImageIndex )
        {
                    }

        Swapchain.ImageViews.resize( ImageCount, VK_NULL_HANDLE );
        Swapchain.Framebuffers.resize( ImageCount, VK_NULL_HANDLE );
        for( u32 ImageIndex = 0; ImageIndex < ImageCount; ++ImageIndex )
        {
            VkImageViewCreateInfo ImageViewCreateInfo{
                VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            ImageViewCreateInfo.image = Swapchain.Images[ImageIndex].image;
            ImageViewCreateInfo.viewType = m_pImpl->ArraySwapchain
                ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
            ImageViewCreateInfo.format = ColorFormat;
            ImageViewCreateInfo.subresourceRange.aspectMask =
                VK_IMAGE_ASPECT_COLOR_BIT;
            ImageViewCreateInfo.subresourceRange.levelCount = 1;
            ImageViewCreateInfo.subresourceRange.layerCount =
                m_pImpl->ArraySwapchain ? ViewCount : 1;
            if( !CheckVk( vkCreateImageView( m_pImpl->Device,
                                              &ImageViewCreateInfo, NULL,
                                              &Swapchain.ImageViews[ImageIndex] ),
                           "image view creation", m_LastError,
                           sizeof(m_LastError) ) )
            {
                Shutdown();
                m_State = session_state::Failed;
                return FALSE;
            }
        }
    }

    VkAttachmentDescription Attachment{};
    Attachment.format = ColorFormat;
    Attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    Attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    Attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    Attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    Attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    Attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    Attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference AttachmentReference{};
    AttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription Subpass{};
    Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    Subpass.colorAttachmentCount = 1;
    Subpass.pColorAttachments = &AttachmentReference;

    VkRenderPassCreateInfo RenderPassCreateInfo{
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    RenderPassCreateInfo.attachmentCount = 1;
    RenderPassCreateInfo.pAttachments = &Attachment;
    RenderPassCreateInfo.subpassCount = 1;
    RenderPassCreateInfo.pSubpasses = &Subpass;
    if( !CheckVk( vkCreateRenderPass( m_pImpl->Device, &RenderPassCreateInfo,
                                      NULL, &m_pImpl->RenderPass ),
                  "render pass creation", m_LastError,
                  sizeof(m_LastError) ) )
    {
        Shutdown();
        m_State = session_state::Failed;
        return FALSE;
    }

    for( Impl::Swapchain& Swapchain : m_pImpl->Swapchains )
    {
        for( size_t ImageIndex = 0; ImageIndex < Swapchain.ImageViews.size();
             ++ImageIndex )
        {
            VkFramebufferCreateInfo FramebufferCreateInfo{
                VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
            FramebufferCreateInfo.renderPass = m_pImpl->RenderPass;
            FramebufferCreateInfo.attachmentCount = 1;
            FramebufferCreateInfo.pAttachments =
                &Swapchain.ImageViews[ImageIndex];
            FramebufferCreateInfo.width = Swapchain.Width;
            FramebufferCreateInfo.height = Swapchain.Height;
            FramebufferCreateInfo.layers = m_pImpl->ArraySwapchain
                                         ? ViewCount : 1;
            if( !CheckVk( vkCreateFramebuffer( m_pImpl->Device,
                                                &FramebufferCreateInfo, NULL,
                                                &Swapchain.Framebuffers[ImageIndex] ),
                           "framebuffer creation", m_LastError,
                           sizeof(m_LastError) ) )
            {
                Shutdown();
                m_State = session_state::Failed;
                return FALSE;
            }
        }
    }

    VkCommandPoolCreateInfo CommandPoolCreateInfo{
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    CommandPoolCreateInfo.queueFamilyIndex = m_pImpl->QueueFamilyIndex;
    CommandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if( !CheckVk( vkCreateCommandPool( m_pImpl->Device, &CommandPoolCreateInfo,
                                       NULL, &m_pImpl->CommandPool ),
                  "command pool creation", m_LastError,
                  sizeof(m_LastError) ) )
    {
        Shutdown();
        m_State = session_state::Failed;
        return FALSE;
    }

    VkCommandBufferAllocateInfo CommandBufferAllocateInfo{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    CommandBufferAllocateInfo.commandPool = m_pImpl->CommandPool;
    CommandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    CommandBufferAllocateInfo.commandBufferCount = 1;
    if( !CheckVk( vkAllocateCommandBuffers( m_pImpl->Device,
                                             &CommandBufferAllocateInfo,
                                             &m_pImpl->CommandBuffer ),
                  "command buffer allocation", m_LastError,
                  sizeof(m_LastError) ) )
    {
        Shutdown();
        m_State = session_state::Failed;
        return FALSE;
    }

    VkFenceCreateInfo FenceCreateInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    if( !CheckVk( vkCreateFence( m_pImpl->Device, &FenceCreateInfo, NULL,
                                 &m_pImpl->Fence ),
                  "fence creation", m_LastError, sizeof(m_LastError) ) )
    {
        Shutdown();
        m_State = session_state::Failed;
        return FALSE;
    }

    m_State = session_state::Ready;
    return TRUE;
}

void VulkanSession::Shutdown( void )
{
    if( !m_pImpl )
    {
        if( m_State != session_state::Uninitialized )
            m_State = session_state::Stopped;
        return;
    }

    if( m_pImpl->SessionRunning )
    {
        xrEndSession( m_pImpl->Session );
        m_pImpl->SessionRunning = FALSE;
    }

    if( m_pImpl->Device )
        vkDeviceWaitIdle( m_pImpl->Device );

    for( Impl::Swapchain& Swapchain : m_pImpl->Swapchains )
    {
        for( VkFramebuffer Framebuffer : Swapchain.Framebuffers )
        {
            if( Framebuffer && m_pImpl->Device )
                vkDestroyFramebuffer( m_pImpl->Device, Framebuffer, NULL );
        }
        for( VkImageView ImageView : Swapchain.ImageViews )
        {
            if( ImageView && m_pImpl->Device )
                vkDestroyImageView( m_pImpl->Device, ImageView, NULL );
        }
        if( Swapchain.Handle )
            xrDestroySwapchain( Swapchain.Handle );
    }

    if( m_pImpl->Fence && m_pImpl->Device )
        vkDestroyFence( m_pImpl->Device, m_pImpl->Fence, NULL );
    if( m_pImpl->CommandPool && m_pImpl->Device )
        vkDestroyCommandPool( m_pImpl->Device, m_pImpl->CommandPool, NULL );
    if( m_pImpl->RenderPass && m_pImpl->Device )
        vkDestroyRenderPass( m_pImpl->Device, m_pImpl->RenderPass, NULL );
    if( m_pImpl->LeftStickAction )
        xrDestroyAction( m_pImpl->LeftStickAction );
    if( m_pImpl->RightStickAction )
        xrDestroyAction( m_pImpl->RightStickAction );
    if( m_pImpl->LeftTriggerAction )
        xrDestroyAction( m_pImpl->LeftTriggerAction );
    if( m_pImpl->RightTriggerAction )
        xrDestroyAction( m_pImpl->RightTriggerAction );
    if( m_pImpl->LeftXAction )
        xrDestroyAction( m_pImpl->LeftXAction );
    if( m_pImpl->LeftYAction )
        xrDestroyAction( m_pImpl->LeftYAction );
    if( m_pImpl->RightAAction )
        xrDestroyAction( m_pImpl->RightAAction );
    if( m_pImpl->RightBAction )
        xrDestroyAction( m_pImpl->RightBAction );
    if( m_pImpl->LeftStickClickAction )
        xrDestroyAction( m_pImpl->LeftStickClickAction );
    if( m_pImpl->RightStickClickAction )
        xrDestroyAction( m_pImpl->RightStickClickAction );
    if( m_pImpl->LeftMenuAction )
        xrDestroyAction( m_pImpl->LeftMenuAction );
    if( m_pImpl->InputActionSet )
        xrDestroyActionSet( m_pImpl->InputActionSet );
    if( m_pImpl->Space )
        xrDestroySpace( m_pImpl->Space );
    if( m_pImpl->Session )
        xrDestroySession( m_pImpl->Session );
    if( m_pImpl->Device && !m_pImpl->ExternalVulkan )
        vkDestroyDevice( m_pImpl->Device, NULL );
    if( m_pImpl->VkInstanceHandle && !m_pImpl->ExternalVulkan )
        vkDestroyInstance( m_pImpl->VkInstanceHandle, NULL );

    delete m_pImpl;
    m_pImpl = NULL;
    m_State = session_state::Stopped;
}

xbool VulkanSession::PollEvents( void )
{
    if( !m_pImpl )
        return FALSE;

    for( ;; )
    {
        XrEventDataBuffer Event{ XR_TYPE_EVENT_DATA_BUFFER };
        const XrResult Result = xrPollEvent( m_pImpl->Instance, &Event );
        if( Result == XR_EVENT_UNAVAILABLE )
            return TRUE;
        if( !CheckXr( Result, "xrPollEvent", m_LastError,
                      sizeof(m_LastError) ) )
        {
            m_State = session_state::Failed;
            return FALSE;
        }

#if defined(TARGET_ANDROID)
        if( Event.type == XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB )
        {
            const XrEventDataDisplayRefreshRateChangedFB* RefreshRateChanged =
                reinterpret_cast<const XrEventDataDisplayRefreshRateChangedFB*>(
                    &Event );
            x_DebugMsg( "OpenXR: display refresh rate changed %.2f -> %.2f Hz\n",
                        RefreshRateChanged->fromDisplayRefreshRate,
                        RefreshRateChanged->toDisplayRefreshRate );
            continue;
        }
#endif

        if( Event.type != XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED )
            continue;

        const XrEventDataSessionStateChanged* StateChanged =
            reinterpret_cast<const XrEventDataSessionStateChanged*>( &Event );
        m_pImpl->SessionState = StateChanged->state;

        if( StateChanged->state == XR_SESSION_STATE_READY &&
            !m_pImpl->SessionRunning )
        {
            m_pImpl->TrackingOriginValid = FALSE;
            XrSessionBeginInfo BeginInfo{ XR_TYPE_SESSION_BEGIN_INFO };
            BeginInfo.primaryViewConfigurationType =
                m_pImpl->ViewConfigurationType;
            if( !CheckXr( xrBeginSession( m_pImpl->Session, &BeginInfo ),
                          "xrBeginSession", m_LastError,
                          sizeof(m_LastError) ) )
            {
                m_State = session_state::Failed;
                return FALSE;
            }
            m_pImpl->SessionRunning = TRUE;
            m_State = session_state::Running;

#if defined(TARGET_ANDROID)
            if( m_pImpl->RequestDisplayRefreshRate )
            {
                const XrResult RequestResult =
                    m_pImpl->RequestDisplayRefreshRate( m_pImpl->Session,
                                                        72.0f );
                float CurrentRefreshRate = 0.0f;
                const XrResult GetRateResult = m_pImpl->GetDisplayRefreshRate
                    ? m_pImpl->GetDisplayRefreshRate( m_pImpl->Session,
                                                       &CurrentRefreshRate )
                    : XR_ERROR_FUNCTION_UNSUPPORTED;
                x_DebugMsg( "OpenXR: request 72 Hz result=%d, current=%.2f Hz (query=%d)\n",
                            static_cast<int>( RequestResult ),
                            CurrentRefreshRate,
                            static_cast<int>( GetRateResult ) );
            }
            else
            {
                x_DebugMsg( "OpenXR: XR_FB_display_refresh_rate unavailable; cannot request 72 Hz\n" );
            }
#endif
        }
        else if( StateChanged->state == XR_SESSION_STATE_STOPPING &&
                 m_pImpl->SessionRunning )
        {
            if( !CheckXr( xrEndSession( m_pImpl->Session ), "xrEndSession",
                          m_LastError, sizeof(m_LastError) ) )
            {
                m_State = session_state::Failed;
                return FALSE;
            }
            m_pImpl->SessionRunning = FALSE;
            m_State = session_state::Stopping;
                    }
        else if( StateChanged->state == XR_SESSION_STATE_EXITING )
        {
            m_pImpl->SessionRunning = FALSE;
            m_State = session_state::Exiting;
                    }
        else if( StateChanged->state == XR_SESSION_STATE_LOSS_PENDING )
        {
            m_pImpl->SessionRunning = FALSE;
            m_State = session_state::LossPending;
                    }
    }
}

void VulkanSession::CaptureInput( ::input_event_buffer& Events )
{
    if( !m_pImpl || !m_pImpl->InputActionsReady ||
        !m_pImpl->SessionRunning )
    {
        return;
    }

    XrActiveActionSet ActiveActionSet{};
    ActiveActionSet.actionSet = m_pImpl->InputActionSet;
    ActiveActionSet.subactionPath = XR_NULL_PATH;

    XrActionsSyncInfo SyncInfo{ XR_TYPE_ACTIONS_SYNC_INFO };
    SyncInfo.countActiveActionSets = 1;
    SyncInfo.activeActionSets = &ActiveActionSet;
    if( XR_FAILED( xrSyncActions( m_pImpl->Session, &SyncInfo ) ) )
        return;

    const u32 TimeStamp = Events.GetEndTimeStamp();

    auto ReadBoolean = [&]( XrAction Action, xbool& Value ) -> xbool
    {
        XrActionStateGetInfo GetInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
        GetInfo.action = Action;
        GetInfo.subactionPath = XR_NULL_PATH;
        XrActionStateBoolean State{ XR_TYPE_ACTION_STATE_BOOLEAN };
        if( XR_FAILED( xrGetActionStateBoolean( m_pImpl->Session,
                                                &GetInfo, &State ) ) )
        {
            Value = FALSE;
            return FALSE;
        }
        Value = (State.isActive && State.currentState) ? TRUE : FALSE;
        return TRUE;
    };

    auto AppendBoolean = [&]( XrAction Action, xbool& Previous,
                              input_gadget Gadget )
    {
        xbool Current = FALSE;
        if( !ReadBoolean( Action, Current ) )
            return;
        if( Current == Previous )
            return;
        Events.Append( Gadget, 0,
                       Current ? INPUT_EVENT_PRESSED : INPUT_EVENT_RELEASED,
                       Current ? 1.0f : 0.0f, TimeStamp );
        Previous = Current;
    };

    auto ReadFloat = [&]( XrAction Action, f32& Value ) -> xbool
    {
        XrActionStateGetInfo GetInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
        GetInfo.action = Action;
        GetInfo.subactionPath = XR_NULL_PATH;
        XrActionStateFloat State{ XR_TYPE_ACTION_STATE_FLOAT };
        if( XR_FAILED( xrGetActionStateFloat( m_pImpl->Session,
                                              &GetInfo, &State ) ) )
        {
            Value = 0.0f;
            return FALSE;
        }
        Value = State.isActive ? State.currentState : 0.0f;
        Value = (Value < 0.0f) ? 0.0f : (Value > 1.0f) ? 1.0f : Value;
        return TRUE;
    };

    auto AppendTrigger = [&]( XrAction Action, f32& Previous,
                              input_gadget Gadget )
    {
        f32 Current = 0.0f;
        if( !ReadFloat( Action, Current ) )
            return;

        /* XR analog values are not raw backend state. The input snapshot is
         * rebuilt from zero every frame, so publish the held value every
         * frame instead of only publishing transitions. */
        Events.Append( Gadget, 0, INPUT_EVENT_ABSOLUTE,
                       Current, TimeStamp );

        const xbool WasDown = Previous >= 0.5f;
        const xbool IsDown = Current >= 0.5f;
        if( WasDown != IsDown )
        {
            Events.Append( Gadget, 0,
                           IsDown ? INPUT_EVENT_PRESSED : INPUT_EVENT_RELEASED,
                           IsDown ? Current : 0.0f, TimeStamp );
        }
        Previous = Current;
    };

    auto AppendStick = [&]( XrAction Action, f32 Previous[2],
                            input_gadget XGadget, input_gadget YGadget,
                            xbool bAllowY )
    {
        XrActionStateGetInfo GetInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
        GetInfo.action = Action;
        GetInfo.subactionPath = XR_NULL_PATH;
        XrActionStateVector2f State{ XR_TYPE_ACTION_STATE_VECTOR2F };
        if( XR_FAILED( xrGetActionStateVector2f( m_pImpl->Session,
                                                 &GetInfo, &State ) ) )
        {
            return;
        }

        f32 Current[2] = { 0.0f, 0.0f };
        if( State.isActive )
        {
            Current[0] = State.currentState.x;
            Current[1] = State.currentState.y;
        }
        Current[0] = (Current[0] < -1.0f) ? -1.0f
                    : (Current[0] > 1.0f) ? 1.0f : Current[0];
        Current[1] = (Current[1] < -1.0f) ? -1.0f
                    : (Current[1] > 1.0f) ? 1.0f : Current[1];

        /* The XR action set is sampled independently of the legacy input
         * backend. Emit both axes every frame so a held stick remains held
         * in input_snapshot after it is rebuilt for the next frame. */
        Events.Append( XGadget, 0, INPUT_EVENT_ABSOLUTE,
                       Current[0], TimeStamp );
        Events.Append( YGadget, 0, INPUT_EVENT_ABSOLUTE,
                       bAllowY ? Current[1] : 0.0f, TimeStamp );
        Previous[0] = Current[0];
        Previous[1] = Current[1];
    };

    AppendStick( m_pImpl->LeftStickAction, m_pImpl->PreviousLeftStick,
                 INPUT_XBOX_STICK_LEFT_X, INPUT_XBOX_STICK_LEFT_Y, TRUE );
    AppendStick( m_pImpl->RightStickAction, m_pImpl->PreviousRightStick,
                 INPUT_XBOX_STICK_RIGHT_X, INPUT_XBOX_STICK_RIGHT_Y, FALSE );
    AppendTrigger( m_pImpl->LeftTriggerAction, m_pImpl->PreviousLeftTrigger,
                   INPUT_XBOX_L_TRIGGER );
    AppendTrigger( m_pImpl->RightTriggerAction, m_pImpl->PreviousRightTrigger,
                   INPUT_XBOX_R_TRIGGER );
    AppendBoolean( m_pImpl->LeftXAction, m_pImpl->PreviousLeftX,
                   INPUT_XBOX_BTN_X );
    AppendBoolean( m_pImpl->LeftYAction, m_pImpl->PreviousLeftY,
                   INPUT_XBOX_BTN_Y );
    AppendBoolean( m_pImpl->RightAAction, m_pImpl->PreviousRightA,
                   INPUT_XBOX_BTN_A );
    AppendBoolean( m_pImpl->RightBAction, m_pImpl->PreviousRightB,
                   INPUT_XBOX_BTN_B );
    AppendBoolean( m_pImpl->LeftStickClickAction,
                   m_pImpl->PreviousLeftStickClick,
                   INPUT_XBOX_BTN_L_STICK );
    AppendBoolean( m_pImpl->RightStickClickAction,
                   m_pImpl->PreviousRightStickClick,
                   INPUT_XBOX_BTN_R_STICK );
    AppendBoolean( m_pImpl->LeftMenuAction, m_pImpl->PreviousLeftMenu,
                   INPUT_XBOX_BTN_START );
}

void VulkanSession::CancelFrame( void )
{
    if( !m_pImpl )
        return;


    for( u32 i = 0; i < m_pImpl->Swapchains.size(); ++i )
    {
        if( (i < m_pImpl->AcquiredImages.size()) &&
            m_pImpl->AcquiredImages[i] )
        {
            XrSwapchainImageReleaseInfo ReleaseInfo{
                XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
            xrReleaseSwapchainImage( m_pImpl->Swapchains[i].Handle,
                                     &ReleaseInfo );
            m_pImpl->AcquiredImages[i] = FALSE;
        }
    }

    if( m_pImpl->FrameBegun )
    {
        XrFrameEndInfo FrameEndInfo{ XR_TYPE_FRAME_END_INFO };
        FrameEndInfo.displayTime = m_pImpl->PreparedFrameState.predictedDisplayTime;
        FrameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        xrEndFrame( m_pImpl->Session, &FrameEndInfo );
        m_pImpl->FrameBegun = FALSE;
    }
    m_pImpl->ProjectionViews.clear();
    m_pImpl->AcquiredImageIndices.clear();
    m_pImpl->AcquiredImages.clear();
    m_pImpl->UseQuadLayer = FALSE;
}

xbool VulkanSession::GetVulkanDeviceInfo( vulkan_device_info& DeviceInfo ) const
{
    if( !m_pImpl || !m_pImpl->VkInstanceHandle || !m_pImpl->PhysicalDevice ||
        !m_pImpl->Device || !m_pImpl->Queue )
    {
        return FALSE;
    }

    DeviceInfo.Instance         = reinterpret_cast<void*>( m_pImpl->VkInstanceHandle );
    DeviceInfo.PhysicalDevice   = reinterpret_cast<void*>( m_pImpl->PhysicalDevice );
    DeviceInfo.Device           = reinterpret_cast<void*>( m_pImpl->Device );
    DeviceInfo.Queue            = reinterpret_cast<void*>( m_pImpl->Queue );
    DeviceInfo.QueueFamilyIndex = m_pImpl->QueueFamilyIndex;
    DeviceInfo.MultiviewEnabled = m_pImpl->MultiviewEnabled;
    return TRUE;
}

xbool VulkanSession::SupportsMultiview( void ) const
{
    return m_pImpl && m_pImpl->MultiviewEnabled;
}

xbool VulkanSession::GetRecommendedRenderSize( u32& Width, u32& Height ) const
{
    if( !m_pImpl || m_pImpl->Swapchains.empty() )
        return FALSE;

    Width  = m_pImpl->Swapchains[0].Width;
    Height = m_pImpl->Swapchains[0].Height;
    return (Width > 0) && (Height > 0);
}

xbool VulkanSession::GetAcquiredImage( u32 Eye, void*& Image ) const
{
    Image = NULL;
    if( !m_pImpl || Eye >= m_pImpl->Views.size() )
        return FALSE;
    const u32 SwapchainIndex = m_pImpl->ArraySwapchain ? 0u : Eye;
    if( SwapchainIndex >= m_pImpl->Swapchains.size() ||
        SwapchainIndex >= m_pImpl->AcquiredImages.size() ||
        !m_pImpl->AcquiredImages[SwapchainIndex] )
        return FALSE;
    const Impl::Swapchain& Swapchain = m_pImpl->Swapchains[SwapchainIndex];
    const u32 Index = m_pImpl->AcquiredImageIndices[SwapchainIndex];
    if( Index >= Swapchain.Images.size() )
        return FALSE;
    Image = reinterpret_cast<void*>( Swapchain.Images[Index].image );
    return Image != NULL;
}

xbool VulkanSession::UsesMutableSrgbImages( void ) const
{
    if( !m_pImpl || m_pImpl->Swapchains.empty() )
        return FALSE;
    const VkFormat Format = m_pImpl->Swapchains[0].Format;
    return m_pImpl->MutableSrgbImages ||
           (Format == VK_FORMAT_R8G8B8A8_UNORM) ||
           (Format == VK_FORMAT_B8G8R8A8_UNORM);
}

xbool VulkanSession::UsesArraySwapchain( void ) const
{
    return m_pImpl && m_pImpl->ArraySwapchain;
}

xbool VulkanSession::ShouldRenderFrame( void ) const
{
    return m_pImpl && m_pImpl->FrameBegun &&
           m_pImpl->PreparedFrameState.shouldRender;
}

xbool VulkanSession::UsesBgraSwapchain( void ) const
{
    return m_pImpl && !m_pImpl->Swapchains.empty() &&
           (m_pImpl->Swapchains[0].Format == VK_FORMAT_B8G8R8A8_SRGB ||
            m_pImpl->Swapchains[0].Format == VK_FORMAT_B8G8R8A8_UNORM);
}

xbool VulkanSession::PrepareFrame( const vulkan_frame_info& FrameInfo )
{
    return PrepareStereoFrame( FrameInfo, FrameInfo );
}

xbool VulkanSession::PrepareQuadFrame( const vulkan_frame_info& FrameInfo,
                                        f32 WidthMeters,
                                        f32 HeightMeters,
                                        f32 DistanceMeters )
{
        if( !m_pImpl || !FrameInfo.CommandBuffer || !FrameInfo.SourceImage )
        return FALSE;

    if( !BeginFrame() )
        return FALSE;
    if( !m_pImpl->FrameBegun )
    {
                return TRUE;
    }
    if( m_pImpl->Swapchains.empty() )
        return FALSE;

    const u32 ViewCount = static_cast<u32>( m_pImpl->Views.size() );
    m_pImpl->ProjectionViews.clear();

    if( m_pImpl->PreparedFrameState.shouldRender )
    {
        Impl::Swapchain& Swapchain = m_pImpl->Swapchains[0];
        const VkImage SourceImage = reinterpret_cast<VkImage>(
            FrameInfo.SourceImage );
        const VkImage DestinationImage = Swapchain.Images[
            m_pImpl->AcquiredImageIndices[0]].image;
        if( SourceImage != DestinationImage ||
            FrameInfo.Width != Swapchain.Width ||
            FrameInfo.Height != Swapchain.Height )
        {
            std::snprintf( m_LastError, sizeof(m_LastError),
                           "quad source is not the acquired OpenXR image" );
            return FALSE;
        }

        m_pImpl->QuadLayer = { XR_TYPE_COMPOSITION_LAYER_QUAD };
        m_pImpl->QuadLayer.space = m_pImpl->Space;
        m_pImpl->QuadLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        m_pImpl->QuadLayer.pose.orientation.w = 1.0f;
        m_pImpl->QuadLayer.pose.position.z = -DistanceMeters;
        m_pImpl->QuadLayer.size.width = WidthMeters;
        m_pImpl->QuadLayer.size.height = HeightMeters;
        m_pImpl->QuadLayer.subImage.swapchain = Swapchain.Handle;
        m_pImpl->QuadLayer.subImage.imageRect.offset = { 0, 0 };
        m_pImpl->QuadLayer.subImage.imageRect.extent = {
            static_cast<s32>( Swapchain.Width ),
            static_cast<s32>( Swapchain.Height ) };
        m_pImpl->QuadLayer.layerFlags =
            XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        m_pImpl->UseQuadLayer = TRUE;
    }

    m_pImpl->ProjectionLayer = {
        XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    m_pImpl->ProjectionLayer.space = m_pImpl->Space;
    m_pImpl->ProjectionLayer.viewCount = 0;
    m_pImpl->ProjectionLayer.views = NULL;
    return TRUE;
}

xbool VulkanSession::BeginFrame( void )
{
        if( !m_pImpl )
        return FALSE;
    if( m_pImpl->FrameBegun )
        return TRUE;

    if( !PollEvents() )
    {
                return FALSE;
    }
    if( !m_pImpl->SessionRunning )
    {
                return TRUE;
    }

    XrFrameWaitInfo FrameWaitInfo{ XR_TYPE_FRAME_WAIT_INFO };
    m_pImpl->PreparedFrameState = { XR_TYPE_FRAME_STATE };
    if( !CheckXr( xrWaitFrame( m_pImpl->Session, &FrameWaitInfo,
                               &m_pImpl->PreparedFrameState ),
                  "xrWaitFrame", m_LastError, sizeof(m_LastError) ) )
    {
                return FALSE;
    }

    XrFrameBeginInfo FrameBeginInfo{ XR_TYPE_FRAME_BEGIN_INFO };
    if( !CheckXr( xrBeginFrame( m_pImpl->Session, &FrameBeginInfo ),
                  "xrBeginFrame", m_LastError, sizeof(m_LastError) ) )
    {
                return FALSE;
    }
    m_pImpl->FrameBegun = TRUE;

#if defined(TARGET_ANDROID)
    if( m_pImpl->QueryPerformanceCounter && !m_pImpl->PerformanceCounters.empty() )
    {
        const xtick Now = x_GetTime();
        if( !m_pImpl->LastPerformanceLog || x_TicksToMs( Now - m_pImpl->LastPerformanceLog ) >= 1000.0f )
        {
            char Metrics[1800] = {};
            size_t Used = 0;
            for( const Impl::PerformanceCounter& Counter : m_pImpl->PerformanceCounters )
            {
                XrPerformanceMetricsCounterMETA Value{ XR_TYPE_PERFORMANCE_METRICS_COUNTER_META };
                if( XR_FAILED( m_pImpl->QueryPerformanceCounter( m_pImpl->Session, Counter.Path, &Value ) ) ||
                    !(Value.counterFlags & XR_PERFORMANCE_METRICS_COUNTER_ANY_VALUE_VALID_BIT_META) )
                    continue;
                const double Number = (Value.counterFlags & XR_PERFORMANCE_METRICS_COUNTER_FLOAT_VALUE_VALID_BIT_META)
                                    ? Value.floatValue : static_cast<double>( Value.uintValue );
                const int Written = std::snprintf( Metrics + Used, sizeof(Metrics) - Used,
                    "%s%s=%.3f", Used ? " " : "", Counter.Name, Number );
                if( Written <= 0 || static_cast<size_t>( Written ) >= sizeof(Metrics) - Used )
                    break;
                Used += static_cast<size_t>( Written );
            }
            __android_log_print( ANDROID_LOG_INFO, "A51Perf", "runtime %s", Used ? Metrics : "counter query returned no values" );
            m_pImpl->LastPerformanceLog = Now;
        }
    }
#endif

    u32 ViewCount = 0;
    XrViewLocateInfo ViewLocateInfo{ XR_TYPE_VIEW_LOCATE_INFO };
    ViewLocateInfo.viewConfigurationType = m_pImpl->ViewConfigurationType;
    ViewLocateInfo.displayTime = m_pImpl->PreparedFrameState.predictedDisplayTime;
    ViewLocateInfo.space = m_pImpl->Space;
    XrViewState ViewState{ XR_TYPE_VIEW_STATE };
    if( !CheckXr( xrLocateViews( m_pImpl->Session, &ViewLocateInfo, &ViewState,
                                 static_cast<u32>(m_pImpl->Views.size()),
                                 &ViewCount, m_pImpl->Views.data() ),
                  "xrLocateViews", m_LastError, sizeof(m_LastError) ) )
    {
                CancelFrame();
        return FALSE;
    }


    if( ViewCount != m_pImpl->Views.size() ||
        m_pImpl->Swapchains.size() !=
            (m_pImpl->ArraySwapchain ? 1u : ViewCount) )
    {
        SetError( m_LastError, sizeof(m_LastError),
                  "OpenXR returned %u views, expected %u",
                  ViewCount, static_cast<u32>(m_pImpl->Swapchains.size()) );
        CancelFrame();
        return FALSE;
    }

    if( !m_pImpl->TrackingOriginValid && (ViewCount >= 2) )
    {
        m_pImpl->TrackingOrigin = CentreYawAnchor( m_pImpl->Views[0],
                                                    m_pImpl->Views[1] );
        m_pImpl->TrackingOriginValid = TRUE;
            }

    for( u32 Eye = 0; Eye < ViewCount; ++Eye )
    {
        const XrPosef RelativeView = m_pImpl->TrackingOriginValid
                                   ? RelativePose( m_pImpl->TrackingOrigin,
                                                   m_pImpl->Views[Eye].pose )
                                   : m_pImpl->Views[Eye].pose;
            }

    const u32 SwapchainCount = static_cast<u32>( m_pImpl->Swapchains.size() );
    m_pImpl->AcquiredImageIndices.assign( SwapchainCount, 0 );
    m_pImpl->AcquiredImages.assign( SwapchainCount, FALSE );
    if( m_pImpl->PreparedFrameState.shouldRender )
    {
        for( u32 Eye = 0; Eye < SwapchainCount; ++Eye )
        {
            Impl::Swapchain& Swapchain = m_pImpl->Swapchains[Eye];
            XrSwapchainImageAcquireInfo AcquireInfo{
                XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
            if( !CheckXr( xrAcquireSwapchainImage(
                              Swapchain.Handle, &AcquireInfo,
                              &m_pImpl->AcquiredImageIndices[Eye] ),
                          "xrAcquireSwapchainImage", m_LastError,
                          sizeof(m_LastError) ) )
            {
                CancelFrame();
                return FALSE;
            }
            m_pImpl->AcquiredImages[Eye] = TRUE;
            XrSwapchainImageWaitInfo WaitInfo{
                XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
            WaitInfo.timeout = XR_INFINITE_DURATION;
#if defined(TARGET_ANDROID)
            const xtick SwapchainWaitStart = x_GetTime();
#endif
            if( !CheckXr( xrWaitSwapchainImage( Swapchain.Handle, &WaitInfo ),
                          "xrWaitSwapchainImage", m_LastError,
                          sizeof(m_LastError) ) )
            {
#if defined(TARGET_ANDROID)
                RecordSwapchainWaitMs( x_TicksToMs( x_GetTime() - SwapchainWaitStart ) );
#endif
                CancelFrame();
                return FALSE;
            }
#if defined(TARGET_ANDROID)
            RecordSwapchainWaitMs( x_TicksToMs( x_GetTime() - SwapchainWaitStart ) );
#endif
        }
    }

    return TRUE;
}

xbool VulkanSession::GetEyeView( u32 Eye, eye_view& View ) const
{
    if( !m_pImpl || Eye >= m_pImpl->Views.size() )
        return FALSE;

    const XrView& XRView = m_pImpl->Views[Eye];
    const XrPosef RelativeEye = m_pImpl->TrackingOriginValid
                              ? RelativePose( m_pImpl->TrackingOrigin,
                                              XRView.pose )
                              : XRView.pose;

    /* Return the relative pose, exactly like Simpsons' ComposeTrackedCamera.
     * The game camera consumes this pose once; the compositor still receives
     * the original absolute XrView pose in PrepareStereoFrame(). */
    /* Area 51 currently runs the headset camera in 3DoF mode. Do not feed
     * the headset's absolute/room-scale translation into the game camera;
     * only the physical eye separation is needed here. The eye offset is
     * rotated with RelativeEye.orientation by the game camera composition. */
    /* Area 51's view space is left-handed relative to OpenXR's horizontal
     * axis: +X is Camera Left here, while +X is Camera Right in OpenXR.
     * Keep the OpenXR projection/layer in its native convention, but map the
     * eye baseline into the game camera convention before RenderGame composes
     * the eye camera.  Without this sign change the left image is rendered
     * from the right-eye position and vice versa; the compositor then places
     * those images using the correct OpenXR eye poses. */
    View.Position[0] = (Eye == 0) ? 0.032f : -0.032f;
    View.Position[1] = 0.0f;
    View.Position[2] = 0.0f;
    /* Keep the camera pose and compositor pose from the same XrView. This is
     * the important comparison with Simpsons: each eye uses its own view,
     * while both passes share the same composition and projection code. */
    XrQuaternionf Orientation = RelativeEye.orientation;
    View.Orientation[0] = Orientation.x;
    View.Orientation[1] = Orientation.y;
    View.Orientation[2] = Orientation.z;
    View.Orientation[3] = Orientation.w;
    View.FovLeft  = XRView.fov.angleLeft;
    View.FovRight = XRView.fov.angleRight;
    View.FovUp    = XRView.fov.angleUp;
    View.FovDown  = XRView.fov.angleDown;

    static xbool LoggedEye[2] = { FALSE, FALSE };
    if( !LoggedEye[Eye] )
    {
        x_DebugMsg( "OpenXR eye %u: pos %.5f %.5f %.5f fov L/R/U/D %.5f %.5f %.5f %.5f\n",
                    Eye,
                    View.Position[0], View.Position[1], View.Position[2],
                    View.FovLeft, View.FovRight, View.FovUp, View.FovDown );
        LoggedEye[Eye] = TRUE;
    }
    return TRUE;
}

xbool VulkanSession::PrepareStereoFrame( const vulkan_frame_info& LeftFrame,
                                         const vulkan_frame_info& RightFrame )
{
    return PrepareStereoFrameInternal( LeftFrame, RightFrame, FALSE );
}

xbool VulkanSession::PrepareMultiviewFrame( const vulkan_frame_info& ArrayFrame )
{
    return PrepareStereoFrameInternal( ArrayFrame, ArrayFrame, TRUE );
}

xbool VulkanSession::PrepareStereoFrameInternal( const vulkan_frame_info& LeftFrame,
                                                  const vulkan_frame_info& RightFrame,
                                                  xbool bArraySource )
{
        if( !m_pImpl || !LeftFrame.CommandBuffer || !LeftFrame.SourceImage ||
        !RightFrame.CommandBuffer || !RightFrame.SourceImage ||
        (LeftFrame.CommandBuffer != RightFrame.CommandBuffer) ||
        (bArraySource && (!m_pImpl->MultiviewEnabled ||
                          !m_pImpl->ArraySwapchain)) ||
        (bArraySource && ((LeftFrame.Width != RightFrame.Width) ||
                          (LeftFrame.Height != RightFrame.Height))) )
    {
                return FALSE;
    }

    const VkImage CurrentLeftSource = reinterpret_cast<VkImage>( LeftFrame.SourceImage );
    const VkImage CurrentRightSource = reinterpret_cast<VkImage>( RightFrame.SourceImage );
    static xbool LoggedStereoSources = FALSE;
    if( !LoggedStereoSources )
    {
        x_DebugMsg( "OpenXR direct stereo targets: left=%p right=%p command=%p\n",
                    reinterpret_cast<void*>( CurrentLeftSource ),
                    reinterpret_cast<void*>( CurrentRightSource ),
                    LeftFrame.CommandBuffer );
        LoggedStereoSources = TRUE;
    }

    if( !BeginFrame() )
    {
                return FALSE;
    }
    if( !m_pImpl->FrameBegun )
    {
                return TRUE;
    }

    const u32 ViewCount = static_cast<u32>( m_pImpl->Views.size() );

    m_pImpl->ProjectionViews.clear();
    if( m_pImpl->PreparedFrameState.shouldRender &&
        (m_pImpl->Swapchains.size() ==
            (m_pImpl->ArraySwapchain ? 1u : ViewCount)) )
    {
        m_pImpl->ProjectionViews.resize( ViewCount );
        for( u32 ViewIndex = 0; ViewIndex < ViewCount; ++ViewIndex )
        {
            const vulkan_frame_info& EyeFrame = (ViewIndex == 0)
                                               ? LeftFrame : RightFrame;
            const VkImage SourceImage = reinterpret_cast<VkImage>(
                EyeFrame.SourceImage );
            const u32 SwapchainIndex = m_pImpl->ArraySwapchain ? 0u : ViewIndex;
            Impl::Swapchain& Swapchain = m_pImpl->Swapchains[SwapchainIndex];
            const VkImage DestinationImage = Swapchain.Images[
                m_pImpl->AcquiredImageIndices[SwapchainIndex]].image;
            if( SourceImage != DestinationImage ||
                EyeFrame.Width != Swapchain.Width ||
                EyeFrame.Height != Swapchain.Height )
            {
                std::snprintf( m_LastError, sizeof(m_LastError),
                               "stereo source is not the acquired OpenXR image" );
                m_pImpl->ProjectionViews.clear();
                return FALSE;
            }


            static u32 LoggedStereoHandles = 0;
            if( LoggedStereoHandles < 40 )
            {
                x_DebugMsg( "OpenXR direct eye %u: image=%p size=%ux%u\n",
                            ViewIndex,
                            reinterpret_cast<void*>( EyeFrame.SourceImage ),
                            EyeFrame.Width, EyeFrame.Height );
                LoggedStereoHandles++;
            }

            m_pImpl->ProjectionViews[ViewIndex] = {
                XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
            m_pImpl->ProjectionViews[ViewIndex].pose =
                m_pImpl->Views[ViewIndex].pose;
            m_pImpl->ProjectionViews[ViewIndex].fov =
                m_pImpl->Views[ViewIndex].fov;
            m_pImpl->ProjectionViews[ViewIndex].subImage.swapchain =
                Swapchain.Handle;
            m_pImpl->ProjectionViews[ViewIndex].subImage.imageArrayIndex =
                m_pImpl->ArraySwapchain ? ViewIndex : 0;
            m_pImpl->ProjectionViews[ViewIndex].subImage.imageRect.offset = {
                0, 0 };
            m_pImpl->ProjectionViews[ViewIndex].subImage.imageRect.extent = {
                static_cast<s32>( Swapchain.Width ),
                static_cast<s32>( Swapchain.Height ) };

                    }
    }
    else
    {
            }

    m_pImpl->ProjectionLayer = {
        XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    m_pImpl->ProjectionLayer.space = m_pImpl->Space;
    m_pImpl->ProjectionLayer.viewCount =
        static_cast<u32>( m_pImpl->ProjectionViews.size() );
    m_pImpl->ProjectionLayer.views = m_pImpl->ProjectionViews.data();
    m_pImpl->UseQuadLayer = FALSE;
        return TRUE;
}

xbool VulkanSession::FinishFrame( void )
{
        if( !m_pImpl || !m_pImpl->FrameBegun )
        return TRUE;

    // SDL has submitted rendering to the Vulkan queue bound to this OpenXR
    // session. OpenXR owns synchronization when a released swapchain image is
    // still referenced by submitted Vulkan work; a host queue-idle here stalls
    // the CPU every frame and removes CPU/GPU overlap.
    for( u32 i = 0; i < m_pImpl->Swapchains.size(); ++i )
    {
        if( (i < m_pImpl->AcquiredImages.size()) &&
            m_pImpl->AcquiredImages[i] )
        {
            XrSwapchainImageReleaseInfo ReleaseInfo{
                XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
            if( !CheckXr( xrReleaseSwapchainImage( m_pImpl->Swapchains[i].Handle,
                                                   &ReleaseInfo ),
                          "xrReleaseSwapchainImage", m_LastError,
                          sizeof(m_LastError) ) )
            {
                                CancelFrame();
                return FALSE;
            }
            m_pImpl->AcquiredImages[i] = FALSE;
                    }
    }

    XrFrameEndInfo FrameEndInfo{ XR_TYPE_FRAME_END_INFO };
    FrameEndInfo.displayTime = m_pImpl->PreparedFrameState.predictedDisplayTime;
    FrameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    const XrCompositionLayerBaseHeader* LayerHeader = NULL;
    if( m_pImpl->UseQuadLayer )
    {
        LayerHeader = reinterpret_cast<const XrCompositionLayerBaseHeader*>(
            &m_pImpl->QuadLayer );
    }
    else if( m_pImpl->ProjectionLayer.viewCount )
    {
        LayerHeader = reinterpret_cast<const XrCompositionLayerBaseHeader*>(
            &m_pImpl->ProjectionLayer );
    }
    if( LayerHeader )
    {
        FrameEndInfo.layerCount = 1;
        FrameEndInfo.layers = &LayerHeader;
    }
    const xbool Result = CheckXr( xrEndFrame( m_pImpl->Session, &FrameEndInfo ),
                                  "xrEndFrame", m_LastError,
                                  sizeof(m_LastError) );
        m_pImpl->FrameBegun = FALSE;
    m_pImpl->ProjectionViews.clear();
    m_pImpl->AcquiredImageIndices.clear();
    m_pImpl->AcquiredImages.clear();
    m_pImpl->UseQuadLayer = FALSE;
    return Result;
}

xbool VulkanSession::RenderTestFrame( void )
{
    if( !m_pImpl )
        return FALSE;
    if( !PollEvents() )
        return FALSE;
    if( !m_pImpl->SessionRunning )
        return TRUE;

    XrFrameWaitInfo FrameWaitInfo{ XR_TYPE_FRAME_WAIT_INFO };
    XrFrameState FrameState{ XR_TYPE_FRAME_STATE };
    if( !CheckXr( xrWaitFrame( m_pImpl->Session, &FrameWaitInfo, &FrameState ),
                  "xrWaitFrame", m_LastError, sizeof(m_LastError) ) )
        return FALSE;

    XrFrameBeginInfo FrameBeginInfo{ XR_TYPE_FRAME_BEGIN_INFO };
    if( !CheckXr( xrBeginFrame( m_pImpl->Session, &FrameBeginInfo ),
                  "xrBeginFrame", m_LastError, sizeof(m_LastError) ) )
        return FALSE;

    u32 ViewCount = 0;
    XrViewLocateInfo ViewLocateInfo{ XR_TYPE_VIEW_LOCATE_INFO };
    ViewLocateInfo.viewConfigurationType = m_pImpl->ViewConfigurationType;
    ViewLocateInfo.displayTime = FrameState.predictedDisplayTime;
    ViewLocateInfo.space = m_pImpl->Space;
    XrViewState ViewState{ XR_TYPE_VIEW_STATE };
    if( !CheckXr( xrLocateViews( m_pImpl->Session, &ViewLocateInfo, &ViewState,
                                 static_cast<u32>(m_pImpl->Views.size()),
                                 &ViewCount, m_pImpl->Views.data() ),
                  "xrLocateViews", m_LastError, sizeof(m_LastError) ) )
        return FALSE;

    std::vector<XrCompositionLayerProjectionView> ProjectionViews;
    XrCompositionLayerProjection Layer{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    if( FrameState.shouldRender &&
        (ViewCount == m_pImpl->Swapchains.size()) )
    {
        ProjectionViews.resize( ViewCount );
        for( u32 ViewIndex = 0; ViewIndex < ViewCount; ++ViewIndex )
        {
            Impl::Swapchain& Swapchain = m_pImpl->Swapchains[ViewIndex];
            u32 ImageIndex = 0;
            XrSwapchainImageAcquireInfo AcquireInfo{
                XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
            if( !CheckXr( xrAcquireSwapchainImage( Swapchain.Handle,
                                                   &AcquireInfo, &ImageIndex ),
                          "xrAcquireSwapchainImage", m_LastError,
                          sizeof(m_LastError) ) )
                return FALSE;

            XrSwapchainImageWaitInfo WaitInfo{
                XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
            WaitInfo.timeout = XR_INFINITE_DURATION;
            if( !CheckXr( xrWaitSwapchainImage( Swapchain.Handle, &WaitInfo ),
                          "xrWaitSwapchainImage", m_LastError,
                          sizeof(m_LastError) ) )
                return FALSE;

            if( !CheckVk( vkResetFences( m_pImpl->Device, 1,
                                          &m_pImpl->Fence ),
                          "fence reset", m_LastError,
                          sizeof(m_LastError) ) )
                return FALSE;

            VkCommandBufferBeginInfo CommandBufferBeginInfo{
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            if( !CheckVk( vkBeginCommandBuffer( m_pImpl->CommandBuffer,
                                                &CommandBufferBeginInfo ),
                          "command buffer begin", m_LastError,
                          sizeof(m_LastError) ) )
                return FALSE;

            VkClearValue ClearValue{};
            ClearValue.color.float32[0] = ViewIndex ? 0.08f : 0.02f;
            ClearValue.color.float32[1] = 0.04f;
            ClearValue.color.float32[2] = ViewIndex ? 0.16f : 0.08f;
            ClearValue.color.float32[3] = 1.0f;

            VkRenderPassBeginInfo RenderPassBeginInfo{
                VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
            RenderPassBeginInfo.renderPass = m_pImpl->RenderPass;
            RenderPassBeginInfo.framebuffer = Swapchain.Framebuffers[ImageIndex];
            RenderPassBeginInfo.renderArea.extent.width = Swapchain.Width;
            RenderPassBeginInfo.renderArea.extent.height = Swapchain.Height;
            RenderPassBeginInfo.clearValueCount = 1;
            RenderPassBeginInfo.pClearValues = &ClearValue;
            vkCmdBeginRenderPass( m_pImpl->CommandBuffer,
                                  &RenderPassBeginInfo,
                                  VK_SUBPASS_CONTENTS_INLINE );
            vkCmdEndRenderPass( m_pImpl->CommandBuffer );

            if( !CheckVk( vkEndCommandBuffer( m_pImpl->CommandBuffer ),
                          "command buffer end", m_LastError,
                          sizeof(m_LastError) ) )
                return FALSE;

            VkSubmitInfo SubmitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
            SubmitInfo.commandBufferCount = 1;
            SubmitInfo.pCommandBuffers = &m_pImpl->CommandBuffer;
            if( !CheckVk( vkQueueSubmit( m_pImpl->Queue, 1, &SubmitInfo,
                                         m_pImpl->Fence ),
                          "queue submit", m_LastError,
                          sizeof(m_LastError) ) ||
                !CheckVk( vkWaitForFences( m_pImpl->Device, 1,
                                            &m_pImpl->Fence, VK_TRUE,
                                            UINT64_MAX ),
                          "queue wait", m_LastError,
                          sizeof(m_LastError) ) )
                return FALSE;

            XrSwapchainImageReleaseInfo ReleaseInfo{
                XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
            if( !CheckXr( xrReleaseSwapchainImage( Swapchain.Handle,
                                                   &ReleaseInfo ),
                          "xrReleaseSwapchainImage", m_LastError,
                          sizeof(m_LastError) ) )
                return FALSE;

            ProjectionViews[ViewIndex] = {
                XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
            ProjectionViews[ViewIndex].pose = m_pImpl->Views[ViewIndex].pose;
            ProjectionViews[ViewIndex].fov = m_pImpl->Views[ViewIndex].fov;
            ProjectionViews[ViewIndex].subImage.swapchain = Swapchain.Handle;
            ProjectionViews[ViewIndex].subImage.imageRect.offset = { 0, 0 };
            ProjectionViews[ViewIndex].subImage.imageRect.extent = {
                static_cast<s32>( Swapchain.Width ),
                static_cast<s32>( Swapchain.Height ) };
        }

        Layer.space = m_pImpl->Space;
        Layer.viewCount = ViewCount;
        Layer.views = ProjectionViews.data();
    }

    XrFrameEndInfo FrameEndInfo{ XR_TYPE_FRAME_END_INFO };
    FrameEndInfo.displayTime = FrameState.predictedDisplayTime;
    FrameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    if( Layer.viewCount )
    {
        const XrCompositionLayerBaseHeader* LayerHeader =
            reinterpret_cast<const XrCompositionLayerBaseHeader*>( &Layer );
        FrameEndInfo.layerCount = 1;
        FrameEndInfo.layers = &LayerHeader;
    }
    if( !CheckXr( xrEndFrame( m_pImpl->Session, &FrameEndInfo ),
                  "xrEndFrame", m_LastError, sizeof(m_LastError) ) )
        return FALSE;

    return TRUE;
}

xbool VulkanSession::IsRunning( void ) const
{
    return m_pImpl && m_pImpl->SessionRunning;
}

session_state VulkanSession::GetState( void ) const
{
    return m_State;
}

const char* VulkanSession::GetLastError( void ) const
{
    return m_LastError;
}

} // namespace a51::xr
