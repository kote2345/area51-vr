// Compile-time switches for isolating Quest GPU cost. Change one option and
// rebuild to compare its contribution in headset.
#pragma once

#include "x_types.hpp"

namespace gpu_test
{
#if defined(TARGET_ANDROID)
    static constexpr xbool DisableDynamicShadows = TRUE;
    static constexpr xbool DisableDecals         = TRUE;
    static constexpr xbool DisableProjectedShadows = TRUE;
    static constexpr xbool DisableProjectedLights  = TRUE;
    static constexpr xbool DisableDynamicLights    = TRUE;
    static constexpr xbool DisableFx               = TRUE;
    static constexpr xbool DisableTracers          = TRUE;
    static constexpr xbool DisableFog              = TRUE;
    static constexpr xbool DisableGlow             = TRUE;
    static constexpr xbool DisableMipFilters       = TRUE;
    static constexpr xbool DisableMotionBlur       = TRUE;
    static constexpr xbool DisableRadialBlur        = TRUE;
    static constexpr xbool DisableScreenWarps       = TRUE;
    static constexpr xbool DisableNoise             = TRUE;
    static constexpr xbool DisableDistortion        = TRUE;
    static constexpr xbool DisableScreenFade        = TRUE;
#else
    static constexpr xbool DisableDynamicShadows = FALSE;
    static constexpr xbool DisableDecals         = FALSE;
    static constexpr xbool DisableProjectedShadows = FALSE;
    static constexpr xbool DisableProjectedLights  = FALSE;
    static constexpr xbool DisableDynamicLights    = FALSE;
    static constexpr xbool DisableFx               = FALSE;
    static constexpr xbool DisableTracers          = FALSE;
    static constexpr xbool DisableFog              = FALSE;
    static constexpr xbool DisableGlow             = FALSE;
    static constexpr xbool DisableMipFilters       = FALSE;
    static constexpr xbool DisableMotionBlur       = FALSE;
    static constexpr xbool DisableRadialBlur        = FALSE;
    static constexpr xbool DisableScreenWarps       = FALSE;
    static constexpr xbool DisableNoise             = FALSE;
    static constexpr xbool DisableDistortion        = FALSE;
    static constexpr xbool DisableScreenFade        = FALSE;
#endif
}
