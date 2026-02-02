#pragma once

// Single-network fallback (optional)
// #define WIFI_SSID "YOUR_WIFI_SSID"
// #define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// Multi-network list (preferred)
#define WIFI_MULTI_ENABLED 1
#define WIFI_NETWORK_COUNT 3
static const char* WIFI_SSIDS[] = {
  "YOUR_WIFI_SSID_1",
  "YOUR_WIFI_SSID_2",
  "YOUR_WIFI_SSID_3",
};

static const char* WIFI_PASSWORDS[] = {
  "YOUR_WIFI_PASSWORD_1",
  "YOUR_WIFI_PASSWORD_2",
  "YOUR_WIFI_PASSWORD_3",
};

// OTA credentials
#define OTA_HOSTNAME "YOUR_DEVICE_NAME"
#define OTA_PASSWORD "YOUR_OTA_PASSWORD"
