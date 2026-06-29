#include "SdManager.h"

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <time.h>
#include <esp_system.h>
#include <esp_sleep.h>

#include "AppConfig.h"
#include "AppState.h"
#include "Utils.h"

static bool sdBegun = false;
static bool timeSyncStarted = false;
static bool bootHistoryWithTimeAppended = false;
static bool bootLogWithTimeAppended = false;
static unsigned long lastSdRefreshMillis = 0;
static unsigned long lastSdStructureCheckMillis = 0;
static const unsigned long SD_REFRESH_INTERVAL_MS = 30000;
static const unsigned long SD_STRUCTURE_CHECK_INTERVAL_MS = 300000;
static const size_t SD_VIEW_MAX_BYTES_DEFAULT = 16384;

static String runtimeStatsDay = "";
static uint32_t runtimeStatsRecords = 0;
static uint32_t runtimeStatsErrors = 0;
static float runtimeTempMin = NAN;
static float runtimeTempMax = NAN;
static double runtimeTempSum = 0.0;
static uint32_t runtimeTempCount = 0;
static float runtimeBatteryMin = NAN;
static float runtimeBatteryMax = NAN;
static double runtimeBatterySum = 0.0;
static uint32_t runtimeBatteryCount = 0;

bool isSdEnabled() {
  return SD_CARD_ENABLED;
}

bool isSdMounted() {
  return isSdEnabled() && appState.sdMounted;
}

static String u64String(uint64_t value) {
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)value);
  return String(buffer);
}

static String bytesHuman(uint64_t bytes) {
  const char* units[] = {"B", "KB", "MB", "GB"};
  double value = (double)bytes;
  uint8_t unit = 0;
  while (value >= 1024.0 && unit < 3) {
    value /= 1024.0;
    unit++;
  }
  if (unit == 0) return String((unsigned long)bytes) + " B";
  unsigned int decimalPlaces = unit == 1 ? 1U : 2U;
  return String(value, decimalPlaces) + " " + units[unit];
}

static bool isTimeValid() {
  time_t now = time(nullptr);
  return now > 1700000000;
}

static String isoTimeText() {
  time_t now = time(nullptr);
  if (now <= 0) return "";

  struct tm tmInfo;
  if (!gmtime_r(&now, &tmInfo)) return "";

  char buffer[28];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tmInfo);
  return String(buffer);
}

static String dayKeyText() {
  if (!isTimeValid()) return "boot";
  time_t now = time(nullptr);
  struct tm tmInfo;
  if (!gmtime_r(&now, &tmInfo)) return "boot";
  char buffer[16];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d", &tmInfo);
  return String(buffer);
}

static String currentHistoryFilePath() {
  String path = String(SD_HISTORY_DAILY_DIR) + "/" + dayKeyText() + ".csv";
  return path;
}

static String currentLogFilePath() {
  String path = String(SD_LOG_DIR) + "/" + dayKeyText() + ".log";
  return path;
}

static String resetReasonText(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXT";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "UNKNOWN";
  }
}

static String wakeupCauseText(esp_sleep_wakeup_cause_t cause) {
  switch (cause) {
    case ESP_SLEEP_WAKEUP_UNDEFINED: return "UNDEFINED";
    case ESP_SLEEP_WAKEUP_EXT0: return "EXT0";
    case ESP_SLEEP_WAKEUP_EXT1: return "EXT1";
    case ESP_SLEEP_WAKEUP_TIMER: return "TIMER";
    case ESP_SLEEP_WAKEUP_TOUCHPAD: return "TOUCHPAD";
    case ESP_SLEEP_WAKEUP_ULP: return "ULP";
    case ESP_SLEEP_WAKEUP_GPIO: return "GPIO";
    case ESP_SLEEP_WAKEUP_UART: return "UART";
    case ESP_SLEEP_WAKEUP_WIFI: return "WIFI";
    case ESP_SLEEP_WAKEUP_COCPU: return "COCPU";
    case ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG: return "COCPU_TRAP_TRIG";
    case ESP_SLEEP_WAKEUP_BT: return "BT";
    default: return "UNKNOWN";
  }
}

static void refreshBootReasonState() {
  appState.resetReason = resetReasonText(esp_reset_reason());
  appState.wakeupCause = wakeupCauseText(esp_sleep_get_wakeup_cause());
}

static void startTimeSyncIfNeeded() {
  if (timeSyncStarted || WiFi.status() != WL_CONNECTED) return;
  timeSyncStarted = true;
  configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
  Serial.println("SD/NTP: sincronitzacio horaria demanada per etiquetar historic.");
}

