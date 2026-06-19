#include "TemperatureSensor.h"

#include <OneWireNg_CurrentPlatform.h>
#include <drivers/DSTherm.h>
#include <utils/Placeholder.h>

#include "AppConfig.h"
#include "AppState.h"

// ==========================
// OBJECTES SONDA
// ==========================

static OneWireNg_CurrentPlatform oneWire(ONE_WIRE_PIN, false);
static DSTherm sensor(oneWire);

void initTemperatureSensor() {
  sensor.writeScratchpadAll(0, 0, DSTherm::RES_12_BIT);
}

bool readTemperature(float &temperatureC, String &errorMessage) {
  sensor.convertTempAll(DSTherm::MAX_CONV_TIME, false);

  Placeholder<DSTherm::Scratchpad> scratchpad;

  OneWireNg::ErrorCode ec = sensor.readScratchpadSingle(scratchpad);

  if (ec != OneWireNg::EC_SUCCESS) {
    errorMessage = "No s'ha pogut llegir la sonda. Codi OneWireNg: " + String(ec);
    return false;
  }

  float rawTemperatureC = scratchpad->getTemp() / 1000.0f;
  appState.lastRawTemperatureC = rawTemperatureC;
  temperatureC = rawTemperatureC + configTemperatureOffsetC;

  if (rawTemperatureC == -127.0f) {
    errorMessage = "Sonda desconnectada o bus OneWire sense resposta (-127 C)";
    return false;
  }

  if (rawTemperatureC == 85.0f) {
    errorMessage = "Lectura DS18B20 85 C descartada: valor tipic d'arrencada o conversio no valida";
    return false;
  }

  if (isnan(temperatureC)) {
    errorMessage = "Temperatura NaN";
    return false;
  }

  if (temperatureC < configMinValidTempC || temperatureC > configMaxValidTempC) {
    unsigned int decimalPlaces = configTemperatureDecimals;
    errorMessage = "Temperatura fora de rang logic: " + String(temperatureC, decimalPlaces) + " C";
    return false;
  }

  errorMessage = "Cap error";
  return true;
}

void performTemperatureRead() {
  float tempC = 0.0f;
  String errorMessage = "";

  appState.totalReads++;

  Serial.println();
  Serial.println("Demanant temperatura...");

  bool ok = readTemperature(tempC, errorMessage);

  appState.lastErrorMessage = errorMessage;

  if (ok) {
    appState.validReads++;
    appState.consecutiveSensorErrors = 0;
    appState.sensorStatus = "OK";
    appState.lastValidTemperatureC = tempC;

    Serial.print("Temperatura piscina: ");
    Serial.print(tempC, configTemperatureDecimals);
    Serial.println(" ºC");

    if (configTemperatureOffsetC != 0.0f) {
      Serial.print("Offset aplicat: ");
      Serial.print(configTemperatureOffsetC, 2);
      Serial.println(" ºC");
    }
  } else {
    appState.failedReads++;
    appState.consecutiveSensorErrors++;
    appState.sensorStatus = appState.consecutiveSensorErrors >= 3 ? "ERROR" : "WARN";

    Serial.print("ERROR: ");
    Serial.println(errorMessage);

    Serial.println("Lectura descartada.");
  }

  Serial.println();
  Serial.println("--- Estat lectures ---");

  Serial.print("Lectures totals: ");
  Serial.println(appState.totalReads);

  Serial.print("Lectures valides: ");
  Serial.println(appState.validReads);

  Serial.print("Lectures fallides: ");
  Serial.println(appState.failedReads);

  Serial.print("Errors consecutius sonda: ");
  Serial.println(appState.consecutiveSensorErrors);

  Serial.print("Estat sonda: ");
  Serial.println(appState.sensorStatus);

  if (!isnan(appState.lastValidTemperatureC)) {
    Serial.print("Ultima temperatura valida: ");
    Serial.print(appState.lastValidTemperatureC, configTemperatureDecimals);
    Serial.println(" ºC");
  } else {
    Serial.println("Ultima temperatura valida: encara cap");
  }

  Serial.print("MQTT publicacions: ");
  Serial.println(appState.mqttPublishCount);

  Serial.print("MQTT errors: ");
  Serial.println(appState.mqttFailCount);

  Serial.println("----------------------");
}
