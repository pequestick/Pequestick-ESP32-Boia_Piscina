#include "InternalEnvSensor.h"

#include <Wire.h>

#include "AppConfig.h"
#include "AppState.h"

namespace {
constexpr uint8_t SHT41_MEASURE_HIGH_PRECISION = 0xFD;

uint8_t sht41Crc(const uint8_t* data, size_t length) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

void setInternalEnvError(const String& message) {
  appState.internalEnvFailedReads++;
  appState.internalEnvConsecutiveErrors++;
  appState.internalEnvStatus = appState.internalEnvConsecutiveErrors >= 3 ? "ERROR" : "WARN";
  appState.internalEnvLastError = message;
  Serial.print("ERROR SHT41: ");
  Serial.println(message);
}
}

void initInternalEnvSensor() {
  Wire.begin(INTERNAL_ENV_I2C_SDA_PIN, INTERNAL_ENV_I2C_SCL_PIN);
  Wire.setClock(100000);

  Wire.beginTransmission(INTERNAL_ENV_I2C_ADDRESS);
  bool detected = Wire.endTransmission() == 0;
  appState.internalEnvStatus = detected ? "READY" : "WARN";
  appState.internalEnvLastError = detected ? "Cap error" : "SHT41 no detectat a l'adreca 0x44";

  Serial.print("- SHT41 intern: ");
  Serial.print(detected ? "detectat" : "no detectat");
  Serial.print(" · SDA GPIO");
  Serial.print(INTERNAL_ENV_I2C_SDA_PIN);
  Serial.print(" · SCL GPIO");
  Serial.println(INTERNAL_ENV_I2C_SCL_PIN);
}

void performInternalEnvRead() {
  appState.internalEnvTotalReads++;
  appState.internalEnvLastReadMillis = millis();

  Wire.beginTransmission(INTERNAL_ENV_I2C_ADDRESS);
  Wire.write(SHT41_MEASURE_HIGH_PRECISION);
  if (Wire.endTransmission() != 0) {
    setInternalEnvError("Sense resposta I2C a 0x44");
    return;
  }

  delay(10);
  if (Wire.requestFrom((uint8_t)INTERNAL_ENV_I2C_ADDRESS, (uint8_t)6) != 6) {
    setInternalEnvError("Resposta incompleta");
    while (Wire.available()) Wire.read();
    return;
  }

  uint8_t data[6];
  for (uint8_t i = 0; i < sizeof(data); ++i) data[i] = Wire.read();
  if (sht41Crc(data, 2) != data[2] || sht41Crc(data + 3, 2) != data[5]) {
    setInternalEnvError("CRC de lectura incorrecte");
    return;
  }

  uint16_t rawTemperature = ((uint16_t)data[0] << 8) | data[1];
  uint16_t rawHumidity = ((uint16_t)data[3] << 8) | data[4];
  float temperatureC = -45.0f + 175.0f * ((float)rawTemperature / 65535.0f);
  float humidityPercent = -6.0f + 125.0f * ((float)rawHumidity / 65535.0f);
  humidityPercent = constrain(humidityPercent, 0.0f, 100.0f);

  if (!isfinite(temperatureC) || !isfinite(humidityPercent) ||
      temperatureC < -40.0f || temperatureC > 125.0f) {
    setInternalEnvError("Valors fora de rang");
    return;
  }

  appState.internalEnvValidReads++;
  appState.internalEnvConsecutiveErrors = 0;
  appState.internalEnvStatus = "OK";
  appState.internalEnvLastError = "Cap error";
  appState.lastInternalTemperatureC = temperatureC;
  appState.lastInternalHumidityPercent = humidityPercent;

  Serial.print("SHT41 interior: ");
  Serial.print(temperatureC, 2);
  Serial.print(" C · ");
  Serial.print(humidityPercent, 1);
  Serial.println(" % RH");
}
