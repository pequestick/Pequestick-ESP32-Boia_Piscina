#include "MqttManager.h"

#include <WiFi.h>
#include <PubSubClient.h>

#include "AppConfig.h"
#include "AppState.h"
#include "Utils.h"

// ==========================
// OBJECTES MQTT
// ==========================

static WiFiClient wifiClient;
static PubSubClient mqttClient(wifiClient);

// ==========================
// TOPICS
// ==========================

String mqttTopic(const String& suffix) {
  String base = normalizedMqttTopicBase(configMqttTopicBase);
  String cleanSuffix = suffix;

  while (cleanSuffix.startsWith("/")) {
    cleanSuffix.remove(0, 1);
  }

  if (cleanSuffix.length() == 0) {
    return base;
  }

  return base + "/" + cleanSuffix;
}

String mqttAvailabilityTopic() {
  return mqttTopic("availability");
}

String mqttCommandRestartTopic() {
  return mqttTopic("command/restart");
}

String mqttCommandPublishDiscoveryTopic() {
  return mqttTopic("command/publish_discovery");
}

String haDiscoveryBaseTopic() {
  return normalizedHaDiscoveryPrefix(configHaDiscoveryPrefix) + "/" + normalizedHaDeviceId(configHaDeviceId);
}

static String discoveryTopic(const String& component, const String& objectId) {
  return normalizedHaDiscoveryPrefix(configHaDiscoveryPrefix) + "/" + component + "/" + normalizedHaDeviceId(configHaDeviceId) + "/" + objectId + "/config";
}

static String configStateTopic(const String& key) {
  return mqttTopic("config/" + key + "/state");
}

static String configSetTopic(const String& key) {
  return mqttTopic("config/" + key + "/set");
}

static String floatPayload(float value, uint8_t decimals) {
  unsigned int decimalPlaces = decimals;
  return String(value, decimalPlaces);
}

// ==========================
// ESTAT
// ==========================

bool isMqttConnected() {
  return mqttClient.connected();
}

String mqttStatusText() {
  if (!configMqttEnabled) {
    return "Desactivat";
  }

  if (mqttClient.connected()) {
    return "Connectat";
  }

  return "Desconnectat";
}

// ==========================
// PUBLICACIO BASE
// ==========================

static bool mqttPublishRetained(const String& topic, const String& payload) {
  if (!configMqttEnabled || !mqttClient.connected()) {
    return false;
  }

  bool ok = mqttClient.publish(topic.c_str(), payload.c_str(), true);

  if (!ok) {
    appState.mqttFailCount++;
    Serial.print("ERROR MQTT publicant a ");
    Serial.println(topic);
  }

  return ok;
}

static bool mqttPublishNotRetained(const String& topic, const String& payload) {
  if (!configMqttEnabled || !mqttClient.connected()) {
    return false;
  }

  bool ok = mqttClient.publish(topic.c_str(), payload.c_str(), false);

  if (!ok) {
    appState.mqttFailCount++;
    Serial.print("ERROR MQTT publicant a ");
    Serial.println(topic);
  }

  return ok;
}

// ==========================
// HOME ASSISTANT DISCOVERY HELPERS
// ==========================

static String buildDeviceInfoJsonPart() {
  String deviceInfo = "";

  deviceInfo += "\"device\":{";
  deviceInfo += "\"identifiers\":[\"" + jsonEscape(configHaDeviceId) + "\"],";
  deviceInfo += "\"name\":\"" + jsonEscape(configHaDeviceName) + "\",";
  deviceInfo += "\"manufacturer\":\"Oriol Lab\",";
  deviceInfo += "\"model\":\"ESP32-C6 DS18B20 + SHT41\",";
  deviceInfo += "\"sw_version\":\"" + String(FIRMWARE_VERSION) + "\"";
  deviceInfo += "}";

  return deviceInfo;
}

static String appendCommonAvailabilityAndDevice(const String& extraFields) {
  String json = "";

  json += "\"availability_topic\":\"" + jsonEscape(mqttAvailabilityTopic()) + "\",";
  json += "\"payload_available\":\"online\",";
  json += "\"payload_not_available\":\"offline\"";

  if (extraFields.length() > 0) {
    json += ",";
    json += extraFields;
  }

  json += ",";
  json += buildDeviceInfoJsonPart();

  return json;
}

static String buildBaseSensorConfig(
  const String& name,
  const String& uniqueId,
  const String& stateTopic,
  const String& extraFields
) {
  String json = "{";

  json += "\"name\":\"" + jsonEscape(name) + "\",";
  json += "\"unique_id\":\"" + jsonEscape(uniqueId) + "\",";
  json += "\"state_topic\":\"" + jsonEscape(stateTopic) + "\",";
  json += appendCommonAvailabilityAndDevice(extraFields);
  json += "}";

  return json;
}

static String buildBinarySensorConfig(
  const String& name,
  const String& uniqueId,
  const String& stateTopic,
  const String& extraFields
) {
  String json = "{";

  json += "\"name\":\"" + jsonEscape(name) + "\",";
  json += "\"unique_id\":\"" + jsonEscape(uniqueId) + "\",";
  json += "\"state_topic\":\"" + jsonEscape(stateTopic) + "\",";
  json += "\"payload_on\":\"online\",";
  json += "\"payload_off\":\"offline\",";
  json += appendCommonAvailabilityAndDevice(extraFields);
  json += "}";

  return json;
}