static bool attemptSdBegin() {
  if (sdBegun) return true;
  pinMode(SD_SPI_CS_PIN, OUTPUT);
  digitalWrite(SD_SPI_CS_PIN, HIGH);
  SPI.begin(SD_SPI_CLK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
  sdBegun = SD.begin(SD_SPI_CS_PIN, SPI, SD_SPI_FREQUENCY_HZ);
  return sdBegun;
}

static String cardTypeName(uint8_t type) {
  switch (type) {
    case CARD_MMC: return "MMC";
    case CARD_SD: return "SDSC";
    case CARD_SDHC: return "SDHC/SDXC";
    case CARD_NONE: return "Sense targeta";
    default: return "Desconeguda";
  }
}

static void setSdError(const String& message) {
  appState.sdLastOperationOk = false;
  appState.sdLastError = message;
  if (!appState.sdMounted) appState.sdStatus = "ERROR";
}

static bool ensureDir(const String& path) {
  if (!isSdMounted()) return false;
  if (SD.exists(path.c_str())) return true;
  if (SD.mkdir(path.c_str())) return true;
  setSdError("No puc crear el directori " + path);
  return false;
}

static uint32_t countNonEmptyLines(const String& path) {
  if (!isSdMounted() || !SD.exists(path.c_str())) return 0;
  File file = SD.open(path.c_str(), FILE_READ);
  if (!file) return 0;
  uint32_t count = 0;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) count++;
  }
  file.close();
  return count;
}

bool refreshSdInfo() {
  if (!isSdEnabled()) {
    appState.sdMounted = false;
    appState.sdStatus = "DISABLED";
    appState.sdLastError = "SD desactivada per firmware";
    return false;
  }

  if (!attemptSdBegin()) {
    appState.sdMounted = false;
    appState.sdStatus = "ERROR";
    appState.sdLastError = "SD no muntada";
    return false;
  }

  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    appState.sdMounted = false;
    appState.sdStatus = "NO_CARD";
    appState.sdLastError = "No detecto cap targeta microSD";
    return false;
  }

  appState.sdCardType = cardTypeName(cardType);
  appState.sdTotalBytes = SD.totalBytes();
  appState.sdUsedBytes = SD.usedBytes();
  appState.sdFreeBytes = appState.sdTotalBytes > appState.sdUsedBytes ? appState.sdTotalBytes - appState.sdUsedBytes : 0;
  appState.sdLastCheckMillis = millis();
  appState.sdMounted = true;
  appState.sdStatus = "OK";
  appState.sdLastOperationOk = true;
  appState.sdLastError = "OK";
  return true;
}

bool ensureSdBaseStructure() {
  if (!isSdMounted()) return false;

  bool ok = true;
  ok &= ensureDir(SD_BASE_DIR);
  ok &= ensureDir(SD_HISTORY_DAILY_DIR);
  ok &= ensureDir(SD_STATS_DIR);
  ok &= ensureDir(SD_LOG_DIR);
  ok &= ensureDir(SD_MQTT_DIR);
  ok &= ensureDir(SD_CONFIG_DIR);
  ok &= ensureDir(SD_BLACKBOX_DIR);
  ok &= ensureDir(SD_SYSTEM_DIR);
  ok &= ensureDir(SD_CALIBRATION_DIR);
  return ok;
}

static void printCsvHeaderIfNew(const String& path, const String& header) {
  if (SD.exists(path.c_str())) return;
  File file = SD.open(path.c_str(), FILE_WRITE);
  if (!file) return;
  file.println(header);
  file.close();
}

bool ensureSdHistoryFile() {
  if (!isSdMounted()) return false;
  if (!ensureSdBaseStructure()) return false;

  appState.sdHistoryPath = currentHistoryFilePath();
  printCsvHeaderIfNew(
    appState.sdHistoryPath,
    "unix_time,iso_time,uptime_seconds,water_temperature_c,raw_temperature_c,water_sensor_status,internal_temperature_c,internal_humidity_percent,internal_env_status,battery_voltage_v,battery_percent,battery_status,wifi_rssi_dbm"
  );

  if (!SD.exists(appState.sdHistoryPath.c_str())) {
    setSdError("No puc crear el fitxer historic diari a la SD");
    return false;
  }

  printCsvHeaderIfNew(
    SD_DAILY_STATS_FILE,
    "unix_time,iso_time,day,records,temp_min_c,temp_max_c,temp_avg_c,battery_min_v,battery_max_v,battery_avg_v,error_count,last_sensor_status,last_battery_status,wifi_rssi_dbm"
  );

  return true;
}

static void csvField(File& file, const String& value) {
  bool needsQuotes = value.indexOf(',') >= 0 || value.indexOf('"') >= 0 || value.indexOf('\n') >= 0 || value.indexOf('\r') >= 0;
  if (!needsQuotes) {
    file.print(value);
    return;
  }

  file.print('"');
  for (size_t i = 0; i < value.length(); i++) {
    char c = value.charAt(i);
    if (c == '"') file.print("\"\"");
    else file.print(c);
  }
  file.print('"');
}

static String floatCsv(float value, uint8_t decimals) {
  if (isnan(value)) return "";
  unsigned int decimalPlaces = decimals;
  return String(value, decimalPlaces);
}

static void resetRuntimeStatsForDay(const String& day) {
  runtimeStatsDay = day;
  runtimeStatsRecords = 0;
  runtimeStatsErrors = 0;
  runtimeTempMin = NAN;
  runtimeTempMax = NAN;
  runtimeTempSum = 0.0;
  runtimeTempCount = 0;
  runtimeBatteryMin = NAN;
  runtimeBatteryMax = NAN;
  runtimeBatterySum = 0.0;
  runtimeBatteryCount = 0;

  appState.sdStatsDay = day;
  appState.sdDailyRecordCount = 0;
  appState.sdDailyErrorCount = 0;
  appState.sdDailyTempMin = NAN;
  appState.sdDailyTempMax = NAN;
  appState.sdDailyTempAvg = NAN;
  appState.sdDailyBatteryMin = NAN;
  appState.sdDailyBatteryMax = NAN;
  appState.sdDailyBatteryAvg = NAN;
}

