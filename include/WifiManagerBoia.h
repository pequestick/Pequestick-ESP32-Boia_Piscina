#pragma once

#include <Arduino.h>

void connectWifi();
void checkWifiConnection();
void printWifiStatus();

bool isWifiConnected();
bool isWifiApActive();
String wifiStatusText();
String wifiModeText();
String wifiStaIpText();
String wifiApIpText();
String wifiConfiguredSsidText();

void restartWifiWithCurrentConfig();
void resetWifiAndRestart();
