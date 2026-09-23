//==============================================================================
//
//  sdleng_vulkan.hpp
//
//==============================================================================

#ifndef SDLENG_VULKAN_HPP
#define SDLENG_VULKAN_HPP

#include "x_types.hpp"

struct rtarget;

struct sdleng_vulkan_device_info
{
    void* Instance;
    void* PhysicalDevice;
    void* Device;
    void* Queue;
    u32   QueueFamilyIndex;
    u32   RenderWidth;
    u32   RenderHeight;
    xbool MultiviewEnabled;
    xbool DirectRenderEnabled;
    xbool DirectRenderBgra;
    xbool DirectRenderArray;
};

struct sdleng_vulkan_frame_info
{
    void* CommandBuffer;
    void* SourceImage;
    u32   Width;
    u32   Height;
};

xbool sdleng_GetVulkanDeviceInfo( sdleng_vulkan_device_info& Info );
xbool sdleng_GetVulkanFrameInfo ( sdleng_vulkan_frame_info&  Info );
xbool sdleng_GetVulkanFrameInfoForEye( u32 Eye,
                                       sdleng_vulkan_frame_info& Info );
xbool sdleng_GetVulkanFrameInfoForTarget( const rtarget& Target,
                                         sdleng_vulkan_frame_info& Info );
xbool sdleng_SetVulkanRenderEye( u32 Eye );
xbool sdleng_SetVulkanEyeImage( u32 Eye, void* VkImage );
xbool sdleng_SetVulkanExternalDevice( const sdleng_vulkan_device_info& Info );
void  sdleng_ClearVulkanExternalDevice( void );
xbool sdleng_HasVulkanExternalDevice( void );
xbool sdleng_VulkanMultiviewEnabled( void );
xbool sdleng_VulkanArraySwapchainEnabled( void );

#endif // SDLENG_VULKAN_HPP
