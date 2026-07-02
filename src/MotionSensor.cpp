#include "MotionSensor.h"

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#include "AppConfig.h"
#include "AppState.h"

namespace {
constexpr uint8_t MPU6050_REG_WHO_AM_I = 0x75;
constexpr uint8_t MPU6050_REG_PWR_MGMT_1 = 0x6B;
constexpr uint8_t MPU6050_REG_GYRO_CONFIG = 0x1B;
constexpr uint8_t MPU6050_REG_ACCEL_CONFIG = 0x1C;
constexpr uint8_t MPU6050_REG_ACCEL_XOUT_H = 0x3B;

bool i2cAddressResponds(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool writeRegister(uint8_t address, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readRegister(uint8_t address, uint8_t reg, uint8_t& value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(address, (uint8_t)1) != 1) {
    while (Wire.available()) Wire.read();
    return false;
  }
  value = Wire.read();
  return true;
}

bool readBlock(uint8_t address, uint8_t reg, uint8_t* data, uint8_t length) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(address, length) != length) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (uint8_t i = 0; i < length; ++i) {
    data[i] = Wire.read();
  }
  return true;
}

int16_t readInt16(const uint8_t* data, uint8_t offset) {
  return (int16_t)(((uint16_t)data[offset] << 8) | data[offset + 1]);
}

void setMotionError(const String& message) {
  appState.motionFailedReads++;
  appState.motionConsecutiveErrors++;
  appState.motionStatus = appState.motionConsecutiveErrors >= 3 ? "ERROR" : "WARN";
  appState.motionLastError = message;
  Serial.print("ERROR MPU6050: ");
  Serial.println(message);
}

uint8_t detectMotionAddress() {
  if (i2cAddressResponds(MOTION_MPU6050_I2C_ADDRESS)) return MOTION_MPU6050_I2C_ADDRESS;
  if (i2cAddressResponds(MOTION_MPU6050_ALT_I2C_ADDRESS)) return MOTION_MPU6050_ALT_I2C_ADDRESS;
  return 0;
}
}

void initMotionSensor() {
  appState.motionI2cAddress = detectMotionAddress();
  if (appState.motionI2cAddress == 0) {
    appState.motionStatus = "WARN";
    appState.motionLastError = "MPU6050 no detectat a 0x68 ni 0x69";
    Serial.println("- MPU6050/GY-521: no detectat al bus I2C intern");
    return;
  }

  uint8_t whoAmI = 0;
  bool whoOk = readRegister(appState.motionI2cAddress, MPU6050_REG_WHO_AM_I, whoAmI);
  bool initOk = writeRegister(appState.motionI2cAddress, MPU6050_REG_PWR_MGMT_1, 0x00);
  delay(10);
  initOk &= writeRegister(appState.motionI2cAddress, MPU6050_REG_ACCEL_CONFIG, 0x00);
  initOk &= writeRegister(appState.motionI2cAddress, MPU6050_REG_GYRO_CONFIG, 0x00);

  appState.motionStatus = initOk ? "READY" : "WARN";
  appState.motionLastError = initOk ? "Cap error" : "Detectat pero no configurat";

  Serial.print("- MPU6050/GY-521: ");
  Serial.print(initOk ? "detectat" : "detectat amb error");
  Serial.print(" · adreca 0x");
  Serial.print(appState.motionI2cAddress, HEX);
  Serial.print(" · WHO_AM_I ");
  Serial.println(whoOk ? String("0x") + String(whoAmI, HEX) : "sense resposta");
}

void performMotionRead() {
  appState.motionTotalReads++;
  appState.motionLastReadMillis = millis();

  if (appState.motionI2cAddress == 0) {
    appState.motionI2cAddress = detectMotionAddress();
    if (appState.motionI2cAddress == 0) {
      setMotionError("MPU6050 no detectat");
      return;
    }
  }

  uint8_t data[14];
  if (!readBlock(appState.motionI2cAddress, MPU6050_REG_ACCEL_XOUT_H, data, sizeof(data))) {
    setMotionError("Resposta incompleta");
    return;
  }

  int16_t rawAx = readInt16(data, 0);
  int16_t rawAy = readInt16(data, 2);
  int16_t rawAz = readInt16(data, 4);
  int16_t rawGx = readInt16(data, 8);
  int16_t rawGy = readInt16(data, 10);
  int16_t rawGz = readInt16(data, 12);

  float ax = (float)rawAx / 16384.0f;
  float ay = (float)rawAy / 16384.0f;
  float az = (float)rawAz / 16384.0f;
  float gx = (float)rawGx / 131.0f;
  float gy = (float)rawGy / 131.0f;
  float gz = (float)rawGz / 131.0f;
  float accelMagnitude = sqrtf(ax * ax + ay * ay + az * az);
  float pitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * 180.0f / PI;
  float roll = atan2f(ay, az) * 180.0f / PI;
  float tilt = max(fabsf(pitch), fabsf(roll));

  if (!isfinite(accelMagnitude) || !isfinite(pitch) || !isfinite(roll)) {
    setMotionError("Valors fora de rang");
    return;
  }

  appState.motionValidReads++;
  appState.motionConsecutiveErrors = 0;
  appState.lastMotionAccelXG = ax;
  appState.lastMotionAccelYG = ay;
  appState.lastMotionAccelZG = az;
  appState.lastMotionGyroXDps = gx;
  appState.lastMotionGyroYDps = gy;
  appState.lastMotionGyroZDps = gz;
  appState.lastMotionAccelMagnitudeG = accelMagnitude;
  appState.lastMotionPitchDeg = pitch;
  appState.lastMotionRollDeg = roll;
  appState.lastMotionTiltDeg = tilt;
  appState.motionMoving = fabsf(accelMagnitude - 1.0f) >= MOTION_ACCEL_DELTA_ALARM_G;
  appState.motionTiltAlarm = tilt >= MOTION_TILT_ALARM_DEGREES;
  appState.motionStatus = appState.motionTiltAlarm || appState.motionMoving ? "ALARM" : "OK";
  appState.motionLastError = "Cap error";

  Serial.print("MPU6050 moviment: pitch ");
  Serial.print(pitch, 1);
  Serial.print(" deg · roll ");
  Serial.print(roll, 1);
  Serial.print(" deg · accel ");
  Serial.print(accelMagnitude, 2);
  Serial.println(" g");
}