static String buildAlarmBinarySensorConfig(
  const String& name,
  const String& uniqueId,
  const String& stateTopic,
  const String& extraFields
) {
  String json = "{";
  json += "\"name\":\"" + jsonEscape(name) + "\",";
  json += "\"unique_id\":\"" + jsonEscape(uniqueId) + "\",";
  json += "\"state_topic\":\"" + jsonEscape(stateTopic) + "\",";
  json += "\"payload_on\":\"ON\",";
  json += "\"payload_off\":\"OFF\",";
  json += appendCommonAvailabilityAndDevice(extraFields);
  json += "}";
  return json;
}

static String buildButtonConfig(
  const String& name,
  const String& uniqueId,
  const String& commandTopic,
  const String& payloadPress,
  const String& extraFields
) {
  String json = "{";

  json += "\"name\":\"" + jsonEscape(name) + "\",";
  json += "\"unique_id\":\"" + jsonEscape(uniqueId) + "\",";
  json += "\"command_topic\":\"" + jsonEscape(commandTopic) + "\",";
  json += "\"payload_press\":\"" + jsonEscape(payloadPress) + "\",";
  json += appendCommonAvailabilityAndDevice(extraFields);
  json += "}";

  return json;
}

static String buildNumberConfig(
  const String& name,
  const String& uniqueId,
  const String& stateTopic,
  const String& commandTopic,
  uint16_t minValue,
  uint16_t maxValue,
  uint16_t stepValue,
  const String& extraFields
) {
  String json = "{";

  json += "\"name\":\"" + jsonEscape(name) + "\",";
  json += "\"unique_id\":\"" + jsonEscape(uniqueId) + "\",";
  json += "\"state_topic\":\"" + jsonEscape(stateTopic) + "\",";
  json += "\"command_topic\":\"" + jsonEscape(commandTopic) + "\",";
  json += "\"min\":" + String(minValue) + ",";
  json += "\"max\":" + String(maxValue) + ",";
  json += "\"step\":" + String(stepValue) + ",";
  json += "\"mode\":\"box\",";
  json += appendCommonAvailabilityAndDevice(extraFields);
  json += "}";

  return json;
}

static String buildNumberConfigFloat(
  const String& name,
  const String& uniqueId,
  const String& stateTopic,
  const String& commandTopic,
  float minValue,
  float maxValue,
  float stepValue,
  const String& extraFields
) {
  String json = "{";

  json += "\"name\":\"" + jsonEscape(name) + "\",";
  json += "\"unique_id\":\"" + jsonEscape(uniqueId) + "\",";
  json += "\"state_topic\":\"" + jsonEscape(stateTopic) + "\",";
  json += "\"command_topic\":\"" + jsonEscape(commandTopic) + "\",";
  json += "\"min\":" + floatPayload(minValue, 1) + ",";
  json += "\"max\":" + floatPayload(maxValue, 1) + ",";
  json += "\"step\":" + floatPayload(stepValue, 1) + ",";
  json += "\"mode\":\"box\",";
  json += appendCommonAvailabilityAndDevice(extraFields);
  json += "}";

  return json;
}

static String buildSwitchConfig(
  const String& name,
  const String& uniqueId,
  const String& stateTopic,
  const String& commandTopic,
  const String& extraFields
) {
  String json = "{";

  json += "\"name\":\"" + jsonEscape(name) + "\",";
  json += "\"unique_id\":\"" + jsonEscape(uniqueId) + "\",";
  json += "\"state_topic\":\"" + jsonEscape(stateTopic) + "\",";
  json += "\"command_topic\":\"" + jsonEscape(commandTopic) + "\",";
  json += "\"payload_on\":\"ON\",";
  json += "\"payload_off\":\"OFF\",";
  json += "\"state_on\":\"ON\",";
  json += "\"state_off\":\"OFF\",";
  json += appendCommonAvailabilityAndDevice(extraFields);
  json += "}";

  return json;
}

// ==========================
// PUBLICACIO CONFIG STATE
// ==========================

void publishMqttConfigState() {
  if (!configMqttEnabled || !mqttClient.connected()) {
    return;
  }

  mqttPublishRetained(configStateTopic("read_interval"), String(configReadIntervalSeconds));
  mqttPublishRetained(configStateTopic("temperature_decimals"), String(configTemperatureDecimals));
  mqttPublishRetained(configStateTopic("mqtt_publish_interval"), String(configMqttPublishIntervalSeconds));
  mqttPublishRetained(configStateTopic("mqtt_enabled"), configMqttEnabled ? "ON" : "OFF");
  mqttPublishRetained(configStateTopic("temperature_offset"), floatPayload(configTemperatureOffsetC, 2));
  mqttPublishRetained(configStateTopic("min_valid_temperature"), floatPayload(configMinValidTempC, 2));
  mqttPublishRetained(configStateTopic("max_valid_temperature"), floatPayload(configMaxValidTempC, 2));
  mqttPublishRetained(configStateTopic("internal_env_alarms"), configInternalEnvAlarmEnabled ? "ON" : "OFF");
  mqttPublishRetained(configStateTopic("internal_temp_alarm"), floatPayload(configInternalTempAlarmC, 1));
  mqttPublishRetained(configStateTopic("internal_humidity_alarm"), floatPayload(configInternalHumidityAlarmPercent, 1));
}


