#include "BatteryMonitor.h"

#include <Arduino.h>

#include "AppConfig.h"
#include "AppState.h"
#include "Utils.h"

static const unsigned long BATTERY_ESTIMATE_MIN_SECONDS = 10UL * 60UL;
static const float BATTERY_ESTIMATE_MIN_DROP_PERCENT = 0.5f;
static const float BATTERY_ESTIMATE_RESET_RISE_PERCENT = 2.0f;

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

static String durationText(uint32_t seconds) {
  if (seconds == 0) return "0 h";

  uint32_t months = seconds / (30UL * 86400UL);
  seconds %= (30UL * 86400UL);
  uint32_t days = seconds / 86400UL;
  seconds %= 86400UL;
  uint32_t hours = seconds / 3600UL;
  seconds %= 3600UL;
  uint32_t minutes = seconds / 60UL;

  String out = "";
  if (months > 0) {
    out += String(months);
    out += months == 1 ? " mes" : " mesos";
    if (days > 0) {
      out += " ";
      out += String(days);
      out += days == 1 ? " dia" : " dies";
    }
    return out;
  }

  if (days > 0) {
    out += String(days);
    out += days == 1 ? " dia" : " dies";
    if (hours > 0) {
      out += " ";
      out += String(hours);
      out += " h";
    }
    return out;
  }

  if (hours > 0) {
    out += String(hours);
    out += " h";
    if (minutes > 0) {
      out += " ";
      out += String(minutes);
      out += " min";
    }
    return out;
  }

  out += String(minutes);
  out += " min";
  return out;
}

static void resetBatteryEstimate(float percent) {
  appState.batteryEstimateStartMillis = millis();
  appState.batteryEstimateStartPercent = percent;
  appState.batteryDischargePercentPerHour = NAN;
  appState.batteryEstimatedRemainingSeconds = 0;
  appState.batteryEstimateReady = false;
  appState.batteryEstimateStatus = "Calibrant descàrrega";
}

static void updateBatteryEstimate(float percent) {
  if (isnan(percent)) {
    appState.batteryEstimateReady = false;
    appState.batteryEstimateStatus = "Sense percentatge valid";
    return;
  }

  if (appState.batteryEstimateStartMillis == 0 || isnan(appState.batteryEstimateStartPercent)) {
    resetBatteryEstimate(percent);
    return;
  }

  // Si la bateria puja clarament, probablement hi ha carrega, soroll ADC o canvi de calibratge.
  // Reiniciem la finestra per no donar una autonomia absurda.
  if (percent > appState.batteryEstimateStartPercent + BATTERY_ESTIMATE_RESET_RISE_PERCENT) {
    resetBatteryEstimate(percent);
    appState.batteryEstimateStatus = "Reiniciat: bateria ha pujat";
    return;
  }

  unsigned long elapsedSeconds = (millis() - appState.batteryEstimateStartMillis) / 1000UL;
  float dropPercent = appState.batteryEstimateStartPercent - percent;

  if (elapsedSeconds < BATTERY_ESTIMATE_MIN_SECONDS) {
    appState.batteryEstimateReady = false;
    appState.batteryDischargePercentPerHour = NAN;
    appState.batteryEstimatedRemainingSeconds = 0;
    uint32_t missingSeconds = (uint32_t)(BATTERY_ESTIMATE_MIN_SECONDS - elapsedSeconds);
    appState.batteryEstimateStatus = "Calibrant: falten " + durationText(missingSeconds);
    return;
  }

  if (dropPercent < BATTERY_ESTIMATE_MIN_DROP_PERCENT) {
    appState.batteryEstimateReady = false;
    appState.batteryDischargePercentPerHour = NAN;
    appState.batteryEstimatedRemainingSeconds = 0;
    appState.batteryEstimateStatus = "Encara no hi ha baixada suficient";
    return;
  }

  float dischargePercentPerSecond = dropPercent / (float)elapsedSeconds;
  if (dischargePercentPerSecond <= 0.0f || isnan(dischargePercentPerSecond)) {
    appState.batteryEstimateReady = false;
    appState.batteryDischargePercentPerHour = NAN;
    appState.batteryEstimatedRemainingSeconds = 0;
    appState.batteryEstimateStatus = "Pendent de descàrrega estable";
    return;
  }

  float remainingSeconds = percent / dischargePercentPerSecond;
  if (remainingSeconds < 0.0f || isnan(remainingSeconds)) {
    appState.batteryEstimateReady = false;
    appState.batteryEstimatedRemainingSeconds = 0;
    appState.batteryEstimateStatus = "Estimacio no valida";
    return;
  }

  if (remainingSeconds > 4294967295.0f) {
    remainingSeconds = 4294967295.0f;
  }

  appState.batteryEstimateReady = true;
  appState.batteryDischargePercentPerHour = dischargePercentPerSecond * 3600.0f;
  appState.batteryEstimatedRemainingSeconds = (uint32_t)remainingSeconds;
  appState.batteryEstimateStatus = "Estimacio activa";
}

void initBatteryMonitor() {
  appState.batteryStatus = isBatteryMonitorEnabled() ? "UNKNOWN" : "DISABLED";
  appState.batteryLastError = isBatteryMonitorEnabled() ? "Encara no s'ha fet cap lectura" : "Monitor de bateria desactivat";
  appState.batteryEstimateStatus = isBatteryMonitorEnabled() ? "Esperant primera lectura" : "Monitor de bateria desactivat";

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
    appState.batteryEstimateReady = false;
    appState.batteryEstimateStatus = "Sense lectura ADC valida";
    Serial.println("ERROR bateria: lectura ADC no valida");
    return;
  }

  appState.batteryValidReads++;
  appState.batteryLastError = "OK";
  appState.batteryStatus = percent <= configBatteryLowPercent ? "LOW" : "OK";

  updateBatteryEstimate(percent);

  Serial.print("Bateria: ");
  Serial.print(voltage, 3);
  Serial.print(" V · ");
  Serial.print(percent, 0);
  Serial.print(" % · ADC ");
  Serial.print(adcMilliVolts, 0);
  Serial.print(" mV · autonomia ");
  Serial.println(batteryRemainingTimeText());
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

String batteryRemainingTimeText() {
  if (!isBatteryMonitorEnabled()) return "Desactivada";
  if (!appState.batteryEstimateReady) return "Calculant";
  return durationText(appState.batteryEstimatedRemainingSeconds);
}

String batteryRemainingDetailText() {
  if (!isBatteryMonitorEnabled()) return "Monitor de bateria desactivat";
  if (!appState.batteryEstimateReady) return appState.batteryEstimateStatus;

  unsigned long elapsedSeconds = appState.batteryEstimateStartMillis == 0 ? 0 : (millis() - appState.batteryEstimateStartMillis) / 1000UL;
  float dropPercent = appState.batteryEstimateStartPercent - appState.lastBatteryPercent;

  String detail = "Basat en ";
  detail += durationText((uint32_t)elapsedSeconds);
  detail += " de mostra";
  if (!isnan(dropPercent)) {
    detail += " i ";
    detail += String(dropPercent, 1);
    detail += " % de baixada";
  }
  if (!isnan(appState.batteryDischargePercentPerHour)) {
    detail += " · ";
    detail += String(appState.batteryDischargePercentPerHour, 2);
    detail += " %/h";
  }
  detail += ". Orientatiu.";
  return detail;
}