static void updateRuntimeStats() {
  String day = dayKeyText();
  if (runtimeStatsDay != day) {
    resetRuntimeStatsForDay(day);
  }

  runtimeStatsRecords++;
  bool hasError = appState.sensorStatus != "OK" || appState.internalEnvStatus == "ERROR" || appState.batteryStatus == "ERROR";
  if (hasError) runtimeStatsErrors++;

  if (!isnan(appState.lastValidTemperatureC)) {
    if (isnan(runtimeTempMin) || appState.lastValidTemperatureC < runtimeTempMin) runtimeTempMin = appState.lastValidTemperatureC;
    if (isnan(runtimeTempMax) || appState.lastValidTemperatureC > runtimeTempMax) runtimeTempMax = appState.lastValidTemperatureC;
    runtimeTempSum += appState.lastValidTemperatureC;
    runtimeTempCount++;
  }

  if (!isnan(appState.lastBatteryVoltage)) {
    if (isnan(runtimeBatteryMin) || appState.lastBatteryVoltage < runtimeBatteryMin) runtimeBatteryMin = appState.lastBatteryVoltage;
    if (isnan(runtimeBatteryMax) || appState.lastBatteryVoltage > runtimeBatteryMax) runtimeBatteryMax = appState.lastBatteryVoltage;
    runtimeBatterySum += appState.lastBatteryVoltage;
    runtimeBatteryCount++;
  }

  appState.sdStatsDay = runtimeStatsDay;
  appState.sdDailyRecordCount = runtimeStatsRecords;
  appState.sdDailyErrorCount = runtimeStatsErrors;
  appState.sdDailyTempMin = runtimeTempMin;
  appState.sdDailyTempMax = runtimeTempMax;
  appState.sdDailyTempAvg = runtimeTempCount == 0 ? NAN : (float)(runtimeTempSum / runtimeTempCount);
  appState.sdDailyBatteryMin = runtimeBatteryMin;
  appState.sdDailyBatteryMax = runtimeBatteryMax;
  appState.sdDailyBatteryAvg = runtimeBatteryCount == 0 ? NAN : (float)(runtimeBatterySum / runtimeBatteryCount);
}

static bool appendDailyStatsSnapshot() {
  if (!isSdMounted()) return false;
  printCsvHeaderIfNew(
    SD_DAILY_STATS_FILE,
    "unix_time,iso_time,day,records,temp_min_c,temp_max_c,temp_avg_c,battery_min_v,battery_max_v,battery_avg_v,error_count,last_sensor_status,last_battery_status,wifi_rssi_dbm"
  );

  File file = SD.open(SD_DAILY_STATS_FILE, FILE_APPEND);
  if (!file) {
    setSdError("No puc obrir el fitxer d'estadistiques diaries");
    return false;
  }

  time_t now = time(nullptr);
  String fields[14];
  fields[0] = isTimeValid() ? String((unsigned long)now) : "";
  fields[1] = isTimeValid() ? isoTimeText() : "";
  fields[2] = runtimeStatsDay;
  fields[3] = String((unsigned long)runtimeStatsRecords);
  fields[4] = floatCsv(runtimeTempMin, 2);
  fields[5] = floatCsv(runtimeTempMax, 2);
  fields[6] = runtimeTempCount == 0 ? "" : floatCsv((float)(runtimeTempSum / runtimeTempCount), 2);
  fields[7] = floatCsv(runtimeBatteryMin, 3);
  fields[8] = floatCsv(runtimeBatteryMax, 3);
  fields[9] = runtimeBatteryCount == 0 ? "" : floatCsv((float)(runtimeBatterySum / runtimeBatteryCount), 3);
  fields[10] = String((unsigned long)runtimeStatsErrors);
  fields[11] = appState.sensorStatus;
  fields[12] = appState.batteryStatus;
  fields[13] = WiFi.status() == WL_CONNECTED ? String(WiFi.RSSI()) : "";

  for (uint8_t i = 0; i < 14; i++) {
    if (i > 0) file.print(',');
    csvField(file, fields[i]);
  }
  file.println();
  file.close();
  return true;
}