void publishHomeAssistantDiscovery() {
  if (!configMqttEnabled || !mqttClient.connected()) {
    return;
  }

  if (!configHaDiscoveryEnabled) {
    Serial.println("Home Assistant Discovery desactivat per configuracio.");
    return;
  }

  Serial.println();
  Serial.println("Publicant MQTT Discovery Home Assistant...");

  String deviceId = normalizedHaDeviceId(configHaDeviceId);

  String tempExtra = "";
  tempExtra += "\"unit_of_measurement\":\"°C\",";
  tempExtra += "\"device_class\":\"temperature\",";
  tempExtra += "\"state_class\":\"measurement\"";

  String humidityExtra = "";
  humidityExtra += "\"unit_of_measurement\":\"%\",";
  humidityExtra += "\"device_class\":\"humidity\",";
  humidityExtra += "\"state_class\":\"measurement\"";

  String rssiExtra = "";
  rssiExtra += "\"unit_of_measurement\":\"dBm\",";
  rssiExtra += "\"device_class\":\"signal_strength\",";
  rssiExtra += "\"state_class\":\"measurement\",";
  rssiExtra += "\"entity_category\":\"diagnostic\"";

  String uptimeExtra = "";
  uptimeExtra += "\"unit_of_measurement\":\"s\",";
  uptimeExtra += "\"device_class\":\"duration\",";
  uptimeExtra += "\"state_class\":\"total_increasing\",";
  uptimeExtra += "\"entity_category\":\"diagnostic\"";

  String ipExtra = "";
  ipExtra += "\"icon\":\"mdi:ip-network\",";
  ipExtra += "\"entity_category\":\"diagnostic\"";

  String totalReadsExtra = "";
  totalReadsExtra += "\"state_class\":\"total_increasing\",";
  totalReadsExtra += "\"icon\":\"mdi:counter\",";
  totalReadsExtra += "\"entity_category\":\"diagnostic\"";

  String validReadsExtra = "";
  validReadsExtra += "\"state_class\":\"total_increasing\",";
  validReadsExtra += "\"icon\":\"mdi:check-circle\",";
  validReadsExtra += "\"entity_category\":\"diagnostic\"";

  String failedReadsExtra = "";
  failedReadsExtra += "\"state_class\":\"total_increasing\",";
  failedReadsExtra += "\"icon\":\"mdi:alert-circle\",";
  failedReadsExtra += "\"entity_category\":\"diagnostic\"";

  String onlineExtra = "";
  onlineExtra += "\"device_class\":\"connectivity\",";
  onlineExtra += "\"entity_category\":\"diagnostic\"";

  String restartExtra = "";
  restartExtra += "\"device_class\":\"restart\",";
  restartExtra += "\"entity_category\":\"config\"";

  String configExtra = "";
  configExtra += "\"entity_category\":\"config\"";

  String secondsExtra = "";
  secondsExtra += "\"unit_of_measurement\":\"s\",";
  secondsExtra += "\"entity_category\":\"config\"";

  String tempConfigExtra = "";
  tempConfigExtra += "\"unit_of_measurement\":\"°C\",";
  tempConfigExtra += "\"entity_category\":\"config\"";

  String sensorStatusExtra = "";
  sensorStatusExtra += "\"icon\":\"mdi:thermometer-alert\",";
  sensorStatusExtra += "\"entity_category\":\"diagnostic\"";

  String consecutiveErrorsExtra = "";
  consecutiveErrorsExtra += "\"state_class\":\"measurement\",";
  consecutiveErrorsExtra += "\"icon\":\"mdi:counter\",";
  consecutiveErrorsExtra += "\"entity_category\":\"diagnostic\"";

  bool ok = true;

  ok &= mqttPublishRetained(
    discoveryTopic("sensor", "temperature"),
    buildBaseSensorConfig("Temperatura aigua", deviceId + "_temperature", mqttTopic("temperature"), tempExtra)
  );

  ok &= mqttPublishRetained(
    discoveryTopic("sensor", "internal_temperature"),
    buildBaseSensorConfig("Temperatura interior de la boia", deviceId + "_internal_temperature", mqttTopic("internal_temperature"), tempExtra)
  );

  ok &= mqttPublishRetained(
    discoveryTopic("sensor", "internal_humidity"),
    buildBaseSensorConfig("Humitat interior de la boia", deviceId + "_internal_humidity", mqttTopic("internal_humidity"), humidityExtra)
  );

  String internalTempAlarmExtra = "\"device_class\":\"heat\",\"icon\":\"mdi:thermometer-alert\"";
  String internalHumidityAlarmExtra = "\"device_class\":\"moisture\",\"icon\":\"mdi:water-alert\"";
  ok &= mqttPublishRetained(
    discoveryTopic("binary_sensor", "internal_temperature_alarm"),
    buildAlarmBinarySensorConfig("Alarma temperatura interior de la boia", deviceId + "_internal_temperature_alarm", mqttTopic("internal_temperature_alarm"), internalTempAlarmExtra)
  );
  ok &= mqttPublishRetained(
    discoveryTopic("binary_sensor", "internal_humidity_alarm"),
    buildAlarmBinarySensorConfig("Alarma humitat interior de la boia", deviceId + "_internal_humidity_alarm", mqttTopic("internal_humidity_alarm"), internalHumidityAlarmExtra)
  );

  ok &= mqttPublishRetained(
    discoveryTopic("sensor", "rssi"),
    buildBaseSensorConfig("RSSI", deviceId + "_rssi", mqttTopic("rssi"), rssiExtra)
  );

  ok &= mqttPublishRetained(
    discoveryTopic("sensor", "uptime"),
    buildBaseSensorConfig("Uptime", deviceId + "_uptime", mqttTopic("uptime"), uptimeExtra)
  );

  ok &= mqttPublishRetained(
    discoveryTopic("sensor", "ip"),
    buildBaseSensorConfig("IP Local", deviceId + "_ip", mqttTopic("ip"), ipExtra)
  );

  ok &= mqttPublishRetained(
    discoveryTopic("sensor", "total_reads"),
    buildBaseSensorConfig("Lectures totals", deviceId + "_total_reads", mqttTopic("total_reads"), totalReadsExtra)
  );

  ok &= mqttPublishRetained(
    discoveryTopic("sensor", "valid_reads"),
    buildBaseSensorConfig("Lectures valides", deviceId + "_valid_reads", mqttTopic("valid_reads"), validReadsExtra)
  );

  ok &= mqttPublishRetained(
    discoveryTopic("sensor", "failed_reads"),
    buildBaseSensorConfig("Lectures fallides", deviceId + "_failed_reads", mqttTopic("failed_reads"), failedReadsExtra)
  );

  ok &= mqttPublishRetained(
    discoveryTopic("sensor", "sensor_status"),
    buildBaseSensorConfig("Estat sonda", deviceId + "_sensor_status", mqttTopic("sensor_status"), sensorStatusExtra)
  );

  ok &= mqttPublishRetained(
    discoveryTopic("sensor", "consecutive_sensor_errors"),
    buildBaseSensorConfig("Errors consecutius sonda", deviceId + "_consecutive_sensor_errors", mqttTopic("consecutive_sensor_errors"), consecutiveErrorsExtra)
  );

  ok &= mqttPublishRetained(
    discoveryTopic("binary_sensor", "online"),
    buildBinarySensorConfig("Online", deviceId + "_online", mqttAvailabilityTopic(), onlineExtra)
  );

  ok &= mqttPublishRetained(
    discoveryTopic("button", "restart"),
    buildButtonConfig("Reiniciar", deviceId + "_restart", mqttCommandRestartTopic(), "restart", restartExtra)
  );

  ok &= mqttPublishRetained(
    discoveryTopic("button", "publish_discovery"),
    buildButtonConfig("Publicar Discovery", deviceId + "_publish_discovery", mqttCommandPublishDiscoveryTopic(), "publish", configExtra)
  );

  ok &= mqttPublishRetained(
    discoveryTopic("number", "read_interval"),
    buildNumberConfig(
      "Interval lectura",
      deviceId + "_read_interval",
      configStateTopic("read_interval"),
      configSetTopic("read_interval"),
      MIN_READ_INTERVAL_SECONDS,
      MAX_READ_INTERVAL_SECONDS,
      1,
      secondsExtra
    )
  );

  ok &= mqttPublishRetained(
    discoveryTopic("number", "temperature_decimals"),
    buildNumberConfig(
      "Decimals temperatura",
      deviceId + "_temperature_decimals",
      configStateTopic("temperature_decimals"),
      configSetTopic("temperature_decimals"),
      MIN_TEMPERATURE_DECIMALS,
      MAX_TEMPERATURE_DECIMALS,
      1,
      configExtra
    )
  );

  ok &= mqttPublishRetained(
    discoveryTopic("number", "temperature_offset"),
    buildNumberConfigFloat(
      "Offset temperatura",
      deviceId + "_temperature_offset",
      configStateTopic("temperature_offset"),
      configSetTopic("temperature_offset"),
      MIN_TEMPERATURE_OFFSET_C,
      MAX_TEMPERATURE_OFFSET_C,
      0.1f,
      tempConfigExtra
    )
  );

  ok &= mqttPublishRetained(
    discoveryTopic("number", "min_valid_temperature"),
    buildNumberConfigFloat(
      "Temperatura minima valida",
      deviceId + "_min_valid_temperature",
      configStateTopic("min_valid_temperature"),
      configSetTopic("min_valid_temperature"),
      ABSOLUTE_MIN_VALID_TEMP_C,
      ABSOLUTE_MAX_VALID_TEMP_C,
      0.1f,
      tempConfigExtra
    )
  );

  ok &= mqttPublishRetained(
    discoveryTopic("number", "max_valid_temperature"),
    buildNumberConfigFloat(
      "Temperatura maxima valida",
      deviceId + "_max_valid_temperature",
      configStateTopic("max_valid_temperature"),
      configSetTopic("max_valid_temperature"),
      ABSOLUTE_MIN_VALID_TEMP_C,
      ABSOLUTE_MAX_VALID_TEMP_C,
      0.1f,
      tempConfigExtra
    )
  );

  ok &= mqttPublishRetained(
    discoveryTopic("number", "mqtt_publish_interval"),
    buildNumberConfig(
      "Interval publicacio MQTT",
      deviceId + "_mqtt_publish_interval",
      configStateTopic("mqtt_publish_interval"),
      configSetTopic("mqtt_publish_interval"),
      MIN_MQTT_INTERVAL_SECONDS,
      MAX_MQTT_INTERVAL_SECONDS,
      1,
      secondsExtra
    )
  );

  ok &= mqttPublishRetained(
    discoveryTopic("switch", "mqtt_enabled"),
    buildSwitchConfig(
      "MQTT activat",
      deviceId + "_mqtt_enabled",
      configStateTopic("mqtt_enabled"),
      configSetTopic("mqtt_enabled"),
      configExtra
    )
  );

  String internalAlarmConfigExtra = "\"entity_category\":\"config\",\"icon\":\"mdi:shield-alert\"";
  String humidityConfigExtra = "\"unit_of_measurement\":\"%\",\"entity_category\":\"config\"";
  ok &= mqttPublishRetained(
    discoveryTopic("switch", "internal_env_alarms"),
    buildSwitchConfig(
      "Alarmes ambient interior de la boia",
      deviceId + "_internal_env_alarms",
      configStateTopic("internal_env_alarms"),
      configSetTopic("internal_env_alarms"),
      internalAlarmConfigExtra
    )
  );
  ok &= mqttPublishRetained(
    discoveryTopic("number", "internal_temp_alarm"),
    buildNumberConfigFloat(
      "Llindar temperatura interior de la boia",
      deviceId + "_internal_temp_alarm",
      configStateTopic("internal_temp_alarm"),
      configSetTopic("internal_temp_alarm"),
      -20.0f,
      85.0f,
      0.1f,
      tempConfigExtra
    )
  );
  ok &= mqttPublishRetained(
    discoveryTopic("number", "internal_humidity_alarm"),
    buildNumberConfigFloat(
      "Llindar humitat interior de la boia",
      deviceId + "_internal_humidity_alarm",
      configStateTopic("internal_humidity_alarm"),
      configSetTopic("internal_humidity_alarm"),
      1.0f,
      100.0f,
      0.1f,
      humidityConfigExtra
    )
  );

  publishMqttConfigState();

  if (ok) {
    appState.mqttDiscoveryPublished = true;
    Serial.println("MQTT Discovery publicat correctament.");
  } else {
    Serial.println("ERROR: MQTT Discovery no s'ha publicat completament.");
  }
}

