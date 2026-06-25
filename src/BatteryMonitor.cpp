#include "BatteryMonitor.h"

#include <Arduino.h>

#include "AppConfig.h"
#include "AppState.h"
#include "Utils.h"

bool isBatteryMonitorEnabled() {
  return BATTERY_VOLTAGE_ADC_PIN >= 0;
}

static float clampPercent(float value) {
  if (isnan(value)) return NAN;
  if (value < 0.0f) return 0.0f;
  if (value > 100.0f) return 100.0f;
  return value;
}

static float estimateBatteryPercent(float voltage) {
  if (isnan(voltage)) return NAN;
  float range = configBatteryFullVoltage - configBatteryEmptyVoltage;
  if (range <= 0.01f) return NAN;
  return clampPercent(((voltage - configBatteryEmptyVoltage) / range) * 100.0f);
}

void initBatteryMonitor() {
  appState.batteryStatus = isBatteryMonitorEnabled() ? "UNKNOWN" : "DISABLED";
  appState.batteryLastError = isBatteryMonitorEnabled() ? "Encara no s'ha fet cap lectura" : "Monitor de bateria desactivat";

  if (!isBatteryMonitorEnabled()) {
    Serial.println("Monitor bateria: desactivat");
    return;
  }

  pinMode(BATTERY_VOLTAGE_ADC_PIN, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(BATTERY_VOLTAGE_ADC_PIN, ADC_11db);

  Serial.print("Monitor bateria: ADC GPIO");
  Serial.print(BATTERY_VOLTAGE_ADC_PIN);
  Serial.print(" · divisor x");
  Serial.println(BATTERY_DIVIDER_RATIO, 2);
}

void performBatteryRead() {
  if (!isBatteryMonitorEnabled()) {
    return;
  }

  appState.batteryTotalReads++;
  appState.batteryLastReadMillis = millis();

  uint32_t rawSum = 0;
  uint32_t mvSum = 0;

  for (uint8_t i = 0; i < BATTERY_ADC_SAMPLES; i++) {
    rawSum += analogRead(BATTERY_VOLTAGE_ADC_PIN);
    mvSum += analogReadMilliVolts(BATTERY_VOLTAGE_ADC_PIN);
    delay(2);
  }

  float raw = (float)rawSum / (float)BATTERY_ADC_SAMPLES;
  float adcMilliVolts = (float)mvSum / (float)BATTERY_ADC_SAMPLES;
  float voltage = (adcMilliVolts / 1000.0f) * BATTERY_DIVIDER_RATIO * configBatteryCalibrationFactor;
  float percent = estimateBatteryPercent(voltage);

  appState.lastBatteryRawAdc = raw;
  appState.lastBatteryAdcMilliVolts = adcMilliVolts;
  appState.lastBatteryVoltage = voltage;
  appState.lastBatteryPercent = percent;

  if (adcMilliVolts <= 50.0f || isnan(voltage) || voltage <= 0.1f || isnan(percent)) {
    appState.batteryFailedReads++;
    appState.batteryStatus = "ERROR";
    appState.batteryLastError = "Lectura ADC bateria no valida";
    Serial.println("ERROR bateria: lectura ADC no valida");
    return;
  }

  appState.batteryValidReads++;
  appState.batteryLastError = "OK";
  appState.batteryStatus = percent <= configBatteryLowPercent ? "LOW" : "OK";

  Serial.print("Bateria: ");
  Serial.print(voltage, 3);
  Serial.print(" V · ");
  Serial.print(percent, 0);
  Serial.print(" % · ADC ");
  Serial.print(adcMilliVolts, 0);
  Serial.println(" mV");
}

String batteryVoltageText() {
  if (!isBatteryMonitorEnabled()) return "Desactivada";
  if (isnan(appState.lastBatteryVoltage)) return "Sense dades";
  return String(appState.lastBatteryVoltage, 3) + " V";
}

String batteryPercentText() {
  if (!isBatteryMonitorEnabled()) return "Desactivada";
  if (isnan(appState.lastBatteryPercent)) return "Sense dades";
  return String(appState.lastBatteryPercent, 0) + " %";
}

String batteryStatusText() {
  if (!isBatteryMonitorEnabled()) return "DISABLED";
  return appState.batteryStatus;
}