bool appendSdHistoryRecord() {
  if (!isSdMounted()) {
    appState.sdHistoryWriteFailCount++;
    return false;
  }

  if (!ensureSdHistoryFile()) {
    appState.sdHistoryWriteFailCount++;
    return false;
  }

  File file = SD.open(appState.sdHistoryPath.c_str(), FILE_APPEND);
  if (!file) {
    appState.sdHistoryWriteFailCount++;
    setSdError("No puc obrir l'historic diari en mode append");
    return false;
  }

  time_t now = time(nullptr);
  String iso = isTimeValid() ? isoTimeText() : "";

  String fields[13];
  fields[0] = isTimeValid() ? String((unsigned long)now) : "";
  fields[1] = iso;
  fields[2] = String((unsigned long)getUptimeSeconds());
  fields[3] = floatCsv(appState.lastValidTemperatureC, configTemperatureDecimals);
  fields[4] = floatCsv(appState.lastRawTemperatureC, configTemperatureDecimals);
  fields[5] = appState.sensorStatus;
  fields[6] = floatCsv(appState.lastInternalTemperatureC, 2);
  fields[7] = floatCsv(appState.lastInternalHumidityPercent, 1);
  fields[8] = appState.internalEnvStatus;
  fields[9] = floatCsv(appState.lastBatteryVoltage, 3);
  fields[10] = floatCsv(appState.lastBatteryPercent, 0);
  fields[11] = appState.batteryStatus;
  fields[12] = WiFi.status() == WL_CONNECTED ? String(WiFi.RSSI()) : "";

  String line = "";
  for (uint8_t i = 0; i < 13; i++) {
    if (i > 0) {
      file.print(',');
      line += ',';
    }
    csvField(file, fields[i]);
    line += fields[i];
  }
  file.println();
  file.close();

  updateRuntimeStats();
  appendDailyStatsSnapshot();

  appState.sdHistoryWriteCount++;
  appState.sdLastWriteMillis = millis();
  appState.sdLastHistoryLine = line;
  appState.sdStatus = "OK";
  appState.sdLastOperationOk = true;
  appState.sdLastError = "OK";
  refreshSdInfo();

  Serial.print("SD historic: registre escrit #");
  Serial.print((unsigned long)appState.sdHistoryWriteCount);
  Serial.print(" a ");
  Serial.println(appState.sdHistoryPath);
  return true;
}

bool appendSdSystemLog(const String& level, const String& message) {
  if (!isSdMounted()) return false;
  if (!ensureSdBaseStructure()) return false;

  String path = currentLogFilePath();
  File file = SD.open(path.c_str(), FILE_APPEND);
  if (!file) return false;

  String iso = isTimeValid() ? isoTimeText() : String("uptime+") + String((unsigned long)getUptimeSeconds()) + "s";
  file.print(iso);
  file.print(" [");
  file.print(level);
  file.print("] ");
  file.println(message);
  file.close();

  appState.sdSystemLogPath = path;
  return true;
}

bool writeSdVersionFile() {
  if (!isSdMounted()) return false;
  if (!ensureSdBaseStructure()) return false;

  File file = SD.open(SD_VERSION_FILE, FILE_WRITE);
  if (!file) return false;
  file.print("{\"firmware_version\":\"");
  file.print(jsonEscape(FIRMWARE_VERSION));
  file.print("\",\"change_title\":\"");
  file.print(jsonEscape(FIRMWARE_CHANGE_TITLE));
  file.print("\",\"change_notes\":\"");
  file.print(jsonEscape(FIRMWARE_CHANGE_NOTES));
  file.print("\",\"device_id\":\"");
  file.print(jsonEscape(configHaDeviceId));
  file.print("\",\"written_iso\":\"");
  file.print(jsonEscape(isTimeValid() ? isoTimeText() : ""));
  file.println("\"}");
  file.close();
  return true;
}

bool writeSdConfigSnapshot() {
  if (!isSdMounted()) return false;
  if (!ensureSdBaseStructure()) return false;

  File file = SD.open(SD_CONFIG_SNAPSHOT_FILE, FILE_WRITE);
  if (!file) return false;
  file.println("{");
  file.println("  \"device_name\": \"" + jsonEscape(configDeviceName) + "\",");
  file.println("  \"device_hostname\": \"" + jsonEscape(configDeviceHostname) + "\",");
  file.println("  \"firmware_version\": \"" + jsonEscape(FIRMWARE_VERSION) + "\",");
  file.println("  \"read_interval_seconds\": " + String(configReadIntervalSeconds) + ",");
  file.println("  \"temperature_decimals\": " + String(configTemperatureDecimals) + ",");
  file.println("  \"temperature_offset_c\": " + floatCsv(configTemperatureOffsetC, 2) + ",");
  file.println("  \"battery_empty_voltage\": " + floatCsv(configBatteryEmptyVoltage, 3) + ",");
  file.println("  \"battery_full_voltage\": " + floatCsv(configBatteryFullVoltage, 3) + ",");
  file.println("  \"battery_low_percent\": " + floatCsv(configBatteryLowPercent, 1) + ",");
  file.println("  \"battery_calibration_factor\": " + floatCsv(configBatteryCalibrationFactor, 4) + ",");
  file.println("  \"mqtt_enabled\": " + String(configMqttEnabled ? "true" : "false") + ",");
  file.println("  \"mqtt_host\": \"" + jsonEscape(configMqttHost) + "\",");
  file.println("  \"mqtt_topic_base\": \"" + jsonEscape(configMqttTopicBase) + "\",");
  file.println("  \"ha_discovery_enabled\": " + String(configHaDiscoveryEnabled ? "true" : "false") + ",");
  file.println("  \"ha_device_id\": \"" + jsonEscape(configHaDeviceId) + "\",");
  file.println("  \"sd_history_daily_dir\": \"" + jsonEscape(SD_HISTORY_DAILY_DIR) + "\"");
  file.println("}");
  file.close();
  return true;
}

