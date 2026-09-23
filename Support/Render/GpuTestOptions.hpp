// Compile-time switches for isolating Quest GPU cost. Change one option and
// rebuild to compare its contribution in headset.
#pragma once

#include "x_types.hpp"

namespace gpu_test
{
#if defined(TARGET_ANDROID)
    static constexpr xbool DisableDynamicShadows   = FALSE;
    static constexpr xbool DisableDecals            = FALSE;
    static constexpr xbool DisableProjectedShadows  = FALSE;
    static constexpr xbool DisableProjectedLights   = FALSE;
    static constexpr xbool DisableDynamicLights     = FALSE;
    static constexpr xbool DisableFx                = FALSE;
    static constexpr xbool DisableTracers           = FALSE;
    static constexpr xbool DisableFog               = FALSE;
    static constexpr xbool DisableGlow              = FALSE;
    static constexpr xbool DisableMipFilters        = FALSE;
    static constexpr xbool DisableMotionBlur        = FALSE;
    static constexpr xbool DisableRadialBlur        = FALSE;
    static constexpr xbool DisableScreenWarps       = FALSE;
    static constexpr xbool DisableNoise             = FALSE;
    static constexpr xbool DisableDistortion        = FALSE;
    static constexpr xbool DisableScreenFade        = FALSE;
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
