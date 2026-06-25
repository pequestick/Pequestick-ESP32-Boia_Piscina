#include "AppConfig.h"
#include "Utils.h"
#include <Preferences.h>

// ==========================
// CONFIGURACIO FIXA
// ==========================

// Wi-Fi per defecte. Es pot canviar des de la web i queda guardat a Preferences.
const char* DEFAULT_WIFI_SSID = "";
const char* DEFAULT_WIFI_PASSWORD = "";
const char* WIFI_AP_SSID = "BOIA-PISCINA-SETUP";
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;

// MQTT per defecte. Es pot canviar des de la web i, parcialment, des de Home Assistant.
const bool DEFAULT_MQTT_ENABLED = true;
const char* DEFAULT_MQTT_HOST = "";
const uint16_t DEFAULT_MQTT_PORT = 1883;
const char* DEFAULT_MQTT_USER = "";
const char* DEFAULT_MQTT_PASSWORD = "";
const char* DEFAULT_MQTT_TOPIC_BASE = "boia_piscina";

// Home Assistant Discovery per defecte.
const bool DEFAULT_HA_DISCOVERY_ENABLED = true;
const char* DEFAULT_HA_DISCOVERY_PREFIX = "homeassistant";
const char* DEFAULT_HA_DEVICE_ID = "boia_piscina";
const char* DEFAULT_HA_DEVICE_NAME = "Boia Piscina";

// Home Assistant API local. Serveix per llegir historial i dibuixar gràfics a la web.
const bool DEFAULT_HA_API_ENABLED = false;
const char* DEFAULT_HA_API_URL = "http://homeassistant.local:8123";
const char* DEFAULT_HA_API_TOKEN = "";
const char* DEFAULT_HA_HISTORY_ENTITY_ID = "sensor.boia_piscina_temperature";
const char* DEFAULT_HA_INTERNAL_TEMPERATURE_ENTITY_ID = "sensor.boia_piscina_internal_temperature";
const char* DEFAULT_HA_INTERNAL_HUMIDITY_ENTITY_ID = "sensor.boia_piscina_internal_humidity";
const char* DEFAULT_HA_BATTERY_ENTITY_ID = "sensor.boia_piscina_battery";
const uint16_t DEFAULT_HA_HISTORY_HOURS = 168;
const uint16_t MIN_HA_HISTORY_HOURS = 1;
const uint16_t MAX_HA_HISTORY_HOURS = 168;

// Identitat dispositiu
const char* DEVICE_ID = "boia_piscina";
const char* DEVICE_NAME = "Boia Piscina";
const char* DEFAULT_DEVICE_HOSTNAME = "boia-piscina";
// Versio mestra del firmware. GitHub Actions llegeix aquesta constant
// automaticament per generar firmware/manifest.json.
const char* FIRMWARE_VERSION = "1.16.0-sd-history";
const char* FIRMWARE_CHANGE_TITLE = "v1.16.0 microSD i historic local";
const char* FIRMWARE_CHANGE_NOTES = "Afegeix suport microSD per SPI, pagina SD/Historic, lectura d'espai ocupat, descarrega CSV i guardat local de lectures.";
const char* DEFAULT_GITHUB_MANIFEST_URL = "https://raw.githubusercontent.com/pequestick/Pequestick-ESP32-Boia_Piscina/main/firmware/manifest.json";
const bool DEFAULT_GITHUB_OTA_ENABLED = true;
const bool DEFAULT_GITHUB_ALLOW_SAME_VERSION_UPDATE = false;
const bool DEFAULT_PRODUCTION_MODE = false;
const bool DEFAULT_BOARD_LED_ENABLED = false;
const bool DEFAULT_BOARD_LED_MIRROR_STATUS = true;
const bool DEFAULT_INTERNAL_ENV_ALARM_ENABLED = true;
const float DEFAULT_INTERNAL_TEMP_ALARM_C = 50.0f;
const float DEFAULT_INTERNAL_HUMIDITY_ALARM_PERCENT = 80.0f;

// Bateria: BAT+ -> 100k -> GPIO1 -> 100k -> GND.
// Pensat per una bateria Li-Ion/LiPo 1S. El percentatge és una estimació lineal.
const float BATTERY_DIVIDER_RATIO = 2.0f;
const float BATTERY_CALIBRATION_FACTOR = 1.0f;
const float BATTERY_EMPTY_VOLTAGE = 3.20f;
const float BATTERY_FULL_VOLTAGE = 4.20f;
const float BATTERY_LOW_PERCENT = 20.0f;
const uint8_t BATTERY_ADC_SAMPLES = 16;

// microSD SPI. El modul de la foto porta pins 3V3/CS/MOSI/CLK/MISO/GND.
// Es fa servir SPI a baixa velocitat per ser tolerant amb cables Dupont curts.
const bool SD_CARD_ENABLED = true;
const uint32_t SD_SPI_FREQUENCY_HZ = 4000000;
const char* SD_HISTORY_DIR = "/boia";
const char* SD_HISTORY_FILE = "/boia/history.csv";

// Ampliacions futures. Documentades a la web, no actives encara.
const char* FUTURE_INTERNAL_ENV_SENSOR = "SHT41 actiu per temperatura i humitat interna";
const char* FUTURE_BATTERY_MONITOR = "Lectura activa de tensio bateria via divisor 100k/100k + ADC GPIO1";
const char* FUTURE_SOLAR_CHARGER = "Placa solar + carregador Li-Ion/LiFePO4 amb proteccio";
const char* FUTURE_EXPANSION_BUS = "I2C intern reservat per sensors ambientals";

// Xarxa avançada per defecte. DHCP per defecte: és el més segur.
const bool DEFAULT_WIFI_USE_STATIC_IP = false;
const char* DEFAULT_WIFI_STATIC_IP = "192.168.5.180";
const char* DEFAULT_WIFI_GATEWAY = "192.168.5.1";
const char* DEFAULT_WIFI_SUBNET = "255.255.255.0";
const char* DEFAULT_WIFI_DNS1 = "192.168.5.1";
const char* DEFAULT_WIFI_DNS2 = "8.8.8.8";

