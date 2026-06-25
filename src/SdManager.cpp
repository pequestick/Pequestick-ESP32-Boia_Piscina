#include "SdManager.h"

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <time.h>

#include "AppConfig.h"
#include "AppState.h"
#include "Utils.h"

static bool sdBegun = false;
static bool timeSyncStarted = false;
static unsigned long lastSdRefreshMillis = 0;
static const unsigned long SD_REFRESH_INTERVAL_MS = 30000;

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
  if (unit == 0) return String((uint32_t)bytes) + " B";
  return String(value, unit == 1 ? 1 : 2) + " " + units[unit];
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

static bool ensureSdDirectory() {
  if (!isSdMounted()) return false;
  if (SD.exists(SD_HISTORY_DIR)) return true;
  if (SD.mkdir(SD_HISTORY_DIR)) return true;
  setSdError("No puc crear el directori d'historic a la SD");
  return false;
}

bool ensureSdHistoryFile() {
  if (!isSdMounted()) return false;
  if (!ensureSdDirectory()) return false;

  if (SD.exists(SD_HISTORY_FILE)) return true;

  File file = SD.open(SD_HISTORY_FILE, FILE_WRITE);
  if (!file) {
    setSdError("No puc crear el fitxer d'historic a la SD");
    return false;
  }

  file.println("unix_time,iso_time,uptime_seconds,water_temperature_c,raw_temperature_c,water_sensor_status,internal_temperature_c,internal_humidity_percent,internal_env_status,battery_voltage_v,battery_percent,battery_status,wifi_rssi_dbm");
  file.close();
  return true;
}

void initSdManager() {
  appState.sdEnabled = isSdEnabled();
  appState.sdMounted = false;
  appState.sdStatus = isSdEnabled() ? "INIT" : "DISABLED";
  appState.sdHistoryPath = SD_HISTORY_FILE;

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
  ensureSdHistoryFile();

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

  unsigned long now = millis();
  if (now - lastSdRefreshMillis >= SD_REFRESH_INTERVAL_MS || lastSdRefreshMillis == 0) {
    lastSdRefreshMillis = now;
    refreshSdInfo();
  }
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
  return String(value, decimals);
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

  File file = SD.open(SD_HISTORY_FILE, FILE_APPEND);
  if (!file) {
    appState.sdHistoryWriteFailCount++;
    setSdError("No puc obrir l'historic en mode append");
    return false;
  }

  time_t now = time(nullptr);
  String iso = isTimeValid() ? isoTimeText() : "";

  String fields[13];
  fields[0] = isTimeValid() ? String((uint32_t)now) : "";
  fields[1] = iso;
  fields[2] = String(getUptimeSeconds());
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

  appState.sdHistoryWriteCount++;
  appState.sdLastWriteMillis = millis();
  appState.sdLastHistoryLine = line;
  appState.sdStatus = "OK";
  appState.sdLastOperationOk = true;
  appState.sdLastError = "OK";
  refreshSdInfo();

  Serial.print("SD historic: registre escrit #");
  Serial.println(appState.sdHistoryWriteCount);
  return true;
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

  bool historyOk = ensureSdHistoryFile();
  refreshSdInfo();

  if (historyOk) {
    appState.sdLastError = "SD netejada i historic recreat";
    appState.sdLastOperationOk = true;
    appState.sdStatus = "OK";
  }
  return historyOk;
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
  return String(pct, 1) + " %";
}

String sdHistoryPathText() {
  return appState.sdHistoryPath.length() > 0 ? appState.sdHistoryPath : String(SD_HISTORY_FILE);
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
  json += ",\"history_writes\":";
  json += String(appState.sdHistoryWriteCount);
  json += ",\"history_write_fails\":";
  json += String(appState.sdHistoryWriteFailCount);
  json += ",\"last_error\":\"";
  json += jsonEscape(sdLastErrorText());
  json += "\"";
  json += ",\"last_write_uptime_seconds\":";
  json += appState.sdLastWriteMillis == 0 ? String("null") : String(appState.sdLastWriteMillis / 1000UL);
  json += "}";
  return json;
}