// ==========================
// COMANDES ENTRANTS
// ==========================

static bool payloadIsOn(const String& payloadText) {
  return payloadText == "on" || payloadText == "1" || payloadText == "true" || payloadText == "si" || payloadText == "sí" || payloadText == "enable" || payloadText == "enabled";
}

static bool payloadIsOff(const String& payloadText) {
  return payloadText == "off" || payloadText == "0" || payloadText == "false" || payloadText == "no" || payloadText == "disable" || payloadText == "disabled";
}

static void handleMqttMessage(char* topic, byte* payload, unsigned int length) {
  String topicText = String(topic);
  String payloadRaw = "";

  for (unsigned int i = 0; i < length; i++) {
    payloadRaw += (char)payload[i];
  }

  payloadRaw.trim();

  String payloadText = payloadRaw;
  payloadText.toLowerCase();

  Serial.print("MQTT missatge rebut a ");
  Serial.print(topicText);
  Serial.print(": ");
  Serial.println(payloadRaw);

  if (topicText == mqttCommandRestartTopic()) {
    if (payloadText == "restart" || payloadIsOn(payloadText)) {
      Serial.println("Reinici demanat per MQTT.");
      appState.mqttRestartRequested = true;
    }
    return;
  }

  if (topicText == mqttCommandPublishDiscoveryTopic()) {
    if (payloadText == "publish" || payloadText == "discovery" || payloadIsOn(payloadText)) {
      Serial.println("Republicacio Discovery demanada per MQTT.");
      appState.mqttDiscoveryRequested = true;
    }
    return;
  }

  if (topicText == configSetTopic("read_interval")) {
    uint16_t value = clampUint16(
      (uint16_t)payloadRaw.toInt(),
      MIN_READ_INTERVAL_SECONDS,
      MAX_READ_INTERVAL_SECONDS
    );
    configReadIntervalSeconds = value;
    saveConfig();
    appState.mqttConfigStatePublishRequested = true;
    Serial.print("Interval lectura canviat per MQTT a ");
    Serial.println(configReadIntervalSeconds);
    return;
  }

  if (topicText == configSetTopic("temperature_decimals")) {
    uint8_t value = clampUint8(
      (uint8_t)payloadRaw.toInt(),
      MIN_TEMPERATURE_DECIMALS,
      MAX_TEMPERATURE_DECIMALS
    );
    configTemperatureDecimals = value;
    saveConfig();
    appState.mqttConfigStatePublishRequested = true;
    Serial.print("Decimals temperatura canviats per MQTT a ");
    Serial.println(configTemperatureDecimals);
    return;
  }

  if (topicText == configSetTopic("temperature_offset")) {
    float value = payloadRaw.toFloat();
    saveSensorConfig(value, configMinValidTempC, configMaxValidTempC);
    appState.mqttConfigStatePublishRequested = true;
    Serial.print("Offset temperatura canviat per MQTT a ");
    Serial.println(configTemperatureOffsetC);
    return;
  }

  if (topicText == configSetTopic("min_valid_temperature")) {
    float value = payloadRaw.toFloat();
    if (value < configMaxValidTempC) {
      saveSensorConfig(configTemperatureOffsetC, value, configMaxValidTempC);
    }
    appState.mqttConfigStatePublishRequested = true;
    Serial.print("Temperatura minima valida canviada per MQTT a ");
    Serial.println(configMinValidTempC);
    return;
  }

  if (topicText == configSetTopic("max_valid_temperature")) {
    float value = payloadRaw.toFloat();
    if (value > configMinValidTempC) {
      saveSensorConfig(configTemperatureOffsetC, configMinValidTempC, value);
    }
    appState.mqttConfigStatePublishRequested = true;
    Serial.print("Temperatura maxima valida canviada per MQTT a ");
    Serial.println(configMaxValidTempC);
    return;
  }

  if (topicText == configSetTopic("mqtt_publish_interval")) {
    uint16_t value = clampUint16(
      (uint16_t)payloadRaw.toInt(),
      MIN_MQTT_INTERVAL_SECONDS,
      MAX_MQTT_INTERVAL_SECONDS
    );

    saveMqttConfig(
      configMqttEnabled,
      configMqttHost,
      configMqttPort,
      configMqttUser,
      configMqttPassword,
      configMqttTopicBase,
      value
    );

    appState.mqttConfigStatePublishRequested = true;
    Serial.print("Interval publicacio MQTT canviat per MQTT a ");
    Serial.println(configMqttPublishIntervalSeconds);
    return;
  }

  if (topicText == configSetTopic("mqtt_enabled")) {
    if (payloadIsOn(payloadText) || payloadIsOff(payloadText)) {
      bool newEnabled = payloadIsOn(payloadText);

      if (newEnabled != configMqttEnabled) {
        // Si es desactiva MQTT des de Home Assistant, publiquem OFF abans de tallar la connexio.
        if (!newEnabled && mqttClient.connected()) {
          mqttClient.publish(configStateTopic("mqtt_enabled").c_str(), "OFF", true);
        }

        saveMqttConfig(
          newEnabled,
          configMqttHost,
          configMqttPort,
          configMqttUser,
          configMqttPassword,
          configMqttTopicBase,
          configMqttPublishIntervalSeconds
        );

        appState.mqttConfigStatePublishRequested = true;
        appState.mqttReconfigureRequested = true;
        Serial.print("MQTT activat canviat per MQTT a ");
        Serial.println(configMqttEnabled ? "ON" : "OFF");
      } else {
        appState.mqttConfigStatePublishRequested = true;
      }
    }
    return;
  }

  if (topicText == configSetTopic("internal_env_alarms")) {
    if (payloadIsOn(payloadText) || payloadIsOff(payloadText)) {
      saveInternalEnvAlarmConfig(payloadIsOn(payloadText), configInternalTempAlarmC, configInternalHumidityAlarmPercent);
      appState.mqttConfigStatePublishRequested = true;
      appState.lastMqttPublishMillis = 0;
      Serial.print("Alarmes ambient interior de la boia canviades per MQTT a ");
      Serial.println(configInternalEnvAlarmEnabled ? "ON" : "OFF");
    }
    return;
  }

  if (topicText == configSetTopic("internal_temp_alarm")) {
    saveInternalEnvAlarmConfig(configInternalEnvAlarmEnabled, payloadRaw.toFloat(), configInternalHumidityAlarmPercent);
    appState.mqttConfigStatePublishRequested = true;
    appState.lastMqttPublishMillis = 0;
    Serial.print("Llindar temperatura interior de la boia canviat per MQTT a ");
    Serial.println(configInternalTempAlarmC);
    return;
  }

  if (topicText == configSetTopic("internal_humidity_alarm")) {
    saveInternalEnvAlarmConfig(configInternalEnvAlarmEnabled, configInternalTempAlarmC, payloadRaw.toFloat());
    appState.mqttConfigStatePublishRequested = true;
    appState.lastMqttPublishMillis = 0;
    Serial.print("Llindar humitat interior de la boia canviat per MQTT a ");
    Serial.println(configInternalHumidityAlarmPercent);
    return;
  }
}

