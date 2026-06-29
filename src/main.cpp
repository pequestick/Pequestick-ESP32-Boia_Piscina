#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>

#include "AppConfig.h"
#include "AppState.h"
#include "TemperatureSensor.h"
#include "WifiManagerBoia.h"
#include "MqttManager.h"
#include "WebServerBoia.h"
#include "HardwareManager.h"
#include "InternalEnvSensor.h"
#include "BatteryMonitor.h"
#include "SdManager.h"

// ==========================
// ESTAT GLOBAL
// ==========================

AppState appState;
static bool cycleReadDone = false;
static bool cyclePublishDone = false;

static void enterDeepSleepBetweenReadings() {
  uint64_t sleepSeconds = configReadIntervalSeconds;
  if (sleepSeconds < 5) sleepSeconds = 5;

  appState.lowPowerStatus = "Entrant en deep sleep durant " + String((unsigned long)sleepSeconds) + " s";
  appState.lowPowerSleepRequested = true;
  Serial.println(appState.lowPowerStatus);

  appendSdSystemLog("POWER", appState.lowPowerStatus);
  if (configMqttEnabled) {
    publishOfflineAndDisconnect();
  }

  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  delay(200);

  esp_sleep_enable_timer_wakeup(sleepSeconds * 1000000ULL);
  esp_deep_sleep_start();
}

static void maybeEnterLowPowerSleep(unsigned long now) {
  if (!configDeepSleepEnabled) {
    appState.lowPowerStatus = "Mode estalvi profund desactivat";
    return;
  }
  if (appState.otaInProgress) {
    appState.lowPowerStatus = "Deep sleep ajornat: OTA en curs";
    return;
  }
  if (isWifiApActive()) {
    appState.lowPowerStatus = "Deep sleep ajornat: AP de rescat actiu";
    return;
  }
  if (!cycleReadDone || (configMqttEnabled && !cyclePublishDone)) {
    appState.lowPowerStatus = "Deep sleep pendent de lectura/publicacio";
    return;
  }
  if (now < (unsigned long)configDeepSleepAwakeSeconds * 1000UL) {
    appState.lowPowerStatus = "Finestra web activa abans del deep sleep";
    return;
  }

  enterDeepSleepBetweenReadings();
}

// ==========================
// HEADER
// ==========================

