#include "networking.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <WiFiMulti.h>

#include "elsa_config.h"
#include "ota_config.h"
#include "wifi_config.h"

static bool s_wifiConnected = false;

#if defined(WIFI_MULTI_ENABLED) && WIFI_MULTI_ENABLED
static WiFiMulti s_wifiMulti;
static bool s_wifiMultiConfigured = false;
#endif

#if ENABLE_WIFI_KEEPALIVE
static uint32_t s_lastWifiCheckMs = 0;
#endif

bool setupWiFi() {
#if (ENABLE_WEB_TELEMETRY || ENABLE_OTA)
#if defined(WIFI_MULTI_ENABLED) && WIFI_MULTI_ENABLED
  if (WIFI_NETWORK_COUNT <= 0) {
    Serial.println("WiFi disabled (WIFI_NETWORK_COUNT is 0)");
    s_wifiConnected = false;
    return s_wifiConnected;
  }

  WiFi.mode(WIFI_STA);
  if (!s_wifiMultiConfigured) {
    for (int i = 0; i < WIFI_NETWORK_COUNT; i++) {
      if (WIFI_SSIDS[i] && strlen(WIFI_SSIDS[i]) > 0) {
        s_wifiMulti.addAP(WIFI_SSIDS[i], WIFI_PASSWORDS[i]);
      }
    }
    s_wifiMultiConfigured = true;
  }

  const uint32_t start = millis();
  while (s_wifiMulti.run() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.print("WiFi connected: ");
    Serial.print(WiFi.SSID());
    Serial.print(" @ ");
    Serial.println(WiFi.localIP());
    s_wifiConnected = true;
    return s_wifiConnected;
  }

  Serial.println("");
  Serial.println("WiFi connection failed");
  s_wifiConnected = false;
  return s_wifiConnected;
#else
  if (strlen(WIFI_SSID) == 0) {
    Serial.println("WiFi disabled (WIFI_SSID is empty)");
    s_wifiConnected = false;
    return s_wifiConnected;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.print("WiFi connected: ");
    Serial.println(WiFi.localIP());
    s_wifiConnected = true;
    return s_wifiConnected;
  }

  Serial.println("");
  Serial.println("WiFi connection failed");
  s_wifiConnected = false;
  return s_wifiConnected;
#endif
#else
  s_wifiConnected = false;
  return s_wifiConnected;
#endif
}

void pollWiFi() {
#if (ENABLE_WEB_TELEMETRY || ENABLE_OTA)
#if ENABLE_WIFI_KEEPALIVE
  const uint32_t now = millis();
  if ((now - s_lastWifiCheckMs) < WIFI_KEEPALIVE_INTERVAL_MS) return;
  s_lastWifiCheckMs = now;

#if defined(WIFI_MULTI_ENABLED) && WIFI_MULTI_ENABLED
  if (WIFI_NETWORK_COUNT <= 0) return;
  const wl_status_t status = s_wifiMulti.run();
  const bool connected = (status == WL_CONNECTED);
#else
  if (strlen(WIFI_SSID) == 0) return;
  WiFi.reconnect();
  const bool connected = (WiFi.status() == WL_CONNECTED);
#endif

  if (connected && !s_wifiConnected) {
    Serial.println("WiFi reconnected");
  }
  s_wifiConnected = connected;
#else
  (void)0;
#endif
#else
  (void)0;
#endif
}

bool isWifiConnected() {
  return s_wifiConnected;
}

bool* wifiConnectedFlag() {
  return &s_wifiConnected;
}

void setupOta() {
#if ENABLE_OTA
  if (!s_wifiConnected) return;
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  if (OTA_PASSWORD[0] != '\0') {
    ArduinoOTA.setPassword(OTA_PASSWORD);
  }
  ArduinoOTA.begin();
  Serial.print("OTA ready: ");
  Serial.print(OTA_HOSTNAME);
  Serial.print(".local (");
  Serial.print(WiFi.localIP());
  Serial.println(")");
#endif
}

void handleOta() {
#if ENABLE_OTA
  if (s_wifiConnected) {
    ArduinoOTA.handle();
  }
#endif
}