static void subscribeOneTopic(const String& topic) {
  if (mqttClient.subscribe(topic.c_str())) {
    Serial.print("Subscrit a: ");
    Serial.println(topic);
  } else {
    Serial.print("ERROR subscrivint a: ");
    Serial.println(topic);
    appState.mqttFailCount++;
  }
}

static void subscribeMqttCommands() {
  if (!configMqttEnabled || !mqttClient.connected()) {
    return;
  }

  subscribeOneTopic(mqttCommandRestartTopic());
  subscribeOneTopic(mqttCommandPublishDiscoveryTopic());
  subscribeOneTopic(configSetTopic("read_interval"));
  subscribeOneTopic(configSetTopic("temperature_decimals"));
  subscribeOneTopic(configSetTopic("temperature_offset"));
  subscribeOneTopic(configSetTopic("min_valid_temperature"));
  subscribeOneTopic(configSetTopic("max_valid_temperature"));
  subscribeOneTopic(configSetTopic("mqtt_publish_interval"));
  subscribeOneTopic(configSetTopic("mqtt_enabled"));
  subscribeOneTopic(configSetTopic("internal_env_alarms"));
  subscribeOneTopic(configSetTopic("internal_temp_alarm"));
  subscribeOneTopic(configSetTopic("internal_humidity_alarm"));
}

