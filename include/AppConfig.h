#pragma once

#include <Arduino.h>

// Wi-Fi per defecte / fallback inicial
extern const char* DEFAULT_WIFI_SSID;
extern const char* DEFAULT_WIFI_PASSWORD;
extern const char* WIFI_AP_SSID;
extern const unsigned long WIFI_CONNECT_TIMEOUT_MS;

// MQTT per defecte
extern const bool DEFAULT_MQTT_ENABLED;
extern const char* DEFAULT_MQTT_HOST;
extern const uint16_t DEFAULT_MQTT_PORT;
extern const char* DEFAULT_MQTT_USER;
extern const char* DEFAULT_MQTT_PASSWORD;
extern const char* DEFAULT_MQTT_TOPIC_BASE;

// Home Assistant Discovery per defecte
extern const bool DEFAULT_HA_DISCOVERY_ENABLED;
extern const char* DEFAULT_HA_DISCOVERY_PREFIX;
extern const char* DEFAULT_HA_DEVICE_ID;
extern const char* DEFAULT_HA_DEVICE_NAME;

// Home Assistant API local per historial
extern const bool DEFAULT_HA_API_ENABLED;
extern const char* DEFAULT_HA_API_URL;
extern const char* DEFAULT_HA_API_TOKEN;
extern const char* DEFAULT_HA_HISTORY_ENTITY_ID;
extern const uint16_t DEFAULT_HA_HISTORY_HOURS;
extern const uint16_t MIN_HA_HISTORY_HOURS;
extern const uint16_t MAX_HA_HISTORY_HOURS;

// Identitat dispositiu base
extern const char* DEVICE_ID;
extern const char* DEVICE_NAME;
extern const char* DEFAULT_DEVICE_HOSTNAME;
extern const char* FIRMWARE_VERSION;
extern const char* FIRMWARE_CHANGE_TITLE;
extern const char* FIRMWARE_CHANGE_NOTES;
extern const char* DEFAULT_GITHUB_MANIFEST_URL;
extern const bool DEFAULT_GITHUB_OTA_ENABLED;
extern const bool DEFAULT_GITHUB_ALLOW_SAME_VERSION_UPDATE;
extern const bool DEFAULT_PRODUCTION_MODE;
extern const bool DEFAULT_BOARD_LED_ENABLED;
extern const bool DEFAULT_BOARD_LED_MIRROR_STATUS;
extern const bool DEFAULT_INTERNAL_ENV_ALARM_ENABLED;
extern const float DEFAULT_INTERNAL_TEMP_ALARM_C;
extern const float DEFAULT_INTERNAL_HUMIDITY_ALARM_PERCENT;

// Ampliacions futures preparades/documentades
extern const char* FUTURE_INTERNAL_ENV_SENSOR;
extern const char* FUTURE_BATTERY_MONITOR;
extern const char* FUTURE_SOLAR_CHARGER;
extern const char* FUTURE_EXPANSION_BUS;

// Xarxa avançada per defecte
extern const bool DEFAULT_WIFI_USE_STATIC_IP;
extern const char* DEFAULT_WIFI_STATIC_IP;
extern const char* DEFAULT_WIFI_GATEWAY;
extern const char* DEFAULT_WIFI_SUBNET;
extern const char* DEFAULT_WIFI_DNS1;
extern const char* DEFAULT_WIFI_DNS2;

// Calibratge sonda per defecte
extern const float DEFAULT_TEMPERATURE_OFFSET_C;
extern const float DEFAULT_MIN_VALID_TEMP_C;
extern const float DEFAULT_MAX_VALID_TEMP_C;
extern const float ABSOLUTE_MIN_VALID_TEMP_C;
extern const float ABSOLUTE_MAX_VALID_TEMP_C;
extern const float MIN_TEMPERATURE_OFFSET_C;
extern const float MAX_TEMPERATURE_OFFSET_C;

// Web server HTTP
extern const uint16_t WEB_SERVER_PORT;

// GPIO utilitzats
#define ONE_WIRE_PIN 4
#define RESET_BUTTON_PIN 9
#define STATUS_LED_PIN 15

// LED intern de placa. A molts ESP32-C6 DevKitC-1 és un LED RGB adreçable a GPIO8.
// Si la teva placa no el porta o no funciona, deixa configBoardLedEnabled=false des de la web.
#define INTERNAL_BOARD_LED_PIN 8
#define INTERNAL_BOARD_LED_IS_RGB 1

// Bus I2C del sensor ambiental intern SHT41.
#define INTERNAL_ENV_I2C_SDA_PIN 6
#define INTERNAL_ENV_I2C_SCL_PIN 7
#define INTERNAL_ENV_I2C_ADDRESS 0x44
#define BATTERY_VOLTAGE_ADC_PIN -1
#define SOLAR_VOLTAGE_ADC_PIN -1
#define CHARGER_STATUS_PIN -1

extern const bool RESET_BUTTON_ENABLED;
extern const bool RESET_BUTTON_ACTIVE_LOW;
extern const bool STATUS_LED_ENABLED;
extern const bool STATUS_LED_ACTIVE_LOW;