bool writeSdBootBlackbox() {
  if (!isSdMounted()) return false;
  if (!ensureSdBaseStructure()) return false;
  refreshBootReasonState();

  File file = SD.open(SD_BOOT_BLACKBOX_FILE, FILE_WRITE);
  if (!file) return false;
  file.println("{");
  file.println("  \"boot_uptime_seconds\": " + String((unsigned long)getUptimeSeconds()) + ",");
  file.println("  \"firmware_version\": \"" + jsonEscape(FIRMWARE_VERSION) + "\",");
  file.println("  \"reset_reason_code\": " + String((int)esp_reset_reason()) + ",");
  file.println("  \"reset_reason\": \"" + jsonEscape(appState.resetReason) + "\",");
  file.println("  \"wakeup_cause\": \"" + jsonEscape(appState.wakeupCause) + "\",");
  file.println("  \"free_heap\": " + String((unsigned long)ESP.getFreeHeap()) + ",");
  file.println("  \"read_interval_seconds\": " + String(configReadIntervalSeconds) + ",");
  file.println("  \"mqtt_publish_interval_seconds\": " + String(configMqttPublishIntervalSeconds) + ",");
  file.println("  \"deep_sleep_enabled\": " + String(configDeepSleepEnabled ? "true" : "false") + ",");
  file.println("  \"deep_sleep_awake_seconds\": " + String(configDeepSleepAwakeSeconds) + ",");
  file.println("  \"wifi_ssid_configured\": " + String(configWifiSsid.length() > 0 ? "true" : "false") + ",");
  file.println("  \"sd_status\": \"" + jsonEscape(appState.sdStatus) + "\"");
  file.println("}");
  file.close();
  return true;
}

bool appendSdBootHistory() {
  if (!isSdMounted()) return false;
  if (!ensureSdBaseStructure()) return false;
  refreshBootReasonState();

  File file = SD.open(SD_BOOT_HISTORY_FILE, FILE_APPEND);
  if (!file) return false;
  file.print("{\"iso\":\"");
  file.print(jsonEscape(isTimeValid() ? isoTimeText() : ""));
  file.print("\",\"uptime_seconds\":");
  file.print((unsigned long)getUptimeSeconds());
  file.print(",\"firmware_version\":\"");
  file.print(jsonEscape(FIRMWARE_VERSION));
  file.print("\",\"reset_reason_code\":");
  file.print((int)esp_reset_reason());
  file.print(",\"reset_reason\":\"");
  file.print(jsonEscape(appState.resetReason));
  file.print("\",\"wakeup_cause\":\"");
  file.print(jsonEscape(appState.wakeupCause));
  file.print("\",\"free_heap\":");
  file.print((unsigned long)ESP.getFreeHeap());
  file.print(",\"read_interval_seconds\":");
  file.print(configReadIntervalSeconds);
  file.print(",\"mqtt_publish_interval_seconds\":");
  file.print(configMqttPublishIntervalSeconds);
  file.print(",\"deep_sleep_enabled\":");
  file.print(configDeepSleepEnabled ? "true" : "false");
  file.println("}");
  file.close();
  return true;
}

bool appendSdMqttPending(const String& payload) {
  if (!isSdMounted()) return false;
  if (!ensureSdBaseStructure()) return false;

  File file = SD.open(SD_MQTT_PENDING_FILE, FILE_APPEND);
  if (!file) {
    setSdError("No puc obrir el buffer MQTT pendent");
    return false;
  }
  file.println(payload);
  file.close();

  appState.sdMqttPendingCount++;
  appendSdSystemLog("MQTT", "Telemetria afegida al buffer local");
  return true;
}

bool hasSdMqttPending() {
  if (!isSdMounted()) return false;
  return SD.exists(SD_MQTT_PENDING_FILE);
}

bool flushSdMqttPending(const String& topic, SdMqttPublishCallback publishCallback) {
  if (!isSdMounted() || publishCallback == nullptr || !SD.exists(SD_MQTT_PENDING_FILE)) return false;

  File input = SD.open(SD_MQTT_PENDING_FILE, FILE_READ);
  if (!input) return false;

  SD.remove(SD_MQTT_PENDING_TMP_FILE);
  File output = SD.open(SD_MQTT_PENDING_TMP_FILE, FILE_WRITE);
  if (!output) {
    input.close();
    return false;
  }

  bool allFlushed = true;
  bool keepRest = false;
  uint32_t flushed = 0;
  uint32_t kept = 0;

  while (input.available()) {
    String line = input.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    if (!keepRest && publishCallback(topic, line)) {
      flushed++;
      continue;
    }

    keepRest = true;
    allFlushed = false;
    output.println(line);
    kept++;
  }

  input.close();
  output.close();

  SD.remove(SD_MQTT_PENDING_FILE);
  if (kept > 0) {
    SD.rename(SD_MQTT_PENDING_TMP_FILE, SD_MQTT_PENDING_FILE);
  } else {
    SD.remove(SD_MQTT_PENDING_TMP_FILE);
  }

  appState.sdMqttFlushCount += flushed;
  appState.sdMqttPendingCount = kept;

  if (flushed > 0) {
    appendSdSystemLog("MQTT", "Buffer MQTT enviat: " + String((unsigned long)flushed) + " registres, pendents " + String((unsigned long)kept));
  }

  return allFlushed;
}

static bool removeRecursive(const String& path) {
  File root = SD.open(path.c_str());
  if (!root) return false;

  if (!root.isDirectory()) {
    root.close();
    return SD.remove(path.c_str());
  }

  File file = root.openNextFile();
  while (file) {
    String childPath = file.path();
    bool childIsDir = file.isDirectory();
    file.close();

    if (childIsDir) {
      removeRecursive(childPath);
      SD.rmdir(childPath.c_str());
    } else {
      SD.remove(childPath.c_str());
    }

    file = root.openNextFile();
  }
  root.close();

  if (path != "/") {
    return SD.rmdir(path.c_str());
  }
  return true;
}