// ==========================
// CONNEXIO
// ==========================

void connectMqtt() {
  if (!configMqttEnabled) {
    return;
  }

  if (mqttClient.connected()) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (configMqttHost.length() == 0) {
    return;
  }

  Serial.println();
  Serial.print("Connectant al broker MQTT ");
  Serial.print(configMqttHost);
  Serial.print(":");
  Serial.println(configMqttPort);

  String clientId = normalizedHaDeviceId(configHaDeviceId) + "_" + WiFi.macAddress();
  clientId.replace(":", "");

  String availability = mqttAvailabilityTopic();
  bool connected;

  if (configMqttUser.length() > 0) {
    connected = mqttClient.connect(
      clientId.c_str(),
      configMqttUser.c_str(),
      configMqttPassword.c_str(),
      availability.c_str(),
      1,
      true,
      "offline"
    );
  } else {
    connected = mqttClient.connect(
      clientId.c_str(),
      availability.c_str(),
      1,
      true,
      "offline"
    );
  }

  if (connected) {
    Serial.println("MQTT connectat correctament.");

    mqttClient.publish(availability.c_str(), "online", true);
    subscribeMqttCommands();
    publishMqttConfigState();

    if (configHaDiscoveryEnabled && !appState.mqttDiscoveryPublished) {
      publishHomeAssistantDiscovery();
    }
  } else {
    Serial.print("ERROR: No s'ha pogut connectar a MQTT. Estat: ");
    Serial.println(mqttClient.state());
    appState.mqttFailCount++;
  }
}

