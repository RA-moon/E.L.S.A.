#pragma once

// Optional OTA secrets (kept out of git).
#if defined(__has_include)
#if __has_include("ota_secrets.h")
#include "ota_secrets.h"
#endif
#endif

#ifndef OTA_HOSTNAME
#define OTA_HOSTNAME "E.L.S.A."
#endif

#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""
#endif
