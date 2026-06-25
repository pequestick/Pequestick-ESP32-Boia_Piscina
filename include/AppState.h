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

  unsigned long internalEnvLastReadMillis = 0;
  uint32_t internalEnvTotalReads = 0;
  uint32_t internalEnvValidReads = 0;
  uint32_t internalEnvFailedReads = 0;
  uint32_t internalEnvConsecutiveErrors = 0;
  float lastInternalTemperatureC = NAN;
  float lastInternalHumidityPercent = NAN;
  String internalEnvStatus = "UNKNOWN";
  String internalEnvLastError = "Encara no s'ha fet cap lectura";

  unsigned long batteryLastReadMillis = 0;
  uint32_t batteryTotalReads = 0;
  uint32_t batteryValidReads = 0;
  uint32_t batteryFailedReads = 0;
  float lastBatteryVoltage = NAN;
  float lastBatteryPercent = NAN;
  float lastBatteryAdcMilliVolts = NAN;
  float lastBatteryRawAdc = NAN;
  String batteryStatus = "UNKNOWN";
  String batteryLastError = "Encara no s'ha fet cap lectura";

  bool sdEnabled = false;
  bool sdMounted = false;
  bool sdLastOperationOk = false;
  String sdStatus = "UNKNOWN";
  String sdCardType = "Sense dades";
  String sdLastError = "SD encara no inicialitzada";
  String sdHistoryPath = "/boia/history/boot.csv";
  String sdDailyStatsPath = "/boia/stats/daily_snapshots.csv";
  String sdSystemLogPath = "/boia/logs/boot.log";
  String sdPendingMqttPath = "/boia/mqtt/pending.jsonl";
  String sdLastHistoryLine = "";
  String sdStatsDay = "boot";
  uint64_t sdTotalBytes = 0;
  uint64_t sdUsedBytes = 0;
  uint64_t sdFreeBytes = 0;
  uint32_t sdHistoryWriteCount = 0;
  uint32_t sdHistoryWriteFailCount = 0;
  uint32_t sdDailyRecordCount = 0;
  uint32_t sdDailyErrorCount = 0;
  uint32_t sdMqttPendingCount = 0;
  uint32_t sdMqttFlushCount = 0;
  float sdDailyTempMin = NAN;
  float sdDailyTempMax = NAN;
  float sdDailyTempAvg = NAN;
  float sdDailyBatteryMin = NAN;
  float sdDailyBatteryMax = NAN;
  float sdDailyBatteryAvg = NAN;
  unsigned long sdLastCheckMillis = 0;
  unsigned long sdLastWriteMillis = 0;

  bool mqttDiscoveryPublished = false;
  bool mqttRestartRequested = false;
  bool mqttDiscoveryRequested = false;
  bool mqttConfigStatePublishRequested = false;
  bool mqttReconfigureRequested = false;

  bool otaInProgress = false;
  bool otaSuccess = false;
  String otaLastMessage = "OTA encara no utilitzada";
  String otaLog = "[0s] OTA log inicialitzat. Encara no s'ha iniciat cap actualitzacio.\n";
  uint32_t otaLogSeq = 0;
  String otaProgressSource = "cap";
  String otaProgressPhase = "espera";
  uint32_t otaProgressBytes = 0;
  uint32_t otaProgressTotal = 0;
  uint8_t otaProgressPercent = 0;
  unsigned long otaProgressMillis = 0;

  bool githubUpdateChecked = false;
  bool githubUpdateOk = false;
  bool githubUpdateAvailable = false;
  bool githubRemoteOlder = false;
  bool githubRemoteSameVersion = false;
  int githubLastHttpCode = 0;
  unsigned long githubLastCheckMillis = 0;
  String githubUpdateMessage = "Encara no comprovat";
  String githubUpdateVersion = "";
  String githubUpdateSha = "";
  String githubUpdateDate = "";
  String githubFirmwareUrl = "";
  String githubFirmwareSha256 = "";
  uint32_t githubFirmwareSize = 0;
  String githubUpdateDetails = "Prem Comprovar actualitzacio per llegir el manifest publicat a GitHub.";

  bool internetCheckDone = false;
  bool internetCheckOk = false;
  int internetHttpCode = 0;
  unsigned long internetLastCheckMillis = 0;
  String internetCheckMessage = "Encara no comprovat";
  String internetCheckDetails = "";
  String internetResolvedIp = "";

  bool hardwareReady = false;
  bool buttonPressed = false;
  unsigned long buttonPressDurationMs = 0;
  String lastHardwareAction = "Cap accio fisica encara";
};

extern AppState appState;