bool logicalFormatSdCard() {
  if (!isSdMounted()) {
    setSdError("No puc netejar la SD perquè no està muntada");
    return false;
  }

  Serial.println("SD: format logic / neteja completa iniciada");
  bool ok = removeRecursive("/");
  if (!ok) {
    setSdError("No he pogut esborrar tot el contingut de la SD");
    return false;
  }

  appState.sdHistoryWriteCount = 0;
  appState.sdHistoryWriteFailCount = 0;
  appState.sdLastHistoryLine = "";
  appState.sdMqttPendingCount = 0;
  appState.sdMqttFlushCount = 0;
  resetRuntimeStatsForDay(dayKeyText());

  bool structureOk = ensureSdBaseStructure();
  bool historyOk = ensureSdHistoryFile();
  writeSdVersionFile();
  writeSdConfigSnapshot();
  writeSdBootBlackbox();
  refreshSdInfo();

  if (structureOk && historyOk) {
    appState.sdLastError = "SD netejada i estructura recreada";
    appState.sdLastOperationOk = true;
    appState.sdStatus = "OK";
    appendSdSystemLog("SD", "Neteja logica completada");
  }
  return structureOk && historyOk;
}

bool normalizeSdPath(const String& input, String& output) {
  String clean = input;
  clean.trim();
  clean.replace("\\", "/");
  if (clean.length() == 0) clean = SD_BASE_DIR;
  if (!clean.startsWith("/")) clean = "/" + clean;
  if (clean.indexOf("..") >= 0) return false;
  while (clean.indexOf("//") >= 0) clean.replace("//", "/");
  if (clean.length() > 1 && clean.endsWith("/")) clean.remove(clean.length() - 1);
  output = clean;
  return true;
}

bool sdPathExists(const String& path) {
  if (!isSdMounted()) return false;
  String clean;
  if (!normalizeSdPath(path, clean)) return false;
  return SD.exists(clean.c_str());
}

bool sdPathIsDirectory(const String& path) {
  if (!isSdMounted()) return false;
  String clean;
  if (!normalizeSdPath(path, clean)) return false;
  File file = SD.open(clean.c_str());
  if (!file) return false;
  bool isDir = file.isDirectory();
  file.close();
  return isDir;
}

String sdReadTextFileLimited(const String& path, size_t maxBytes, bool& truncated) {
  truncated = false;
  if (!isSdMounted()) return "SD no muntada";

  String clean;
  if (!normalizeSdPath(path, clean)) return "Ruta no valida";

  File file = SD.open(clean.c_str(), FILE_READ);
  if (!file) return "No puc obrir el fitxer";
  if (file.isDirectory()) {
    file.close();
    return "La ruta és un directori";
  }

  String content;
  size_t limit = maxBytes == 0 ? SD_VIEW_MAX_BYTES_DEFAULT : maxBytes;
  content.reserve(limit < 4096 ? limit : 4096);
  size_t readBytes = 0;
  while (file.available() && readBytes < limit) {
    content += (char)file.read();
    readBytes++;
  }
  truncated = file.available();
  file.close();
  return content;
}

static String fileNameFromPath(const String& path) {
  int pos = path.lastIndexOf('/');
  if (pos < 0) return path;
  return path.substring(pos + 1);
}

static String childPathForDirectoryEntry(const String& directoryPath, File& file) {
  String name = file.name();
  if (name.length() == 0) name = file.path();
  name.replace("\\", "/");

  int slash = name.lastIndexOf('/');
  if (slash >= 0) name = name.substring(slash + 1);

  String base = directoryPath;
  if (base.length() == 0) base = "/";
  while (base.length() > 1 && base.endsWith("/")) base.remove(base.length() - 1);

  if (base == "/") return "/" + name;
  return base + "/" + name;
}

String sdDirectoryListingJson(const String& path) {
  String clean;
  if (!normalizeSdPath(path, clean)) clean = SD_BASE_DIR;

  String json = "{\"path\":\"" + jsonEscape(clean) + "\",\"mounted\":" + String(isSdMounted() ? "true" : "false") + ",\"items\":[";
  if (!isSdMounted()) {
    json += "]}";
    return json;
  }

  File root = SD.open(clean.c_str());
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    json += "]}";
    return json;
  }

  bool first = true;
  File file = root.openNextFile();
  while (file) {
    String itemPath = childPathForDirectoryEntry(clean, file);
    if (!first) json += ",";
    first = false;
    json += "{\"name\":\"" + jsonEscape(fileNameFromPath(itemPath)) + "\",\"path\":\"" + jsonEscape(itemPath) + "\",\"directory\":" + String(file.isDirectory() ? "true" : "false") + ",\"size\":" + u64String(file.size()) + "}";
    file.close();
    file = root.openNextFile();
  }
  root.close();
  json += "]}";
  return json;
}

