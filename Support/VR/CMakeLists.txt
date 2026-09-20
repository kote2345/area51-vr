set( A51_OPENXR_PLATFORM_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/XRRuntime_Desktop.cpp"
)

if( CMAKE_SYSTEM_NAME STREQUAL "Android" )
    set( A51_OPENXR_PLATFORM_SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/XRRuntime_Android.cpp"
    )
endif()

set( A51_OPENXR_DEPENDENCIES x_files )
set( A51_OPENXR_INCLUDE_DIRECTORIES )

if( CMAKE_SYSTEM_NAME STREQUAL "Android" )
    find_package( OpenXR CONFIG REQUIRED )

    list( APPEND A51_OPENXR_INCLUDE_DIRECTORIES
        "${CMAKE_SOURCE_DIR}/xCore/3rdParty/SDL3/include"
    )

    find_library( A51_VULKAN_LIBRARY vulkan REQUIRED )

    if( NOT TARGET OpenXR::headers OR NOT TARGET OpenXR::openxr_loader )
        message( FATAL_ERROR
            "The Khronos OpenXR Android prefab package did not expose "
            "OpenXR::headers and OpenXR::openxr_loader." )
    endif()

    list( APPEND A51_OPENXR_DEPENDENCIES
        OpenXR::headers
        OpenXR::openxr_loader
        ${A51_VULKAN_LIBRARY}
    )
else()
    find_package( Vulkan REQUIRED )

    list( APPEND A51_OPENXR_DEPENDENCIES
        ${A51_OPENXR_LOADER_LIBRARY}
        Vulkan::Vulkan
    )
    list( APPEND A51_OPENXR_INCLUDE_DIRECTORIES
        "${A51_OPENXR_HEADERS_DIR}"
    )
endif()

a51_add_project( A51OpenXR STATIC
    SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/XRRuntime.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/XRRuntime.hpp"
        "${CMAKE_CURRENT_LIST_DIR}/XRSession.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/XRSession.hpp"
        ${A51_OPENXR_PLATFORM_SOURCES}
    DEPENDENCIES
        ${A51_OPENXR_DEPENDENCIES}
    PRIVATE_INCLUDE_DIRECTORIES
        ${A51_OPENXR_INCLUDE_DIRECTORIES}
)

target_compile_definitions( A51OpenXR PRIVATE A51_ENABLE_OPENXR=1 )

if( CMAKE_SYSTEM_NAME STREQUAL "Android" )
    target_compile_definitions( A51OpenXR PRIVATE XR_USE_PLATFORM_ANDROID=1 )
endif()
