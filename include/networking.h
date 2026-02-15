#pragma once

bool setupWiFi();
void pollWiFi();
bool isWifiConnected();
bool* wifiConnectedFlag();
void setupOta();
void handleOta();