void checkMqttConnection() {
  if (!configMqttEnabled) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (!mqttClient.connected()) {
    Serial.println();
    Serial.println("AVIS: MQTT desconnectat. Reintentant...");
    connectMqtt();
  }
}

void publishMqttTelemetry() {
  if (!configMqttEnabled || !mqttClient.connected()) {
    return;
  }

  if (!isnan(appState.lastValidTemperatureC)) {
    String tempPayload = formatTemperatureForJson(
      appState.lastValidTemperatureC,
      configTemperatureDecimals
    );

    if (mqttPublishRetained(mqttTopic("temperature"), tempPayload)) {
      Serial.print("MQTT temperatura publicada: ");
      Serial.println(tempPayload);
    }
  }

  if (!isnan(appState.lastInternalTemperatureC)) {
    mqttPublishRetained(
      mqttTopic("internal_temperature"),
      formatTemperatureForJson(appState.lastInternalTemperatureC, 2)
    );
  }
  if (!isnan(appState.lastInternalHumidityPercent)) {
    mqttPublishRetained(
      mqttTopic("internal_humidity"),
      formatTemperatureForJson(appState.lastInternalHumidityPercent, 1)
    );
  }
  mqttPublishRetained(mqttTopic("internal_env_status"), appState.internalEnvStatus);
  bool internalTempAlarm = configInternalEnvAlarmEnabled && !isnan(appState.lastInternalTemperatureC) && appState.lastInternalTemperatureC >= configInternalTempAlarmC;
  bool internalHumidityAlarm = configInternalEnvAlarmEnabled && !isnan(appState.lastInternalHumidityPercent) && appState.lastInternalHumidityPercent >= configInternalHumidityAlarmPercent;
  mqttPublishRetained(mqttTopic("internal_temperature_alarm"), internalTempAlarm ? "ON" : "OFF");
  mqttPublishRetained(mqttTopic("internal_humidity_alarm"), internalHumidityAlarm ? "ON" : "OFF");

  mqttPublishRetained(mqttTopic("rssi"), String(WiFi.RSSI()));
  mqttPublishRetained(mqttTopic("uptime"), String(getUptimeSeconds()));
  mqttPublishRetained(mqttTopic("ip"), WiFi.localIP().toString());
  mqttPublishRetained(mqttTopic("total_reads"), String(appState.totalReads));
  mqttPublishRetained(mqttTopic("valid_reads"), String(appState.validReads));
  mqttPublishRetained(mqttTopic("failed_reads"), String(appState.failedReads));
  mqttPublishRetained(mqttTopic("sensor_status"), appState.sensorStatus);
  mqttPublishRetained(mqttTopic("consecutive_sensor_errors"), String(appState.consecutiveSensorErrors));
  mqttPublishRetained(mqttTopic("last_error"), appState.lastErrorMessage);

  String telemetry = "{";

  telemetry += "\"temperature_c\":";
  telemetry += formatTemperatureForJson(appState.lastValidTemperatureC, configTemperatureDecimals);
  telemetry += ",";

  telemetry += "\"internal_temperature_c\":";
  telemetry += formatTemperatureForJson(appState.lastInternalTemperatureC, 2);
  telemetry += ",";

  telemetry += "\"internal_humidity_percent\":";
  telemetry += formatTemperatureForJson(appState.lastInternalHumidityPercent, 1);
  telemetry += ",";

  telemetry += "\"internal_env_status\":\"";
  telemetry += jsonEscape(appState.internalEnvStatus);
  telemetry += "\",";

  telemetry += "\"internal_env_alarms_enabled\":";
  telemetry += configInternalEnvAlarmEnabled ? "true" : "false";
  telemetry += ",";
  telemetry += "\"internal_temperature_alarm\":";
  telemetry += internalTempAlarm ? "true" : "false";
  telemetry += ",";
  telemetry += "\"internal_humidity_alarm\":";
  telemetry += internalHumidityAlarm ? "true" : "false";
  telemetry += ",";
  telemetry += "\"internal_temperature_alarm_threshold_c\":";
  telemetry += floatPayload(configInternalTempAlarmC, 1);
  telemetry += ",";
  telemetry += "\"internal_humidity_alarm_threshold_percent\":";
  telemetry += floatPayload(configInternalHumidityAlarmPercent, 1);
  telemetry += ",";

  telemetry += "\"total_reads\":";
  telemetry += String(appState.totalReads);
  telemetry += ",";

  telemetry += "\"valid_reads\":";
  telemetry += String(appState.validReads);
  telemetry += ",";

  telemetry += "\"failed_reads\":";
  telemetry += String(appState.failedReads);
  telemetry += ",";

  telemetry += "\"sensor_status\":\"";
  telemetry += jsonEscape(appState.sensorStatus);
  telemetry += "\",";

  telemetry += "\"consecutive_sensor_errors\":";
  telemetry += String(appState.consecutiveSensorErrors);
  telemetry += ",";

  telemetry += "\"temperature_offset_c\":";
  telemetry += floatPayload(configTemperatureOffsetC, 2);
  telemetry += ",";

  telemetry += "\"rssi_dbm\":";
  telemetry += String(WiFi.RSSI());
  telemetry += ",";

  telemetry += "\"uptime_seconds\":";
  telemetry += String(getUptimeSeconds());
  telemetry += ",";

  telemetry += "\"ip\":\"";
  telemetry += jsonEscape(WiFi.localIP().toString());
  telemetry += "\",";

  telemetry += "\"read_interval_seconds\":";
  telemetry += String(configReadIntervalSeconds);
  telemetry += ",";

  telemetry += "\"mqtt_publish_interval_seconds\":";
  telemetry += String(configMqttPublishIntervalSeconds);
  telemetry += ",";

  telemetry += "\"temperature_decimals\":";
  telemetry += String(configTemperatureDecimals);

  telemetry += "}";

  mqttPublishRetained(mqttTopic("telemetry"), telemetry);
  publishMqttConfigState();

  appState.mqttPublishCount++;
}

