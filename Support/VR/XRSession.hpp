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
    f32 HeadPosition[3];
    f32 Orientation[4];
    f32 FovLeft;
    f32 FovRight;
    f32 FovUp;
    f32 FovDown;
};

/* Controller poses are expressed relative to the session's yaw-aligned
 * tracking origin, in OpenXR meters and coordinate convention. */
struct controller_pose
{
    f32 Position[3];
    f32 Orientation[4];
    xbool Valid;
};

struct body_joint_pose
{
    f32   Position[3];
    f32   Orientation[4];
    xbool PositionValid;
    xbool OrientationValid;
};

enum : u32
{
    BODY_JOINT_ROOT = 0,
    BODY_JOINT_HIPS,
    BODY_JOINT_SPINE_LOWER,
    BODY_JOINT_SPINE_MIDDLE,
    BODY_JOINT_SPINE_UPPER,
    BODY_JOINT_CHEST,
    BODY_JOINT_NECK,
    BODY_JOINT_HEAD,
    BODY_JOINT_LEFT_SHOULDER,
    BODY_JOINT_LEFT_SCAPULA,
    BODY_JOINT_LEFT_UPPER_ARM,
    BODY_JOINT_LEFT_LOWER_ARM,
    BODY_JOINT_LEFT_WRIST_TWIST,
    BODY_JOINT_RIGHT_SHOULDER,
    BODY_JOINT_RIGHT_SCAPULA,
    BODY_JOINT_RIGHT_UPPER_ARM,
    BODY_JOINT_RIGHT_LOWER_ARM,
    BODY_JOINT_RIGHT_WRIST_TWIST,
    BODY_JOINT_UPPER_BODY_COUNT,
    BODY_JOINT_LEFT_UPPER_LEG = 70,
    BODY_JOINT_LEFT_LOWER_LEG,
    BODY_JOINT_LEFT_ANKLE_TWIST,
    BODY_JOINT_LEFT_ANKLE,
    BODY_JOINT_LEFT_SUBTALAR,
    BODY_JOINT_LEFT_TRANSVERSE,
    BODY_JOINT_LEFT_FOOT_BALL,
    BODY_JOINT_RIGHT_UPPER_LEG,
    BODY_JOINT_RIGHT_LOWER_LEG,
    BODY_JOINT_RIGHT_ANKLE_TWIST,
    BODY_JOINT_RIGHT_ANKLE,
    BODY_JOINT_RIGHT_SUBTALAR,
    BODY_JOINT_RIGHT_TRANSVERSE,
    BODY_JOINT_RIGHT_FOOT_BALL,
    BODY_JOINT_FULL_BODY_COUNT = 84
};

struct body_tracking_pose
{
    body_joint_pose Joints[BODY_JOINT_FULL_BODY_COUNT];
    f32             Confidence;
    xbool           Valid;
    xbool           HighFidelity;
    xbool           FullBody;
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
    void            CaptureInput   ( ::input_event_buffer& Events,
                                      xbool InGameplay );
    xbool           GetEyeView      ( u32 Eye, eye_view& View ) const;
    xbool           GetControllerPose( u32 Hand, controller_pose& Pose ) const;
    xbool           GetBodyTrackingPose( body_tracking_pose& Pose );
    xbool           GetControllerFingerInput( u32 Hand, f32& Grip,
                                              f32& Trigger ) const;
    xbool           GetRightControllerFaceButtons( xbool& A,
                                                    xbool& B ) const;
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
    xbool           GetAcquiredImage( u32 Eye, void*& Image ) const;
    xbool           UsesMutableSrgbImages( void ) const;
    xbool           UsesArraySwapchain( void ) const;
    xbool           ShouldRenderFrame( void ) const;
    xbool           UsesBgraSwapchain( void ) const;

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
