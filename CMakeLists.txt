#==========================================================================
#
#  CMakeLists.txt
#
#==========================================================================

cmake_minimum_required( VERSION 3.23 )

# Set CMP0091 before project().
cmake_policy( SET CMP0091 NEW )

# Android Gradle Plugin names its release native configuration
# "RelWithDebInfo".  Keep the engine's configuration model identical to
# desktop builds: Android release is the regular Area 51 Release build.
if( CMAKE_SYSTEM_NAME STREQUAL "Android" AND
    CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo" )
    set( CMAKE_BUILD_TYPE Release CACHE STRING
        "Area 51 build configuration" FORCE )
endif()

project( Area51 LANGUAGES C CXX )

# Android's Gradle variant normally forces CMAKE_BUILD_TYPE to Debug or
# RelWithDebInfo. Allow the Quest profiling build to select the engine's
# optimized development configuration while retaining X_PROFILE and logs.
if( ANDROID AND DEFINED A51_ANDROID_CONFIGURATION AND
    NOT A51_ANDROID_CONFIGURATION STREQUAL "" )
    set( CMAKE_BUILD_TYPE "${A51_ANDROID_CONFIGURATION}" CACHE STRING
         "Area 51 Android configuration override" FORCE )
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(
        $<$<CONFIG:Release>:-g>
    )
endif()

# The Android toolchain does not define compiler flags for the engine's
# custom OptDebug configuration. Keep this profiling build optimized while
# retaining symbols and the X_PROFILE instrumentation.
if( NOT MSVC )
    add_compile_options(
        $<$<CONFIG:OptDebug>:-O2>
        $<$<CONFIG:OptDebug>:-g>
    )
endif()

# Must precede include( A51ThirdParty ), which add_subdirectory()'s the bundled libs.
if( MSVC AND NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY )
    set( CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<OR:$<STREQUAL:$<CONFIG>,Debug>,$<STREQUAL:$<CONFIG>,EDITOR-Debug>>:Debug>"
        CACHE STRING "MSVC runtime library selection" )
endif()

list( APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" )
include( A51Project )
include( A51Platform )
include( A51ThirdParty )

#==========================================================================
#  OPTIONS AND CONFIGURATIONS
#==========================================================================

set( A51_CONFIGURATION_TYPES Debug OptDebug QA Release EDITOR-Debug CACHE STRING "Supported Area 51 configurations" )
if( CMAKE_CONFIGURATION_TYPES )
    set( CMAKE_CONFIGURATION_TYPES "${A51_CONFIGURATION_TYPES}" CACHE STRING "Supported Area 51 configurations" FORCE )
elseif( NOT DEFINED CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "" )
    if( CMAKE_SYSTEM_NAME STREQUAL "Android" )
        set( CMAKE_BUILD_TYPE Release CACHE STRING
            "Default Area 51 configuration for Android builds" FORCE )
    else()
        set( CMAKE_BUILD_TYPE Debug CACHE STRING
            "Default Area 51 configuration for single-config generators" FORCE )
    endif()
endif()

foreach( _a51_configuration IN LISTS A51_CONFIGURATION_TYPES )
    string( TOUPPER "${_a51_configuration}" _a51_configuration_upper )
    foreach( _a51_linker_kind EXE SHARED MODULE )
        set( _a51_linker_flags_variable
            "CMAKE_${_a51_linker_kind}_LINKER_FLAGS_${_a51_configuration_upper}" )
        if( NOT DEFINED "${_a51_linker_flags_variable}" )
            set( "${_a51_linker_flags_variable}" ""
                CACHE STRING "${_a51_configuration} ${_a51_linker_kind} linker flags" )
        endif()
    endforeach()
endforeach()

option( A51_BUILD_SELECTED_APPS "Build the selected application targets" ON )
option( A51_ENABLE_QT "Enable Qt integration for XBMPViewer when Qt6 is available" ON )
option( A51_EXPORT_COMPILE_COMMANDS "Generate compile_commands.json for tooling" ON )
option( A51_BUILD_DXC "Build the bundled DXC host compiler from source" ON )
option( A51_ENABLE_OPENXR "Enable the common OpenXR runtime layer" OFF )
option( A51_GBUFFER_LOW_BW "Use RGBA8 auxiliary G-buffer targets to reduce mobile GPU bandwidth" OFF )
option( A51_DIRECT_RENDER "Use the direct color/depth render path without auxiliary G-buffer targets" OFF )
set( A51_OPENXR_HEADERS_DIR "" CACHE PATH
     "Directory containing the OpenXR headers (openxr/openxr.h)" )
set( A51_OPENXR_LOADER_LIBRARY "" CACHE FILEPATH
     "OpenXR loader library for desktop or Android" )
if( WIN32 )
    set( _a51_default_windows_targets ON )
else()
    set( _a51_default_windows_targets OFF )
endif()
option( A51_BUILD_WINDOWS_TARGETS "Enable targets whose current implementation requires Windows-only dependencies" ${_a51_default_windows_targets} )

if( A51_EXPORT_COMPILE_COMMANDS )
    set( CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL
        "Generate compile_commands.json for tooling" FORCE )
endif()

add_library( a51_build_options INTERFACE )
add_library( a51::build_options ALIAS a51_build_options )

target_compile_features( a51_build_options INTERFACE cxx_std_17 )
if( A51_ENABLE_OPENXR )
    target_compile_definitions( a51_build_options INTERFACE
        A51_ENABLE_OPENXR=1
    )
endif()
if( A51_GBUFFER_LOW_BW )
    target_compile_definitions( a51_build_options INTERFACE
        A51_GBUFFER_LOW_BW=1
    )
endif()
if( A51_DIRECT_RENDER )
    target_compile_definitions( a51_build_options INTERFACE
        A51_DIRECT_RENDER=1
    )
endif()
target_compile_definitions( a51_build_options INTERFACE
    "$<$<PLATFORM_ID:Windows>:NOMINMAX>"
    "$<$<PLATFORM_ID:Windows>:_CRT_SECURE_NO_WARNINGS>"
    "$<$<STREQUAL:$<CONFIG>,EDITOR-Debug>:CONFIG_DEBUG>"
    "$<$<STREQUAL:$<CONFIG>,EDITOR-Debug>:X_EDITOR>"
)

if( MSVC )
    # Allow parallel cl.exe invocations.
    target_compile_options( a51_build_options INTERFACE /FS )

    target_compile_options( a51_build_options INTERFACE
        "$<$<CONFIG:Debug>:/Od>"
        "$<$<STREQUAL:$<CONFIG>,EDITOR-Debug>:/Od>"
        "$<$<CONFIG:OptDebug>:/O2>"
        "$<$<CONFIG:QA>:/O2>"
        "$<$<CONFIG:Release>:/O2>"
        "$<$<CONFIG:Debug>:/Zi>"
        "$<$<STREQUAL:$<CONFIG>,EDITOR-Debug>:/Zi>"
        "$<$<CONFIG:OptDebug>:/Zi>"
        "$<$<CONFIG:QA>:/Zi>"
        "$<$<CONFIG:Release>:/Zi>"
    )
    target_link_options( a51_build_options INTERFACE
        "$<$<CONFIG:Debug>:/DEBUG>"
        "$<$<STREQUAL:$<CONFIG>,EDITOR-Debug>:/DEBUG>"
        "$<$<CONFIG:OptDebug>:/DEBUG>"
        "$<$<CONFIG:QA>:/DEBUG>"
        "$<$<CONFIG:Release>:/DEBUG>"
    )
endif()

if( WIN32 )
    target_link_libraries( a51_build_options INTERFACE
        ws2_32
        user32
        gdi32
        ole32
        comctl32
        comdlg32
    )
endif()

include( CheckIncludeFileCXX )
if( WIN32 )
    check_include_file_cxx( "windows.h" A51_HAS_WINDOWS_H )
    if( NOT A51_HAS_WINDOWS_H )
        message( WARNING "The active Windows toolchain cannot include windows.h. Configure succeeds, but Windows targets require a Windows SDK toolchain before they can be built." )
    endif()
endif()

if( A51_ENABLE_QT )
    find_package( Qt6 COMPONENTS Core Gui Widgets QUIET )
endif()

if( A51_ENABLE_OPENXR )
    if( NOT CMAKE_SYSTEM_NAME MATCHES "^(Windows|Linux|Android)$" )
        message( FATAL_ERROR
            "A51_ENABLE_OPENXR requires a Windows, Linux, or Android target; "
            "CMAKE_SYSTEM_NAME is '${CMAKE_SYSTEM_NAME}'." )
    endif()

    if( CMAKE_SYSTEM_NAME STREQUAL "Android" )
        if( CMAKE_ANDROID_ARCH_ABI AND
            NOT CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a" )
            message( FATAL_ERROR
                "A51_ENABLE_OPENXR on Android currently requires arm64-v8a; "
                "CMAKE_ANDROID_ARCH_ABI is '${CMAKE_ANDROID_ARCH_ABI}'." )
        endif()

        # The Khronos Android AAR exposes both OpenXR::headers and
        # OpenXR::openxr_loader through Gradle Prefab. Do not require a
        # filesystem path here: the prefab package is materialized by the
        # Android Gradle Plugin before the native CMake configure step.
        message( STATUS "OpenXR Android dependency will be provided by Gradle Prefab." )
    else()
        if( NOT A51_OPENXR_HEADERS_DIR )
            find_path( A51_OPENXR_HEADERS_DIR
                NAMES openxr/openxr.h
                PATH_SUFFIXES include )
        endif()

        if( NOT A51_OPENXR_HEADERS_DIR OR
            NOT EXISTS "${A51_OPENXR_HEADERS_DIR}/openxr/openxr.h" )
            message( FATAL_ERROR
                "A51_ENABLE_OPENXR is ON, but OpenXR headers were not found. "
                "Pass -DA51_OPENXR_HEADERS_DIR=/path/to/include." )
        endif()

        if( NOT A51_OPENXR_LOADER_LIBRARY )
            find_library( A51_OPENXR_LOADER_LIBRARY
                NAMES openxr_loader OpenXR_loader openxr_loader_mt )
        endif()

        if( NOT A51_OPENXR_LOADER_LIBRARY )
            message( FATAL_ERROR
                "A51_ENABLE_OPENXR is ON, but the OpenXR loader was not found. "
                "Pass -DA51_OPENXR_LOADER_LIBRARY=/path/to/openxr_loader." )
        endif()

        set( A51_OPENXR_HEADERS_DIR "${A51_OPENXR_HEADERS_DIR}"
             CACHE PATH "Directory containing the OpenXR headers" FORCE )
        set( A51_OPENXR_LOADER_LIBRARY "${A51_OPENXR_LOADER_LIBRARY}"
             CACHE FILEPATH "OpenXR loader library" FORCE )
    endif()
endif()

#==========================================================================
#  PROJECT GRAPH
#==========================================================================

add_subdirectory( xCore )

# ShaderTool is a host-side tool.  Build DXC from the bundled source tree on
# Linux and Windows instead of relying on a prebuilt compiler library.  DXC is
# deliberately not added to Android builds: the Android target only consumes
# the SPIR-V blobs produced by this host tool.
if( A51_BUILD_SELECTED_APPS AND A51_BUILD_DXC AND
    ( CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
      ( CMAKE_SYSTEM_NAME STREQUAL "Windows" AND A51_BUILD_WINDOWS_TARGETS ) ) )
    set( A51_DXC_SOURCE_DIR
         "${CMAKE_CURRENT_SOURCE_DIR}/xCore/3rdParty/DXC"
         CACHE PATH "Bundled DirectX Shader Compiler source directory" )

    if( NOT EXISTS "${A51_DXC_SOURCE_DIR}/CMakeLists.txt" )
        message( FATAL_ERROR
            "A51_BUILD_DXC is enabled, but DXC was not found at "
            "${A51_DXC_SOURCE_DIR}." )
    endif()

    set( _a51_dxc_external_dir "${A51_DXC_SOURCE_DIR}/external" )
    if( NOT WIN32 AND
        NOT EXISTS "${_a51_dxc_external_dir}/DirectX-Headers/include/directx/d3d12shader.h" )
        message( FATAL_ERROR
            "DXC requires the DirectX-Headers dependency at "
            "${_a51_dxc_external_dir}/DirectX-Headers. "
            "Populate DXC external dependencies before configuring Area51." )
    endif()
    if( NOT EXISTS "${_a51_dxc_external_dir}/SPIRV-Headers/include/spirv/unified1/spirv.hpp" OR
        NOT EXISTS "${_a51_dxc_external_dir}/SPIRV-Tools/CMakeLists.txt" )
        message( FATAL_ERROR
            "DXC SPIR-V dependencies are missing under ${_a51_dxc_external_dir}. "
            "Populate SPIRV-Headers and SPIRV-Tools before configuring Area51." )
    endif()

    # DXC is a large LLVM project.  Only its compiler targets are needed by
    # ShaderTool, so keep the generated graph and default build small.
    set( ENABLE_SPIRV_CODEGEN ON CACHE BOOL "Enable DXC SPIR-V code generation" FORCE )
    set( LLVM_TARGETS_TO_BUILD None CACHE STRING "LLVM targets used by DXC" FORCE )
    set( LLVM_ENABLE_EH ON CACHE BOOL "Enable DXC exception handling" FORCE )
    set( LLVM_ENABLE_RTTI ON CACHE BOOL "Enable DXC RTTI" FORCE )
    set( CLANG_ENABLE_ARCMT OFF CACHE BOOL "Build unused Clang ARCMT support" FORCE )
    set( CLANG_ENABLE_STATIC_ANALYZER OFF CACHE BOOL "Build unused Clang static analyzer" FORCE )
    set( SPIRV_BUILD_TESTS OFF CACHE BOOL "Build DXC SPIR-V tests" FORCE )
    set( HLSL_INCLUDE_TESTS OFF CACHE BOOL "Generate DXC HLSL tests" FORCE )
    set( LLVM_INCLUDE_TESTS OFF CACHE BOOL "Generate LLVM tests" FORCE )
    set( LLVM_BUILD_TESTS OFF CACHE BOOL "Build LLVM tests" FORCE )
    set( LLVM_INCLUDE_EXAMPLES OFF CACHE BOOL "Generate LLVM examples" FORCE )
    set( LLVM_BUILD_EXAMPLES OFF CACHE BOOL "Build LLVM examples" FORCE )
    set( LLVM_INCLUDE_DOCS OFF CACHE BOOL "Generate LLVM documentation" FORCE )
    set( LLVM_BUILD_DOCS OFF CACHE BOOL "Build LLVM documentation" FORCE )
    set( LLVM_INCLUDE_UTILS OFF CACHE BOOL "Generate LLVM utilities" FORCE )
    set( LLVM_DISTRIBUTION_COMPONENTS "" CACHE STRING "DXC distribution components" FORCE )
    set( DIRECTX_HEADER_INCLUDE_DIR
         "${_a51_dxc_external_dir}/DirectX-Headers/include"
         CACHE PATH "DirectX-Headers include directory" FORCE )

    add_subdirectory( "${A51_DXC_SOURCE_DIR}"
                      "${CMAKE_BINARY_DIR}/DXC"
                      EXCLUDE_FROM_ALL )
endif()

add_subdirectory( Support )
add_subdirectory( Apps )

set( _a51_tool_targets
    xbmptool
    Art2Code
    dfsTool
    XBMPViewer
    SoundPackager
    ShaderTool
    AnimCompiler
    BitmapCompiler
    DecalCompiler
    FontBuilder
    stringTool
)

set( _a51_configured_tool_targets )
foreach( _a51_tool_target IN LISTS _a51_tool_targets )
    if( TARGET ${_a51_tool_target} )
        list( APPEND _a51_configured_tool_targets ${_a51_tool_target} )
    endif()
endforeach()

if( _a51_configured_tool_targets )
    add_custom_target( a51_tools )
    add_dependencies( a51_tools ${_a51_configured_tool_targets} )
endif()

#==========================================================================
#  PLATFORM DEPENDENCIES
#==========================================================================

if( TARGET Entropy )
    target_compile_definitions( Entropy PRIVATE ENTROPY_RENDER_SDL )
endif()

if( TARGET Script_mgr )
    target_include_directories( Script_mgr PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/xCore/3rdParty/Lua/5.5.0"
    )
endif()

if( TARGET ShaderTool )
    if( EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/xCore/3rdParty/DXC/include/dxc/dxcapi.h" )
        target_include_directories( ShaderTool PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/xCore/3rdParty/DXC/include/dxc"
        )
    else()
        target_include_directories( ShaderTool PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/xCore/3rdParty/DXC/inc"
        )
    endif()

    if( TARGET dxcompiler )
        target_compile_definitions( ShaderTool PRIVATE DXC_USE_DIRECTX_HEADERS=1 )
        target_include_directories( ShaderTool PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/xCore/3rdParty/DXC/external/DirectX-Headers/include/directx"
            "${CMAKE_CURRENT_SOURCE_DIR}/xCore/3rdParty/DXC/external/DirectX-Headers/include/wsl/stubs"
        )
    endif()

    if( TARGET dxcompiler )
        target_link_libraries( ShaderTool PRIVATE dxcompiler )
        if( UNIX )
            set_property( TARGET ShaderTool APPEND PROPERTY BUILD_RPATH
                          "${CMAKE_BINARY_DIR}/lib" )
        endif()
    endif()
endif()

message( STATUS "Configured the native Area 51 CMake target graph." )
