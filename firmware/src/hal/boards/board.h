#pragma once

// Board-specific hardware definitions.
// The PLATFORM_* macro is set by platformio.ini per-env build flag.

#if defined(PLATFORM_TWATCH)
  #include "twatch_ultra.h"
#elif defined(PLATFORM_TDECK)
  #include "tdeck_plus.h"
#else
  #error "No board platform defined - expected PLATFORM_TDECK or PLATFORM_TWATCH"
#endif