extern const unsigned long BUTTON_DEBOUNCE_MS;
extern const unsigned long BUTTON_RESTART_MS;
extern const unsigned long BUTTON_WIFI_RESET_MS;
extern const unsigned long BUTTON_FACTORY_RESET_MS;

// Limits configuracio web
extern const uint16_t MIN_READ_INTERVAL_SECONDS;
extern const uint16_t MAX_READ_INTERVAL_SECONDS;

extern const uint16_t MIN_MQTT_INTERVAL_SECONDS;
extern const uint16_t MAX_MQTT_INTERVAL_SECONDS;

extern const uint8_t MIN_TEMPERATURE_DECIMALS;
extern const uint8_t MAX_TEMPERATURE_DECIMALS;

// Valors per defecte
extern const uint16_t DEFAULT_READ_INTERVAL_SECONDS;
extern const uint16_t DEFAULT_MQTT_PUBLISH_INTERVAL_SECONDS;
extern const uint8_t DEFAULT_TEMPERATURE_DECIMALS;

// Intervals interns fixos
extern const unsigned long WIFI_CHECK_INTERVAL_SECONDS;
extern const unsigned long MQTT_CHECK_INTERVAL_SECONDS;

// Configuracio persistent: identitat dispositiu
extern String configDeviceName;
extern String configDeviceHostname;
extern bool configProductionMode;
extern bool configBoardLedEnabled;
extern bool configBoardLedMirrorStatus;
extern bool configGithubOtaEnabled;
extern String configGithubManifestUrl;
extern bool configGithubAllowSameVersionUpdate;
extern bool configInternalEnvAlarmEnabled;
extern float configInternalTempAlarmC;
extern float configInternalHumidityAlarmPercent;

// Configuracio persistent: temperatura
extern uint16_t configReadIntervalSeconds;
extern uint8_t configTemperatureDecimals;
extern float configTemperatureOffsetC;
extern float configMinValidTempC;
extern float configMaxValidTempC;

// Configuracio persistent: Wi-Fi
extern String configWifiSsid;
extern String configWifiPassword;

// Configuracio persistent: xarxa avançada
extern bool configWifiUseStaticIp;
extern String configWifiStaticIp;
extern String configWifiGateway;
extern String configWifiSubnet;
extern String configWifiDns1;
extern String configWifiDns2;

// Configuracio persistent: MQTT
extern bool configMqttEnabled;
extern String configMqttHost;
extern uint16_t configMqttPort;
extern String configMqttUser;
extern String configMqttPassword;
extern String configMqttTopicBase;
extern uint16_t configMqttPublishIntervalSeconds;

// Configuracio persistent: Home Assistant Discovery
extern bool configHaDiscoveryEnabled;
extern String configHaDiscoveryPrefix;
extern String configHaDeviceId;
extern String configHaDeviceName;

// Configuracio persistent: Home Assistant API local
extern bool configHaApiEnabled;
extern String configHaApiUrl;
extern String configHaApiToken;
extern String configHaHistoryEntityId;
extern uint16_t configHaHistoryHours;

void loadConfig();
void saveConfig();
void resetConfigToDefaults();

void saveSensorConfig(float offsetC, float minValidC, float maxValidC);
void resetSensorConfigToDefaults();

void saveWifiConfig(const String& ssid, const String& password);
void resetWifiConfigToDefaults();
void forceWifiSetupMode();
void factoryResetConfigAndSetupMode();
bool hasWifiConfig();

void saveNetworkConfig(
  bool useStaticIp,
  const String& staticIp,
  const String& gateway,
  const String& subnet,
  const String& dns1,
  const String& dns2
);

void resetNetworkConfigToDhcp();
bool isValidIpString(const String& value);

void saveMqttConfig(
  bool enabled,
  const String& host,
  uint16_t port,
  const String& user,
  const String& password,
  const String& topicBase,
  uint16_t publishIntervalSeconds
);

void resetMqttConfigToDefaults();
String normalizedMqttTopicBase(const String& topicBase);

void saveHomeAssistantConfig(
  bool discoveryEnabled,
  const String& discoveryPrefix,
  const String& deviceId,
  const String& deviceName
);

void resetHomeAssistantConfigToDefaults();
void saveHomeAssistantApiConfig(bool apiEnabled, const String& apiUrl, const String& apiToken, const String& historyEntityId, uint16_t historyHours);
String normalizedHaApiUrl(const String& apiUrl);
String normalizedHaEntityId(const String& entityId);
String normalizedHaDiscoveryPrefix(const String& prefix);
String normalizedHaDeviceId(const String& deviceId);
String normalizedDeviceName(const String& deviceName);
String normalizedDeviceHostname(const String& hostname);
void saveDeviceIdentity(const String& deviceName, const String& hostname);
void saveDeviceMode(bool productionMode);
void saveBoardLedConfig(bool enabled, bool mirrorStatus);
void saveInternalEnvAlarmConfig(bool enabled, float temperatureC, float humidityPercent);
void saveGithubOtaConfig(bool enabled, const String& manifestUrl, bool allowSameVersionUpdate);
String normalizedGithubManifestUrl(const String& manifestUrl);