// Calibratge i rang logic de la sonda
const float DEFAULT_TEMPERATURE_OFFSET_C = 0.0f;
const float DEFAULT_MIN_VALID_TEMP_C = -5.0f;
const float DEFAULT_MAX_VALID_TEMP_C = 60.0f;
const float ABSOLUTE_MIN_VALID_TEMP_C = -20.0f;
const float ABSOLUTE_MAX_VALID_TEMP_C = 80.0f;
const float MIN_TEMPERATURE_OFFSET_C = -10.0f;
const float MAX_TEMPERATURE_OFFSET_C = 10.0f;

// Web server HTTP
const uint16_t WEB_SERVER_PORT = 80;

// Hardware fisic
// ONE_WIRE_PIN és a AppConfig.h perquè la llibreria OneWire el necessita com a macro.
const bool RESET_BUTTON_ENABLED = true;
const bool RESET_BUTTON_ACTIVE_LOW = true;
const bool STATUS_LED_ENABLED = true;
const bool STATUS_LED_ACTIVE_LOW = false;
const unsigned long BUTTON_DEBOUNCE_MS = 40;
const unsigned long BUTTON_RESTART_MS = 5000;
const unsigned long BUTTON_WIFI_RESET_MS = 10000;
const unsigned long BUTTON_FACTORY_RESET_MS = 20000;

// Limits configuracio web
const uint16_t MIN_READ_INTERVAL_SECONDS = 5;
const uint16_t MAX_READ_INTERVAL_SECONDS = 3600;

const uint16_t MIN_MQTT_INTERVAL_SECONDS = 10;
const uint16_t MAX_MQTT_INTERVAL_SECONDS = 3600;

const uint8_t MIN_TEMPERATURE_DECIMALS = 0;
const uint8_t MAX_TEMPERATURE_DECIMALS = 3;

// Valors per defecte
const uint16_t DEFAULT_READ_INTERVAL_SECONDS = 10;
const uint16_t DEFAULT_MQTT_PUBLISH_INTERVAL_SECONDS = 30;
const uint8_t DEFAULT_TEMPERATURE_DECIMALS = 2;

// Intervals interns fixos
const unsigned long WIFI_CHECK_INTERVAL_SECONDS = 15;
const unsigned long MQTT_CHECK_INTERVAL_SECONDS = 10;

// ==========================
// CONFIGURACIO PERSISTENT
// ==========================

uint16_t configReadIntervalSeconds = DEFAULT_READ_INTERVAL_SECONDS;
uint8_t configTemperatureDecimals = DEFAULT_TEMPERATURE_DECIMALS;
float configTemperatureOffsetC = DEFAULT_TEMPERATURE_OFFSET_C;
float configMinValidTempC = DEFAULT_MIN_VALID_TEMP_C;
float configMaxValidTempC = DEFAULT_MAX_VALID_TEMP_C;

String configWifiSsid = DEFAULT_WIFI_SSID;
String configWifiPassword = DEFAULT_WIFI_PASSWORD;
static bool configWifiForceSetupAp = false;

bool configWifiUseStaticIp = DEFAULT_WIFI_USE_STATIC_IP;
String configWifiStaticIp = DEFAULT_WIFI_STATIC_IP;
String configWifiGateway = DEFAULT_WIFI_GATEWAY;
String configWifiSubnet = DEFAULT_WIFI_SUBNET;
String configWifiDns1 = DEFAULT_WIFI_DNS1;
String configWifiDns2 = DEFAULT_WIFI_DNS2;

bool configMqttEnabled = DEFAULT_MQTT_ENABLED;
String configMqttHost = DEFAULT_MQTT_HOST;
uint16_t configMqttPort = DEFAULT_MQTT_PORT;
String configMqttUser = DEFAULT_MQTT_USER;
String configMqttPassword = DEFAULT_MQTT_PASSWORD;
String configMqttTopicBase = DEFAULT_MQTT_TOPIC_BASE;
uint16_t configMqttPublishIntervalSeconds = DEFAULT_MQTT_PUBLISH_INTERVAL_SECONDS;

bool configHaDiscoveryEnabled = DEFAULT_HA_DISCOVERY_ENABLED;
String configHaDiscoveryPrefix = DEFAULT_HA_DISCOVERY_PREFIX;
String configHaDeviceId = DEFAULT_HA_DEVICE_ID;
String configHaDeviceName = DEFAULT_HA_DEVICE_NAME;

bool configHaApiEnabled = DEFAULT_HA_API_ENABLED;
String configHaApiUrl = DEFAULT_HA_API_URL;
String configHaApiToken = DEFAULT_HA_API_TOKEN;
String configHaHistoryEntityId = DEFAULT_HA_HISTORY_ENTITY_ID;
String configHaInternalTemperatureEntityId = DEFAULT_HA_INTERNAL_TEMPERATURE_ENTITY_ID;
String configHaInternalHumidityEntityId = DEFAULT_HA_INTERNAL_HUMIDITY_ENTITY_ID;
String configHaBatteryEntityId = DEFAULT_HA_BATTERY_ENTITY_ID;
uint16_t configHaHistoryHours = DEFAULT_HA_HISTORY_HOURS;

String configDeviceName = DEVICE_NAME;
String configDeviceHostname = DEFAULT_DEVICE_HOSTNAME;
bool configProductionMode = DEFAULT_PRODUCTION_MODE;
bool configBoardLedEnabled = DEFAULT_BOARD_LED_ENABLED;
bool configBoardLedMirrorStatus = DEFAULT_BOARD_LED_MIRROR_STATUS;
bool configGithubOtaEnabled = DEFAULT_GITHUB_OTA_ENABLED;
String configGithubManifestUrl = DEFAULT_GITHUB_MANIFEST_URL;
bool configGithubAllowSameVersionUpdate = DEFAULT_GITHUB_ALLOW_SAME_VERSION_UPDATE;
bool configInternalEnvAlarmEnabled = DEFAULT_INTERNAL_ENV_ALARM_ENABLED;
float configInternalTempAlarmC = DEFAULT_INTERNAL_TEMP_ALARM_C;
float configInternalHumidityAlarmPercent = DEFAULT_INTERNAL_HUMIDITY_ALARM_PERCENT;