String sdDirectoryListingHtml(const String& path) {
  String clean;
  if (!normalizeSdPath(path, clean)) clean = SD_BASE_DIR;

  String html = "";
  if (!isSdMounted()) {
    html += "<p class='hint bad'>SD no muntada. L'explorador només apareixerà quan la targeta funcioni.</p>";
    return html;
  }

  File root = SD.open(clean.c_str());
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    html += "<p class='hint bad'>No puc obrir el directori: " + htmlEscape(clean) + "</p>";
    return html;
  }

  html += "<div class='small'>Ruta actual: <code>" + htmlEscape(clean) + "</code></div>";
  html += "<div class='actions' style='margin:10px 0'>";
  if (clean != "/") {
    String parent = clean;
    int slash = parent.lastIndexOf('/');
    parent = slash <= 0 ? "/" : parent.substring(0, slash);
    html += "<a class='btn secondary' href='/storage?path=" + htmlEscape(parent) + "'>Pujar nivell</a>";
  }
  html += "<a class='btn secondary' href='/sd-list?path=" + htmlEscape(clean) + "'>JSON directori</a>";
  html += "</div>";
  html += "<table><tr><th>Nom</th><th>Tipus</th><th>Mida</th><th>Accions</th></tr>";

  File file = root.openNextFile();
  bool any = false;
  while (file) {
    any = true;
    String itemPath = childPathForDirectoryEntry(clean, file);
    String name = fileNameFromPath(itemPath);
    bool isDir = file.isDirectory();
    html += "<tr><td>" + htmlEscape(name) + "</td><td>" + String(isDir ? "Directori" : "Fitxer") + "</td><td>" + String(isDir ? "-" : bytesHuman(file.size())) + "</td><td>";
    if (isDir) {
      html += "<a href='/storage?path=" + htmlEscape(itemPath) + "'>Obrir</a>";
    } else {
      html += "<a href='/sd-view?path=" + htmlEscape(itemPath) + "'>Veure</a> · <a href='/sd-download?path=" + htmlEscape(itemPath) + "'>Descarregar</a>";
    }
    html += "</td></tr>";
    file.close();
    file = root.openNextFile();
  }
  root.close();

  if (!any) html += "<tr><td colspan='4'>Directori buit</td></tr>";
  html += "</table>";
  return html;
}

void initSdManager() {
  refreshBootReasonState();
  appState.sdEnabled = isSdEnabled();
  appState.sdMounted = false;
  appState.sdStatus = isSdEnabled() ? "INIT" : "DISABLED";
  appState.sdHistoryPath = currentHistoryFilePath();
  appState.sdDailyStatsPath = SD_DAILY_STATS_FILE;
  appState.sdSystemLogPath = currentLogFilePath();
  appState.sdPendingMqttPath = SD_MQTT_PENDING_FILE;
  resetRuntimeStatsForDay(dayKeyText());

  if (!isSdEnabled()) {
    Serial.println("SD: desactivada per firmware");
    return;
  }

  Serial.println();
  Serial.print("SD: inicialitzant SPI CLK GPIO");
  Serial.print(SD_SPI_CLK_PIN);
  Serial.print(" · MISO GPIO");
  Serial.print(SD_SPI_MISO_PIN);
  Serial.print(" · MOSI GPIO");
  Serial.print(SD_SPI_MOSI_PIN);
  Serial.print(" · CS GPIO");
  Serial.println(SD_SPI_CS_PIN);

  if (!attemptSdBegin() || SD.cardType() == CARD_NONE) {
    appState.sdMounted = false;
    appState.sdStatus = "NO_CARD";
    appState.sdLastError = "No detecto la microSD. Revisa cablejat, format FAT32 i alimentacio 3V3.";
    Serial.print("SD ERROR: ");
    Serial.println(appState.sdLastError);
    return;
  }

  appState.sdMounted = true;
  refreshSdInfo();
  ensureSdBaseStructure();
  ensureSdHistoryFile();
  writeSdVersionFile();
  writeSdConfigSnapshot();
  writeSdBootBlackbox();
  appendSdBootHistory();
  appState.sdMqttPendingCount = countNonEmptyLines(SD_MQTT_PENDING_FILE);
  appendSdSystemLog("BOOT", "Sistema arrencat amb microSD muntada. Reset=" + appState.resetReason + " Wakeup=" + appState.wakeupCause);
  refreshSdInfo();

  Serial.print("SD: muntada. Tipus ");
  Serial.print(appState.sdCardType);
  Serial.print(" · total ");
  Serial.print(sdTotalText());
  Serial.print(" · usat ");
  Serial.println(sdUsedText());
}

void handleSdManager() {
  startTimeSyncIfNeeded();

  if (!isSdEnabled() || !sdBegun) return;

  if (isSdMounted() && isTimeValid()) {
    if (!bootHistoryWithTimeAppended) {
      appendSdBootHistory();
      bootHistoryWithTimeAppended = true;
    }
    if (!bootLogWithTimeAppended) {
      appendSdSystemLog("BOOT", "Hora sincronitzada. Reset=" + appState.resetReason + " Wakeup=" + appState.wakeupCause);
      bootLogWithTimeAppended = true;
    }
  }

  unsigned long now = millis();
  if (now - lastSdRefreshMillis >= SD_REFRESH_INTERVAL_MS || lastSdRefreshMillis == 0) {
    lastSdRefreshMillis = now;
    refreshSdInfo();
  }

  if (isSdMounted() && (now - lastSdStructureCheckMillis >= SD_STRUCTURE_CHECK_INTERVAL_MS || lastSdStructureCheckMillis == 0)) {
    lastSdStructureCheckMillis = now;
    ensureSdBaseStructure();
    appState.sdHistoryPath = currentHistoryFilePath();
    appState.sdSystemLogPath = currentLogFilePath();
    appState.sdMqttPendingCount = countNonEmptyLines(SD_MQTT_PENDING_FILE);
  }
}

