#pragma once

// Public builds have no OTA password. To keep one on your own panel, create
// include/ota_secret_local.h (gitignored) with:
//   #define OTA_PASSWORD "your-password"
// and the same value in platformio_override.ini as --auth.
#if defined(__has_include)
#  if __has_include("ota_secret_local.h")
#    include "ota_secret_local.h"
#  endif
#endif
#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""
#endif
