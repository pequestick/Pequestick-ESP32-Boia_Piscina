#include "Utils.h"

uint32_t getUptimeSeconds() {
  return millis() / 1000UL;
}

String formatTemperature(float value, uint8_t decimals) {
  if (isnan(value)) {
    return "Sense dades";
  }

  unsigned int decimalPlaces = decimals;
  return String(value, decimalPlaces);
}

String formatTemperatureForJson(float value, uint8_t decimals) {
  if (isnan(value)) {
    return "null";
  }

  unsigned int decimalPlaces = decimals;
  return String(value, decimalPlaces);
}

String htmlEscape(const String& input) {
  String output = input;
  output.replace("&", "&amp;");
  output.replace("<", "&lt;");
  output.replace(">", "&gt;");
  output.replace("\"", "&quot;");
  output.replace("'", "&#39;");
  return output;
}

String jsonEscape(const String& input) {
  String output = input;
  output.replace("\\", "\\\\");
  output.replace("\"", "\\\"");
  output.replace("\n", "\\n");
  output.replace("\r", "\\r");
  return output;
}

uint16_t clampUint16(uint16_t value, uint16_t minValue, uint16_t maxValue) {
  if (value < minValue) {
    return minValue;
  }

  if (value > maxValue) {
    return maxValue;
  }

  return value;
}

uint8_t clampUint8(uint8_t value, uint8_t minValue, uint8_t maxValue) {
  if (value < minValue) {
    return minValue;
  }

  if (value > maxValue) {
    return maxValue;
  }

  return value;
}