String sdStatusText() {
  if (!isSdEnabled()) return "DISABLED";
  return appState.sdStatus;
}

String sdCardTypeText() {
  if (!isSdMounted()) return "Sense targeta";
  return appState.sdCardType;
}

String sdTotalText() {
  if (!isSdMounted()) return "Sense dades";
  return bytesHuman(appState.sdTotalBytes);
}

String sdUsedText() {
  if (!isSdMounted()) return "Sense dades";
  return bytesHuman(appState.sdUsedBytes);
}

String sdFreeText() {
  if (!isSdMounted()) return "Sense dades";
  return bytesHuman(appState.sdFreeBytes);
}

String sdUsedPercentText() {
  if (!isSdMounted() || appState.sdTotalBytes == 0) return "Sense dades";
  double pct = ((double)appState.sdUsedBytes / (double)appState.sdTotalBytes) * 100.0;
  unsigned int decimalPlaces = 1U;
  return String(pct, decimalPlaces) + " %";
}

String sdHistoryPathText() {
  appState.sdHistoryPath = currentHistoryFilePath();
  return appState.sdHistoryPath;
}

String sdDailyStatsPathText() {
  return SD_DAILY_STATS_FILE;
}

String sdSystemLogPathText() {
  appState.sdSystemLogPath = currentLogFilePath();
  return appState.sdSystemLogPath;
}

String sdPendingMqttPathText() {
  return SD_MQTT_PENDING_FILE;
}

String sdLastErrorText() {
  return appState.sdLastError;
}

String sdInfoJson() {
  String json = "{";
  json += "\"enabled\":";
  json += isSdEnabled() ? "true" : "false";
  json += ",\"mounted\":";
  json += isSdMounted() ? "true" : "false";
  json += ",\"status\":\"";
  json += jsonEscape(sdStatusText());
  json += "\"";
  json += ",\"card_type\":\"";
  json += jsonEscape(sdCardTypeText());
  json += "\"";
  json += ",\"total_bytes\":";
  json += u64String(appState.sdTotalBytes);
  json += ",\"used_bytes\":";
  json += u64String(appState.sdUsedBytes);
  json += ",\"free_bytes\":";
  json += u64String(appState.sdFreeBytes);
  json += ",\"history_file\":\"";
  json += jsonEscape(sdHistoryPathText());
  json += "\"";
  json += ",\"daily_stats_file\":\"";
  json += jsonEscape(sdDailyStatsPathText());
  json += "\"";
  json += ",\"system_log_file\":\"";
  json += jsonEscape(sdSystemLogPathText());
  json += "\"";
  json += ",\"mqtt_pending_file\":\"";
  json += jsonEscape(sdPendingMqttPathText());
  json += "\"";
  json += ",\"history_writes\":";
  json += String((unsigned long)appState.sdHistoryWriteCount);
  json += ",\"history_write_fails\":";
  json += String((unsigned long)appState.sdHistoryWriteFailCount);
  json += ",\"mqtt_pending_count\":";
  json += String((unsigned long)appState.sdMqttPendingCount);
  json += ",\"mqtt_flush_count\":";
  json += String((unsigned long)appState.sdMqttFlushCount);
  json += ",\"daily_day\":\"";
  json += jsonEscape(appState.sdStatsDay);
  json += "\",\"daily_records\":";
  json += String((unsigned long)appState.sdDailyRecordCount);
  json += ",\"daily_error_count\":";
  json += String((unsigned long)appState.sdDailyErrorCount);
  json += ",\"daily_temp_min\":";
  json += isnan(appState.sdDailyTempMin) ? String("null") : floatCsv(appState.sdDailyTempMin, 2);
  json += ",\"daily_temp_max\":";
  json += isnan(appState.sdDailyTempMax) ? String("null") : floatCsv(appState.sdDailyTempMax, 2);
  json += ",\"daily_temp_avg\":";
  json += isnan(appState.sdDailyTempAvg) ? String("null") : floatCsv(appState.sdDailyTempAvg, 2);
  json += ",\"daily_battery_min\":";
  json += isnan(appState.sdDailyBatteryMin) ? String("null") : floatCsv(appState.sdDailyBatteryMin, 3);
  json += ",\"daily_battery_max\":";
  json += isnan(appState.sdDailyBatteryMax) ? String("null") : floatCsv(appState.sdDailyBatteryMax, 3);
  json += ",\"daily_battery_avg\":";
  json += isnan(appState.sdDailyBatteryAvg) ? String("null") : floatCsv(appState.sdDailyBatteryAvg, 3);
  json += ",\"last_error\":\"";
  json += jsonEscape(sdLastErrorText());
  json += "\"";
  json += ",\"last_write_uptime_seconds\":";
  json += appState.sdLastWriteMillis == 0 ? String("null") : String((unsigned long)(appState.sdLastWriteMillis / 1000UL));
  json += "}";
  return json;
}
