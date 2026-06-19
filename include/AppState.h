#pragma once

#include <Arduino.h>

struct AppState {
  unsigned long lastReadMillis = 0;
  unsigned long lastWifiCheckMillis = 0;
  unsigned long lastMqttCheckMillis = 0;
  unsigned long lastMqttPublishMillis = 0;

  uint32_t totalReads = 0;
  uint32_t validReads = 0;
  uint32_t failedReads = 0;

  uint32_t mqttPublishCount = 0;
  uint32_t mqttFailCount = 0;

  float lastValidTemperatureC = NAN;
  float lastRawTemperatureC = NAN;
  uint32_t consecutiveSensorErrors = 0;
  String sensorStatus = "UNKNOWN";
  String lastErrorMessage = "Encara no s'ha fet cap lectura";

  bool mqttDiscoveryPublished = false;
  bool mqttRestartRequested = false;
  bool mqttDiscoveryRequested = false;
  bool mqttConfigStatePublishRequested = false;
  bool mqttReconfigureRequested = false;

  bool otaInProgress = false;
  bool otaSuccess = false;
  String otaLastMessage = "OTA encara no utilitzada";

  bool githubUpdateChecked = false;
  bool githubUpdateAvailable = false;
  String githubUpdateMessage = "Encara no comprovat";
  String githubUpdateVersion = "";
  String githubUpdateSha = "";
  String githubUpdateDate = "";
  String githubFirmwareUrl = "";

  bool hardwareReady = false;
  bool buttonPressed = false;
  unsigned long buttonPressDurationMs = 0;
  String lastHardwareAction = "Cap accio fisica encara";
};

extern AppState appState;