void printHeader() {
  Serial.println();
  Serial.println("================================");
  Serial.print("BOIA PISCINA - ");
  Serial.println(FIRMWARE_VERSION);
  Serial.println("ESP32-C6 + DS18B20 + SHT41 + bateria GPIO1 + microSD + Wi-Fi + MQTT + HA + OTA");
  Serial.println("v1.18 - microSD com a caixa negra: historics diaris, stats, logs, blackbox i buffer MQTT");
  Serial.println("================================");

  Serial.print("GPIO sonda DS18B20: GPIO");
  Serial.println(ONE_WIRE_PIN);

  Serial.print("SHT41 I2C: SDA GPIO");
  Serial.print(INTERNAL_ENV_I2C_SDA_PIN);
  Serial.print(" · SCL GPIO");
  Serial.println(INTERNAL_ENV_I2C_SCL_PIN);

  Serial.print("ADC bateria: GPIO");
  Serial.print(BATTERY_VOLTAGE_ADC_PIN);
  Serial.print(" · divisor x");
  Serial.println(BATTERY_DIVIDER_RATIO, 2);

  Serial.print("microSD SPI: CS GPIO");
  Serial.print(SD_SPI_CS_PIN);
  Serial.print(" · MOSI GPIO");
  Serial.print(SD_SPI_MOSI_PIN);
  Serial.print(" · CLK GPIO");
  Serial.print(SD_SPI_CLK_PIN);
  Serial.print(" · MISO GPIO");
  Serial.println(SD_SPI_MISO_PIN);

  Serial.print("GPIO boto fisic: ");
  Serial.println(RESET_BUTTON_ENABLED ? ("GPIO" + String(RESET_BUTTON_PIN)) : "desactivat");

  Serial.print("GPIO LED estat: ");
  Serial.println(STATUS_LED_ENABLED ? ("GPIO" + String(STATUS_LED_PIN)) : "desactivat");

  Serial.print("Interval lectura: ");
  Serial.print(configReadIntervalSeconds);
  Serial.println(" segons");

  Serial.print("Decimals temperatura: ");
  Serial.println(configTemperatureDecimals);

  Serial.print("Wi-Fi SSID: ");
  Serial.println(configWifiSsid);

  Serial.print("MQTT activat: ");
  Serial.println(configMqttEnabled ? "si" : "no");

  Serial.print("MQTT host: ");
  Serial.println(configMqttHost);

  Serial.print("MQTT port: ");
  Serial.println(configMqttPort);

  Serial.print("MQTT topic base: ");
  Serial.println(configMqttTopicBase);

  Serial.print("HA Discovery activat: ");
  Serial.println(configHaDiscoveryEnabled ? "si" : "no");

  Serial.print("HA prefix: ");
  Serial.println(configHaDiscoveryPrefix);

  Serial.print("HA device ID: ");
  Serial.println(configHaDeviceId);

  Serial.print("Interval publicacio MQTT: ");
  Serial.print(configMqttPublishIntervalSeconds);
  Serial.println(" segons");

  Serial.print("Offset temperatura: ");
  Serial.println(configTemperatureOffsetC);

  Serial.print("Rang valid temperatura: ");
  Serial.print(configMinValidTempC);
  Serial.print(" - ");
  Serial.println(configMaxValidTempC);

  Serial.print("IP fixa: ");
  Serial.println(configWifiUseStaticIp ? "si" : "no");

  Serial.print("Web port: ");
  Serial.println(WEB_SERVER_PORT);

  Serial.print("Mode dispositiu: ");
  Serial.println(configProductionMode ? "produccio" : "desenvolupament");

  Serial.print("GitHub OTA: ");
  Serial.println(configGithubOtaEnabled ? "si" : "no");

  Serial.print("GitHub manifest: ");
  Serial.println(configGithubManifestUrl);

  Serial.println("================================");
}

// ==========================
// SETUP / LOOP
// ==========================

void setup() {
  Serial.begin(115200);
  delay(2000);

  loadConfig();
  printHeader();

  initTemperatureSensor();
  initHardwareManager();
  initInternalEnvSensor();
  initBatteryMonitor();
  initSdManager();

  connectWifi();

  setupWebServer();

  initMqtt();

  Serial.println();
  Serial.println("Sistema preparat. Comencen lectures...");
}

void loop() {
  unsigned long now = millis();

  handleWebServer();
  mqttLoop();
  handleHardwareManager();
  handleSdManager();

  if (now - appState.lastWifiCheckMillis >= WIFI_CHECK_INTERVAL_SECONDS * 1000UL || appState.lastWifiCheckMillis == 0) {
    appState.lastWifiCheckMillis = now;
    checkWifiConnection();
  }

  if (configMqttEnabled && (now - appState.lastMqttCheckMillis >= MQTT_CHECK_INTERVAL_SECONDS * 1000UL || appState.lastMqttCheckMillis == 0)) {
    appState.lastMqttCheckMillis = now;
    checkMqttConnection();
  }

  if (now - appState.lastReadMillis >= (unsigned long)configReadIntervalSeconds * 1000UL || appState.lastReadMillis == 0) {
    appState.lastReadMillis = now;
    performTemperatureRead();
    performInternalEnvRead();
    performBatteryRead();
    appendSdHistoryRecord();
    cycleReadDone = true;
  }

  if (configMqttEnabled && (now - appState.lastMqttPublishMillis >= (unsigned long)configMqttPublishIntervalSeconds * 1000UL || appState.lastMqttPublishMillis == 0)) {
    appState.lastMqttPublishMillis = now;
    publishMqttTelemetry();
    cyclePublishDone = true;
  }

  if (!configMqttEnabled) {
    cyclePublishDone = true;
  }

  maybeEnterLowPowerSleep(now);
}
