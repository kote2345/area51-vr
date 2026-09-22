//==============================================================================
//
//  XRSession.hpp
//
//==============================================================================

#ifndef A51_XR_SESSION_HPP
#define A51_XR_SESSION_HPP

#include "x_types.hpp"

class input_event_buffer;

namespace a51::xr
{

class Runtime;

struct vulkan_device_info
{
    void* Instance;
    void* PhysicalDevice;
    void* Device;
    void* Queue;
    u32   QueueFamilyIndex;
    xbool MultiviewEnabled;
};

struct vulkan_frame_info
{
    void* CommandBuffer;
    void* SourceImage;
    u32   Width;
    u32   Height;
};

struct eye_view
{
    f32 Position[3];
    f32 Orientation[4];
    f32 FovLeft;
    f32 FovRight;
    f32 FovUp;
    f32 FovDown;
};

enum class session_state : u8
{
    Uninitialized,
    Ready,
    Running,
    Stopping,
    Exiting,
    LossPending,
    Stopped,
    Failed,
};

class VulkanSession
{
public:
                    VulkanSession   ( void );
                    ~VulkanSession  ( void );

    xbool           Initialize      ( Runtime& Runtime );
    xbool           Initialize      ( Runtime& Runtime,
                                      const vulkan_device_info& DeviceInfo );
    void            Shutdown        ( void );
    xbool           PollEvents      ( void );
    xbool           BeginFrame      ( void );
    void            CaptureInput   ( ::input_event_buffer& Events );
    xbool           GetEyeView      ( u32 Eye, eye_view& View ) const;
    xbool           RenderTestFrame ( void );
    xbool           PrepareFrame   ( const vulkan_frame_info& FrameInfo );
    xbool           PrepareQuadFrame( const vulkan_frame_info& FrameInfo,
                                      f32 WidthMeters = 1.6f,
                                      f32 HeightMeters = 1.2f,
                                      f32 DistanceMeters = 1.8f );
    xbool           PrepareStereoFrame( const vulkan_frame_info& LeftFrame,
                                        const vulkan_frame_info& RightFrame );
    xbool           PrepareMultiviewFrame( const vulkan_frame_info& ArrayFrame );
    xbool           FinishFrame    ( void );
    void            CancelFrame    ( void );
    xbool           GetVulkanDeviceInfo( vulkan_device_info& DeviceInfo ) const;
    xbool           SupportsMultiview( void ) const;
    xbool           GetRecommendedRenderSize( u32& Width, u32& Height ) const;

    xbool           IsRunning       ( void ) const;
    session_state   GetState        ( void ) const;
    const char*     GetLastError    ( void ) const;

private:
                    VulkanSession   ( const VulkanSession& ) = delete;
    VulkanSession&   operator=      ( const VulkanSession& ) = delete;

    struct Impl;
    xbool           InitializeInternal( Runtime& Runtime );
    xbool           PrepareStereoFrameInternal( const vulkan_frame_info& LeftFrame,
                                                const vulkan_frame_info& RightFrame,
                                                xbool bArraySource );
    Impl*           m_pImpl;
    session_state   m_State;
    char            m_LastError[256];
    vulkan_device_info m_ExternalDevice;
    xbool           m_bUseExternalDevice;
};

} // namespace a51::xr

#endif // A51_XR_SESSION_HPP