void publishOfflineAndDisconnect() {
  if (mqttClient.connected()) {
    String availability = mqttAvailabilityTopic();
    mqttClient.publish(availability.c_str(), "offline", true);
    mqttClient.disconnect();
  }
}

void reconfigureMqtt() {
  publishOfflineAndDisconnect();
  delay(100);

  appState.mqttDiscoveryPublished = false;
  appState.mqttRestartRequested = false;
  appState.mqttDiscoveryRequested = false;
  appState.mqttConfigStatePublishRequested = false;
  appState.mqttReconfigureRequested = false;

  mqttClient.setServer(configMqttHost.c_str(), configMqttPort);
  mqttClient.setCallback(handleMqttMessage);

  if (configMqttEnabled) {
    connectMqtt();
  }
}

void initMqtt() {
  mqttClient.setServer(configMqttHost.c_str(), configMqttPort);
  mqttClient.setCallback(handleMqttMessage);
  mqttClient.setBufferSize(4096);

  if (configMqttEnabled) {
    connectMqtt();
  } else {
    Serial.println();
    Serial.println("MQTT desactivat per configuracio.");
  }
}

void mqttLoop() {
  if (configMqttEnabled) {
    mqttClient.loop();
  }

  if (appState.mqttConfigStatePublishRequested) {
    appState.mqttConfigStatePublishRequested = false;
    publishMqttConfigState();
  }

  if (appState.mqttDiscoveryRequested) {
    appState.mqttDiscoveryRequested = false;
    appState.mqttDiscoveryPublished = false;
    publishHomeAssistantDiscovery();
  }

  if (appState.mqttReconfigureRequested) {
    appState.mqttReconfigureRequested = false;
    Serial.println("Reconfigurant MQTT per comanda rebuda...");
    reconfigureMqtt();
  }

  if (appState.mqttRestartRequested) {
    appState.mqttRestartRequested = false;

    Serial.println("Reiniciant boia per comanda MQTT...");
    delay(500);
    publishOfflineAndDisconnect();
    delay(500);
    ESP.restart();
  }
}