static Preferences preferences;

static String trimCopy(String value) {
  value.trim();
  return value;
}


static String normalizedHaApiToken(String token) {
  token.trim();

  // Accept both a raw Home Assistant long-lived token and a copied
  // Authorization header like "Bearer eyJ...". Store only the raw token.
  if (token.startsWith("Bearer ")) {
    token = token.substring(7);
    token.trim();
  }
  if (token.startsWith("bearer ")) {
    token = token.substring(7);
    token.trim();
  }

  // Remove accidental wrapping quotes.
  if (token.length() >= 2 && ((token.charAt(0) == '"' && token.charAt(token.length() - 1) == '"') || (token.charAt(0) == '\'' && token.charAt(token.length() - 1) == '\''))) {
    token = token.substring(1, token.length() - 1);
    token.trim();
  }

  return token;
}

static float clampFloat(float value, float minValue, float maxValue) {
  if (isnan(value)) {
    return minValue;
  }
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

bool isValidIpString(const String& value) {
  IPAddress ip;
  String clean = trimCopy(value);
  return clean.length() > 0 && ip.fromString(clean);
}

String normalizedMqttTopicBase(const String& topicBase) {
  String clean = trimCopy(topicBase);

  while (clean.startsWith("/")) {
    clean.remove(0, 1);
  }

  while (clean.endsWith("/")) {
    clean.remove(clean.length() - 1);
  }

  if (clean.length() == 0) {
    clean = DEFAULT_MQTT_TOPIC_BASE;
  }

  return clean;
}

String normalizedHaDiscoveryPrefix(const String& prefix) {
  String clean = trimCopy(prefix);

  while (clean.startsWith("/")) {
    clean.remove(0, 1);
  }

  while (clean.endsWith("/")) {
    clean.remove(clean.length() - 1);
  }

  if (clean.length() == 0) {
    clean = DEFAULT_HA_DISCOVERY_PREFIX;
  }

  return clean;
}

String normalizedHaDeviceId(const String& deviceId) {
  String clean = trimCopy(deviceId);
  clean.toLowerCase();
  clean.replace(" ", "_");
  clean.replace("/", "_");
  clean.replace("\\", "_");

  if (clean.length() == 0) {
    clean = DEFAULT_HA_DEVICE_ID;
  }

  return clean;
}



String normalizedHaApiUrl(const String& apiUrl) {
  String clean = trimCopy(apiUrl);

  while (clean.endsWith("/")) {
    clean.remove(clean.length() - 1);
  }

  if (clean.length() == 0) {
    clean = DEFAULT_HA_API_URL;
  }

  return clean;
}

String normalizedHaEntityId(const String& entityId) {
  String clean = trimCopy(entityId);
  clean.toLowerCase();
  clean.replace(" ", "_");

  if (clean.length() == 0) {
    clean = DEFAULT_HA_HISTORY_ENTITY_ID;
  }

  return clean;
}

String normalizedGithubManifestUrl(const String& manifestUrl) {
  String clean = trimCopy(manifestUrl);

  // Correccions habituals quan es copia la URL des del navegador.
  // GitHub /blob/... és una pàgina HTML; l'ESP32 necessita la URL raw.
  clean.replace("https://github.com/", "https://raw.githubusercontent.com/");
  clean.replace("http://github.com/", "http://raw.githubusercontent.com/");
  clean.replace("/blob/main/", "/main/");
  clean.replace("/blob/master/", "/master/");

  // Error tipic escrit a ma: raw.githubuser... en comptes de raw.githubusercontent...
  clean.replace("raw.githubuser.com", "raw.githubusercontent.com");
  clean.replace("raw.githubusercontents.com", "raw.githubusercontent.com");
  clean.replace("raw.githubuserusercontent.com", "raw.githubusercontent.com");

  if (clean.length() == 0) {
    clean = DEFAULT_GITHUB_MANIFEST_URL;
  }
  return clean;
}

String normalizedDeviceName(const String& deviceName) {
  String clean = trimCopy(deviceName);
  if (clean.length() == 0) {
    clean = DEVICE_NAME;
  }
  if (clean.length() > 48) {
    clean = clean.substring(0, 48);
  }
  return clean;
}

String normalizedDeviceHostname(const String& hostname) {
  String clean = trimCopy(hostname);
  clean.toLowerCase();
  clean.replace(" ", "-");
  clean.replace("_", "-");

  String out = "";
  bool lastDash = false;
  for (uint16_t i = 0; i < clean.length(); i++) {
    char c = clean.charAt(i);
    bool valid = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    if (!valid) {
      continue;
    }
    if (c == '-') {
      if (out.length() == 0 || lastDash) {
        continue;
      }
      lastDash = true;
    } else {
      lastDash = false;
    }
    out += c;
  }

  while (out.endsWith("-")) {
    out.remove(out.length() - 1);
  }

  if (out.length() == 0) {
    out = DEFAULT_DEVICE_HOSTNAME;
  }
  if (out.length() > 31) {
    out = out.substring(0, 31);
    while (out.endsWith("-")) {
      out.remove(out.length() - 1);
    }
  }
  return out;
}

void loadConfig() {
  preferences.begin("boia", true);

  configReadIntervalSeconds = preferences.getUShort("read_int", DEFAULT_READ_INTERVAL_SECONDS);
  configTemperatureDecimals = preferences.getUChar("decimals", DEFAULT_TEMPERATURE_DECIMALS);
  configTemperatureOffsetC = preferences.getFloat("temp_off", DEFAULT_TEMPERATURE_OFFSET_C);
  configMinValidTempC = preferences.getFloat("temp_min", DEFAULT_MIN_VALID_TEMP_C);
  configMaxValidTempC = preferences.getFloat("temp_max", DEFAULT_MAX_VALID_TEMP_C);

  configWifiForceSetupAp = preferences.getBool("wifi_force", false);
  configWifiSsid = trimCopy(preferences.getString("wifi_ssid", DEFAULT_WIFI_SSID));
  configWifiPassword = preferences.getString("wifi_pass", DEFAULT_WIFI_PASSWORD);

  configWifiUseStaticIp = preferences.getBool("wifi_static", DEFAULT_WIFI_USE_STATIC_IP);
  configWifiStaticIp = trimCopy(preferences.getString("wifi_ip", DEFAULT_WIFI_STATIC_IP));
  configWifiGateway = trimCopy(preferences.getString("wifi_gw", DEFAULT_WIFI_GATEWAY));
  configWifiSubnet = trimCopy(preferences.getString("wifi_mask", DEFAULT_WIFI_SUBNET));
  configWifiDns1 = trimCopy(preferences.getString("wifi_dns1", DEFAULT_WIFI_DNS1));
  configWifiDns2 = trimCopy(preferences.getString("wifi_dns2", DEFAULT_WIFI_DNS2));

  configMqttEnabled = preferences.getBool("mqtt_en", DEFAULT_MQTT_ENABLED);
  configMqttHost = trimCopy(preferences.getString("mqtt_host", DEFAULT_MQTT_HOST));
  configMqttPort = preferences.getUShort("mqtt_port", DEFAULT_MQTT_PORT);
  configMqttUser = preferences.getString("mqtt_user", DEFAULT_MQTT_USER);
  configMqttPassword = preferences.getString("mqtt_pass", DEFAULT_MQTT_PASSWORD);
  configMqttTopicBase = normalizedMqttTopicBase(preferences.getString("mqtt_base", DEFAULT_MQTT_TOPIC_BASE));
  configMqttPublishIntervalSeconds = preferences.getUShort("mqtt_int", DEFAULT_MQTT_PUBLISH_INTERVAL_SECONDS);

  configHaDiscoveryEnabled = preferences.getBool("ha_disc", DEFAULT_HA_DISCOVERY_ENABLED);
  configHaDiscoveryPrefix = normalizedHaDiscoveryPrefix(preferences.getString("ha_pref", DEFAULT_HA_DISCOVERY_PREFIX));
  configHaDeviceId = normalizedHaDeviceId(preferences.getString("ha_devid", DEFAULT_HA_DEVICE_ID));
  configHaDeviceName = trimCopy(preferences.getString("ha_devname", DEFAULT_HA_DEVICE_NAME));

  configHaApiEnabled = preferences.getBool("ha_api_en", DEFAULT_HA_API_ENABLED);
  configHaApiUrl = normalizedHaApiUrl(preferences.getString("ha_api_url", DEFAULT_HA_API_URL));
  configHaApiToken = normalizedHaApiToken(preferences.getString("ha_api_tok", DEFAULT_HA_API_TOKEN));
  configHaHistoryEntityId = normalizedHaEntityId(preferences.getString("ha_hist_ent", DEFAULT_HA_HISTORY_ENTITY_ID));
  configHaInternalTemperatureEntityId = normalizedHaEntityId(preferences.getString("ha_int_temp", DEFAULT_HA_INTERNAL_TEMPERATURE_ENTITY_ID));
  configHaInternalHumidityEntityId = normalizedHaEntityId(preferences.getString("ha_int_hum", DEFAULT_HA_INTERNAL_HUMIDITY_ENTITY_ID));
  configHaBatteryEntityId = trimCopy(preferences.getString("ha_battery", DEFAULT_HA_BATTERY_ENTITY_ID));
  if (configHaBatteryEntityId.length() > 0) configHaBatteryEntityId = normalizedHaEntityId(configHaBatteryEntityId);
  configHaHistoryHours = preferences.getUShort("ha_hist_hrs", DEFAULT_HA_HISTORY_HOURS);

  configDeviceName = normalizedDeviceName(preferences.getString("dev_name", DEVICE_NAME));
  configDeviceHostname = normalizedDeviceHostname(preferences.getString("dev_host", DEFAULT_DEVICE_HOSTNAME));
  configProductionMode = preferences.getBool("prod_mode", DEFAULT_PRODUCTION_MODE);
  configBoardLedEnabled = preferences.getBool("brd_led_en", DEFAULT_BOARD_LED_ENABLED);
  configBoardLedMirrorStatus = preferences.getBool("brd_led_mir", DEFAULT_BOARD_LED_MIRROR_STATUS);
  configGithubOtaEnabled = preferences.getBool("gh_ota_en", DEFAULT_GITHUB_OTA_ENABLED);
  configGithubManifestUrl = normalizedGithubManifestUrl(preferences.getString("gh_manifest", DEFAULT_GITHUB_MANIFEST_URL));
  configGithubAllowSameVersionUpdate = preferences.getBool("gh_same", DEFAULT_GITHUB_ALLOW_SAME_VERSION_UPDATE);
  configInternalEnvAlarmEnabled = preferences.getBool("env_alarm", DEFAULT_INTERNAL_ENV_ALARM_ENABLED);
  configInternalTempAlarmC = preferences.isKey("env_temp_hi")
    ? preferences.getFloat("env_temp_hi", DEFAULT_INTERNAL_TEMP_ALARM_C)
    : DEFAULT_INTERNAL_TEMP_ALARM_C;
  configInternalHumidityAlarmPercent = preferences.isKey("env_hum_hi")
    ? preferences.getFloat("env_hum_hi", DEFAULT_INTERNAL_HUMIDITY_ALARM_PERCENT)
    : DEFAULT_INTERNAL_HUMIDITY_ALARM_PERCENT;

  preferences.end();

  configReadIntervalSeconds = clampUint16(
    configReadIntervalSeconds,
    MIN_READ_INTERVAL_SECONDS,
    MAX_READ_INTERVAL_SECONDS
  );

  configMqttPublishIntervalSeconds = clampUint16(
    configMqttPublishIntervalSeconds,
    MIN_MQTT_INTERVAL_SECONDS,
    MAX_MQTT_INTERVAL_SECONDS
  );

  configHaHistoryHours = clampUint16(
    configHaHistoryHours,
    MIN_HA_HISTORY_HOURS,
    MAX_HA_HISTORY_HOURS
  );

  configTemperatureDecimals = clampUint8(
    configTemperatureDecimals,
    MIN_TEMPERATURE_DECIMALS,
    MAX_TEMPERATURE_DECIMALS
  );

  configTemperatureOffsetC = clampFloat(configTemperatureOffsetC, MIN_TEMPERATURE_OFFSET_C, MAX_TEMPERATURE_OFFSET_C);
  configMinValidTempC = clampFloat(configMinValidTempC, ABSOLUTE_MIN_VALID_TEMP_C, ABSOLUTE_MAX_VALID_TEMP_C);
  configMaxValidTempC = clampFloat(configMaxValidTempC, ABSOLUTE_MIN_VALID_TEMP_C, ABSOLUTE_MAX_VALID_TEMP_C);
  configInternalTempAlarmC = clampFloat(configInternalTempAlarmC, -20.0f, 85.0f);
  configInternalHumidityAlarmPercent = clampFloat(configInternalHumidityAlarmPercent, 1.0f, 100.0f);

  if (configMinValidTempC >= configMaxValidTempC) {
    configMinValidTempC = DEFAULT_MIN_VALID_TEMP_C;
    configMaxValidTempC = DEFAULT_MAX_VALID_TEMP_C;
  }

  if (configWifiUseStaticIp) {
    configWifiUseStaticIp = isValidIpString(configWifiStaticIp) && isValidIpString(configWifiGateway) && isValidIpString(configWifiSubnet);
  }

  if (!isValidIpString(configWifiStaticIp)) { configWifiStaticIp = DEFAULT_WIFI_STATIC_IP; }
  if (!isValidIpString(configWifiGateway)) { configWifiGateway = DEFAULT_WIFI_GATEWAY; }
  if (!isValidIpString(configWifiSubnet)) { configWifiSubnet = DEFAULT_WIFI_SUBNET; }
  if (!isValidIpString(configWifiDns1)) { configWifiDns1 = DEFAULT_WIFI_DNS1; }
  if (!isValidIpString(configWifiDns2)) { configWifiDns2 = DEFAULT_WIFI_DNS2; }

  if (configWifiForceSetupAp) {
    configWifiSsid = "";
    configWifiPassword = "";
  } else if (configWifiSsid.length() == 0) {
    configWifiSsid = DEFAULT_WIFI_SSID;
    configWifiPassword = DEFAULT_WIFI_PASSWORD;
  }

  if (configMqttHost.length() == 0) {
    configMqttHost = DEFAULT_MQTT_HOST;
  }

  if (configMqttPort == 0) {
    configMqttPort = DEFAULT_MQTT_PORT;
  }

  configMqttTopicBase = normalizedMqttTopicBase(configMqttTopicBase);
  configHaDiscoveryPrefix = normalizedHaDiscoveryPrefix(configHaDiscoveryPrefix);
  configHaDeviceId = normalizedHaDeviceId(configHaDeviceId);
  configHaApiUrl = normalizedHaApiUrl(configHaApiUrl);
  configHaHistoryEntityId = normalizedHaEntityId(configHaHistoryEntityId);
  configHaApiToken = normalizedHaApiToken(configHaApiToken);

  if (configHaDeviceName.length() == 0) {
    configHaDeviceName = configDeviceName.length() > 0 ? configDeviceName : String(DEFAULT_HA_DEVICE_NAME);
  }

  configDeviceName = normalizedDeviceName(configDeviceName);
  configDeviceHostname = normalizedDeviceHostname(configDeviceHostname);
  configGithubManifestUrl = normalizedGithubManifestUrl(configGithubManifestUrl);
}

void saveConfig() {
  preferences.begin("boia", false);

  preferences.putUShort("read_int", configReadIntervalSeconds);
  preferences.putUChar("decimals", configTemperatureDecimals);

  preferences.end();
}

void resetConfigToDefaults() {
  configReadIntervalSeconds = DEFAULT_READ_INTERVAL_SECONDS;
  configTemperatureDecimals = DEFAULT_TEMPERATURE_DECIMALS;

  saveConfig();
}


void saveSensorConfig(float offsetC, float minValidC, float maxValidC) {
  configTemperatureOffsetC = clampFloat(offsetC, MIN_TEMPERATURE_OFFSET_C, MAX_TEMPERATURE_OFFSET_C);
  configMinValidTempC = clampFloat(minValidC, ABSOLUTE_MIN_VALID_TEMP_C, ABSOLUTE_MAX_VALID_TEMP_C);
  configMaxValidTempC = clampFloat(maxValidC, ABSOLUTE_MIN_VALID_TEMP_C, ABSOLUTE_MAX_VALID_TEMP_C);

  if (configMinValidTempC >= configMaxValidTempC) {
    configMinValidTempC = DEFAULT_MIN_VALID_TEMP_C;
    configMaxValidTempC = DEFAULT_MAX_VALID_TEMP_C;
  }

  preferences.begin("boia", false);
  preferences.putFloat("temp_off", configTemperatureOffsetC);
  preferences.putFloat("temp_min", configMinValidTempC);
  preferences.putFloat("temp_max", configMaxValidTempC);
  preferences.end();
}

void resetSensorConfigToDefaults() {
  configTemperatureOffsetC = DEFAULT_TEMPERATURE_OFFSET_C;
  configMinValidTempC = DEFAULT_MIN_VALID_TEMP_C;
  configMaxValidTempC = DEFAULT_MAX_VALID_TEMP_C;

  preferences.begin("boia", false);
  preferences.remove("temp_off");
  preferences.remove("temp_min");
  preferences.remove("temp_max");
  preferences.end();
}

void saveWifiConfig(const String& ssid, const String& password) {
  String cleanSsid = trimCopy(ssid);

  if (cleanSsid.length() == 0) {
    return;
  }

  configWifiSsid = cleanSsid;
  configWifiPassword = password;
  configWifiForceSetupAp = false;

  preferences.begin("boia", false);
  preferences.putBool("wifi_force", false);
  preferences.putString("wifi_ssid", configWifiSsid);
  preferences.putString("wifi_pass", configWifiPassword);
  preferences.end();
}

void resetWifiConfigToDefaults() {
  configWifiSsid = DEFAULT_WIFI_SSID;
  configWifiPassword = DEFAULT_WIFI_PASSWORD;
  configWifiForceSetupAp = false;

  preferences.begin("boia", false);
  preferences.putBool("wifi_force", false);
  preferences.remove("wifi_ssid");
  preferences.remove("wifi_pass");
  preferences.end();
}

void forceWifiSetupMode() {
  configWifiSsid = "";
  configWifiPassword = "";
  configWifiForceSetupAp = true;

  preferences.begin("boia", false);
  preferences.putBool("wifi_force", true);
  preferences.putString("wifi_ssid", "");
  preferences.putString("wifi_pass", "");
  preferences.end();
}

void factoryResetConfigAndSetupMode() {
  preferences.begin("boia", false);
  preferences.clear();
  preferences.putBool("wifi_force", true);
  preferences.end();

  // El reset físic també restaura l'accés web inicial.
  preferences.begin("boia_auth", false);
  preferences.clear();
  preferences.end();

  configReadIntervalSeconds = DEFAULT_READ_INTERVAL_SECONDS;
  configTemperatureDecimals = DEFAULT_TEMPERATURE_DECIMALS;
  configTemperatureOffsetC = DEFAULT_TEMPERATURE_OFFSET_C;
  configMinValidTempC = DEFAULT_MIN_VALID_TEMP_C;
  configMaxValidTempC = DEFAULT_MAX_VALID_TEMP_C;

  configWifiSsid = "";
  configWifiPassword = "";
  configWifiForceSetupAp = true;
  configWifiUseStaticIp = DEFAULT_WIFI_USE_STATIC_IP;
  configWifiStaticIp = DEFAULT_WIFI_STATIC_IP;
  configWifiGateway = DEFAULT_WIFI_GATEWAY;
  configWifiSubnet = DEFAULT_WIFI_SUBNET;
  configWifiDns1 = DEFAULT_WIFI_DNS1;
  configWifiDns2 = DEFAULT_WIFI_DNS2;

  configMqttEnabled = DEFAULT_MQTT_ENABLED;
  configMqttHost = DEFAULT_MQTT_HOST;
  configMqttPort = DEFAULT_MQTT_PORT;
  configMqttUser = DEFAULT_MQTT_USER;
  configMqttPassword = DEFAULT_MQTT_PASSWORD;
  configMqttTopicBase = DEFAULT_MQTT_TOPIC_BASE;
  configMqttPublishIntervalSeconds = DEFAULT_MQTT_PUBLISH_INTERVAL_SECONDS;

  configHaDiscoveryEnabled = DEFAULT_HA_DISCOVERY_ENABLED;
  configHaDiscoveryPrefix = DEFAULT_HA_DISCOVERY_PREFIX;
  configHaDeviceId = DEFAULT_HA_DEVICE_ID;
  configHaDeviceName = DEFAULT_HA_DEVICE_NAME;
  configHaApiEnabled = DEFAULT_HA_API_ENABLED;
  configHaApiUrl = DEFAULT_HA_API_URL;
  configHaApiToken = DEFAULT_HA_API_TOKEN;
  configHaHistoryEntityId = DEFAULT_HA_HISTORY_ENTITY_ID;
  configHaInternalTemperatureEntityId = DEFAULT_HA_INTERNAL_TEMPERATURE_ENTITY_ID;
  configHaInternalHumidityEntityId = DEFAULT_HA_INTERNAL_HUMIDITY_ENTITY_ID;
  configHaBatteryEntityId = DEFAULT_HA_BATTERY_ENTITY_ID;

  configDeviceName = DEVICE_NAME;
  configDeviceHostname = DEFAULT_DEVICE_HOSTNAME;
  configProductionMode = DEFAULT_PRODUCTION_MODE;
  configBoardLedEnabled = DEFAULT_BOARD_LED_ENABLED;
  configBoardLedMirrorStatus = DEFAULT_BOARD_LED_MIRROR_STATUS;
  configGithubOtaEnabled = DEFAULT_GITHUB_OTA_ENABLED;
  configGithubManifestUrl = DEFAULT_GITHUB_MANIFEST_URL;
  configGithubAllowSameVersionUpdate = DEFAULT_GITHUB_ALLOW_SAME_VERSION_UPDATE;
  configInternalEnvAlarmEnabled = DEFAULT_INTERNAL_ENV_ALARM_ENABLED;
  configInternalTempAlarmC = DEFAULT_INTERNAL_TEMP_ALARM_C;
  configInternalHumidityAlarmPercent = DEFAULT_INTERNAL_HUMIDITY_ALARM_PERCENT;
}

bool hasWifiConfig() {
  return !configWifiForceSetupAp && configWifiSsid.length() > 0;
}


void saveNetworkConfig(
  bool useStaticIp,
  const String& staticIp,
  const String& gateway,
  const String& subnet,
  const String& dns1,
  const String& dns2
) {
  String cleanIp = trimCopy(staticIp);
  String cleanGateway = trimCopy(gateway);
  String cleanSubnet = trimCopy(subnet);
  String cleanDns1 = trimCopy(dns1);
  String cleanDns2 = trimCopy(dns2);

  bool validStaticConfig = isValidIpString(cleanIp) && isValidIpString(cleanGateway) && isValidIpString(cleanSubnet);

  configWifiUseStaticIp = useStaticIp && validStaticConfig;
  configWifiStaticIp = isValidIpString(cleanIp) ? cleanIp : String(DEFAULT_WIFI_STATIC_IP);
  configWifiGateway = isValidIpString(cleanGateway) ? cleanGateway : String(DEFAULT_WIFI_GATEWAY);
  configWifiSubnet = isValidIpString(cleanSubnet) ? cleanSubnet : String(DEFAULT_WIFI_SUBNET);
  configWifiDns1 = isValidIpString(cleanDns1) ? cleanDns1 : String(DEFAULT_WIFI_DNS1);
  configWifiDns2 = isValidIpString(cleanDns2) ? cleanDns2 : String(DEFAULT_WIFI_DNS2);

  preferences.begin("boia", false);
  preferences.putBool("wifi_static", configWifiUseStaticIp);
  preferences.putString("wifi_ip", configWifiStaticIp);
  preferences.putString("wifi_gw", configWifiGateway);
  preferences.putString("wifi_mask", configWifiSubnet);
  preferences.putString("wifi_dns1", configWifiDns1);
  preferences.putString("wifi_dns2", configWifiDns2);
  preferences.end();
}

void resetNetworkConfigToDhcp() {
  configWifiUseStaticIp = DEFAULT_WIFI_USE_STATIC_IP;
  configWifiStaticIp = DEFAULT_WIFI_STATIC_IP;
  configWifiGateway = DEFAULT_WIFI_GATEWAY;
  configWifiSubnet = DEFAULT_WIFI_SUBNET;
  configWifiDns1 = DEFAULT_WIFI_DNS1;
  configWifiDns2 = DEFAULT_WIFI_DNS2;

  preferences.begin("boia", false);
  preferences.remove("wifi_static");
  preferences.remove("wifi_ip");
  preferences.remove("wifi_gw");
  preferences.remove("wifi_mask");
  preferences.remove("wifi_dns1");
  preferences.remove("wifi_dns2");
  preferences.end();
}

void saveMqttConfig(
  bool enabled,
  const String& host,
  uint16_t port,
  const String& user,
  const String& password,
  const String& topicBase,
  uint16_t publishIntervalSeconds
) {
  String cleanHost = trimCopy(host);
  String cleanTopicBase = normalizedMqttTopicBase(topicBase);

  if (cleanHost.length() == 0) {
    cleanHost = DEFAULT_MQTT_HOST;
  }

  if (port == 0) {
    port = DEFAULT_MQTT_PORT;
  }

  configMqttEnabled = enabled;
  configMqttHost = cleanHost;
  configMqttPort = port;
  configMqttUser = user;
  configMqttPassword = password;
  configMqttTopicBase = cleanTopicBase;
  configMqttPublishIntervalSeconds = clampUint16(
    publishIntervalSeconds,
    MIN_MQTT_INTERVAL_SECONDS,
    MAX_MQTT_INTERVAL_SECONDS
  );

  preferences.begin("boia", false);
  preferences.putBool("mqtt_en", configMqttEnabled);
  preferences.putString("mqtt_host", configMqttHost);
  preferences.putUShort("mqtt_port", configMqttPort);
  preferences.putString("mqtt_user", configMqttUser);
  preferences.putString("mqtt_pass", configMqttPassword);
  preferences.putString("mqtt_base", configMqttTopicBase);
  preferences.putUShort("mqtt_int", configMqttPublishIntervalSeconds);
  preferences.end();
}

void resetMqttConfigToDefaults() {
  configMqttEnabled = DEFAULT_MQTT_ENABLED;
  configMqttHost = DEFAULT_MQTT_HOST;
  configMqttPort = DEFAULT_MQTT_PORT;
  configMqttUser = DEFAULT_MQTT_USER;
  configMqttPassword = DEFAULT_MQTT_PASSWORD;
  configMqttTopicBase = DEFAULT_MQTT_TOPIC_BASE;
  configMqttPublishIntervalSeconds = DEFAULT_MQTT_PUBLISH_INTERVAL_SECONDS;

  preferences.begin("boia", false);
  preferences.remove("mqtt_en");
  preferences.remove("mqtt_host");
  preferences.remove("mqtt_port");
  preferences.remove("mqtt_user");
  preferences.remove("mqtt_pass");
  preferences.remove("mqtt_base");
  preferences.remove("mqtt_int");
  preferences.end();
}


void saveDeviceIdentity(const String& deviceName, const String& hostname) {
  configDeviceName = normalizedDeviceName(deviceName);
  configDeviceHostname = normalizedDeviceHostname(hostname);

  // Per defecte, el nom de Home Assistant segueix el nom general del dispositiu.
  configHaDeviceName = configDeviceName;

  preferences.begin("boia", false);
  preferences.putString("dev_name", configDeviceName);
  preferences.putString("dev_host", configDeviceHostname);
  preferences.putString("ha_devname", configHaDeviceName);
  preferences.end();
}

void saveHomeAssistantConfig(
  bool discoveryEnabled,
  const String& discoveryPrefix,
  const String& deviceId,
  const String& deviceName
) {
  configHaDiscoveryEnabled = discoveryEnabled;
  configHaDiscoveryPrefix = normalizedHaDiscoveryPrefix(discoveryPrefix);
  configHaDeviceId = normalizedHaDeviceId(deviceId);
  configHaDeviceName = trimCopy(deviceName);

  if (configHaDeviceName.length() == 0) {
    configHaDeviceName = configDeviceName.length() > 0 ? configDeviceName : String(DEFAULT_HA_DEVICE_NAME);
  }

  configDeviceName = normalizedDeviceName(configDeviceName);
  configDeviceHostname = normalizedDeviceHostname(configDeviceHostname);

  preferences.begin("boia", false);
  preferences.putBool("ha_disc", configHaDiscoveryEnabled);
  preferences.putString("ha_pref", configHaDiscoveryPrefix);
  preferences.putString("ha_devid", configHaDeviceId);
  preferences.putString("ha_devname", configHaDeviceName);
  preferences.end();
}

void resetHomeAssistantConfigToDefaults() {
  configHaDiscoveryEnabled = DEFAULT_HA_DISCOVERY_ENABLED;
  configHaDiscoveryPrefix = DEFAULT_HA_DISCOVERY_PREFIX;
  configHaDeviceId = DEFAULT_HA_DEVICE_ID;
  configHaDeviceName = DEFAULT_HA_DEVICE_NAME;
  configHaApiEnabled = DEFAULT_HA_API_ENABLED;
  configHaApiUrl = DEFAULT_HA_API_URL;
  configHaApiToken = DEFAULT_HA_API_TOKEN;
  configHaHistoryEntityId = DEFAULT_HA_HISTORY_ENTITY_ID;
  configHaInternalTemperatureEntityId = DEFAULT_HA_INTERNAL_TEMPERATURE_ENTITY_ID;
  configHaInternalHumidityEntityId = DEFAULT_HA_INTERNAL_HUMIDITY_ENTITY_ID;
  configHaBatteryEntityId = DEFAULT_HA_BATTERY_ENTITY_ID;
  configHaHistoryHours = DEFAULT_HA_HISTORY_HOURS;

  preferences.begin("boia", false);
  preferences.remove("ha_disc");
  preferences.remove("ha_pref");
  preferences.remove("ha_devid");
  preferences.remove("ha_devname");
  preferences.remove("ha_api_en");
  preferences.remove("ha_api_url");
  preferences.remove("ha_api_tok");
  preferences.remove("ha_hist_ent");
  preferences.remove("ha_int_temp");
  preferences.remove("ha_int_hum");
  preferences.remove("ha_battery");
  preferences.remove("ha_hist_hrs");
  preferences.end();
}


void saveHomeAssistantApiConfig(bool apiEnabled, const String& apiUrl, const String& apiToken, const String& historyEntityId, uint16_t historyHours) {
  configHaApiEnabled = apiEnabled;
  configHaApiUrl = normalizedHaApiUrl(apiUrl);
  configHaApiToken = normalizedHaApiToken(apiToken);
  configHaHistoryEntityId = normalizedHaEntityId(historyEntityId);
  configHaHistoryHours = clampUint16(historyHours, MIN_HA_HISTORY_HOURS, MAX_HA_HISTORY_HOURS);

  preferences.begin("boia", false);
  preferences.putBool("ha_api_en", configHaApiEnabled);
  preferences.putString("ha_api_url", configHaApiUrl);
  preferences.putString("ha_api_tok", configHaApiToken);
  preferences.putString("ha_hist_ent", configHaHistoryEntityId);
  preferences.putUShort("ha_hist_hrs", configHaHistoryHours);
  preferences.end();
}

void saveHomeAssistantStatisticsEntities(const String& internalTemperatureEntityId, const String& internalHumidityEntityId, const String& batteryEntityId) {
  configHaInternalTemperatureEntityId = normalizedHaEntityId(internalTemperatureEntityId);
  configHaInternalHumidityEntityId = normalizedHaEntityId(internalHumidityEntityId);
  configHaBatteryEntityId = trimCopy(batteryEntityId);
  if (configHaBatteryEntityId.length() > 0) configHaBatteryEntityId = normalizedHaEntityId(configHaBatteryEntityId);

  preferences.begin("boia", false);
  preferences.putString("ha_int_temp", configHaInternalTemperatureEntityId);
  preferences.putString("ha_int_hum", configHaInternalHumidityEntityId);
  if (configHaBatteryEntityId.length() > 0) preferences.putString("ha_battery", configHaBatteryEntityId);
  else preferences.remove("ha_battery");
  preferences.end();
}

void saveDeviceMode(bool productionMode) {
  configProductionMode = productionMode;
  preferences.begin("boia", false);
  preferences.putBool("prod_mode", configProductionMode);
  preferences.end();
}


void saveBoardLedConfig(bool enabled, bool mirrorStatus) {
  configBoardLedEnabled = enabled;
  configBoardLedMirrorStatus = mirrorStatus;

  preferences.begin("boia", false);
  preferences.putBool("brd_led_en", configBoardLedEnabled);
  preferences.putBool("brd_led_mir", configBoardLedMirrorStatus);
  preferences.end();
}

void saveInternalEnvAlarmConfig(bool enabled, float temperatureC, float humidityPercent) {
  configInternalEnvAlarmEnabled = enabled;
  configInternalTempAlarmC = clampFloat(temperatureC, -20.0f, 85.0f);
  configInternalHumidityAlarmPercent = clampFloat(humidityPercent, 1.0f, 100.0f);

  preferences.begin("boia", false);
  preferences.putBool("env_alarm", configInternalEnvAlarmEnabled);
  preferences.putFloat("env_temp_hi", configInternalTempAlarmC);
  preferences.putFloat("env_hum_hi", configInternalHumidityAlarmPercent);
  preferences.end();
}


void saveGithubOtaConfig(bool enabled, const String& manifestUrl, bool allowSameVersionUpdate) {
  configGithubOtaEnabled = enabled;
  configGithubManifestUrl = normalizedGithubManifestUrl(manifestUrl);
  configGithubAllowSameVersionUpdate = allowSameVersionUpdate;

  preferences.begin("boia", false);
  preferences.putBool("gh_ota_en", configGithubOtaEnabled);
  preferences.putString("gh_manifest", configGithubManifestUrl);
  preferences.putBool("gh_same", configGithubAllowSameVersionUpdate);
  preferences.end();
}
