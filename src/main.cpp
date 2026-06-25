#include <Arduino.h>

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
  }

  if (configMqttEnabled && (now - appState.lastMqttPublishMillis >= (unsigned long)configMqttPublishIntervalSeconds * 1000UL || appState.lastMqttPublishMillis == 0)) {
    appState.lastMqttPublishMillis = now;
    publishMqttTelemetry();
  }
}
