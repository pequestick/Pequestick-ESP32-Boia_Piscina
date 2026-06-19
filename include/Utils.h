#pragma once

#include <Arduino.h>

uint32_t getUptimeSeconds();

String formatTemperature(float value, uint8_t decimals);
String formatTemperatureForJson(float value, uint8_t decimals);

String htmlEscape(const String& input);
String jsonEscape(const String& input);

uint16_t clampUint16(uint16_t value, uint16_t minValue, uint16_t maxValue);
uint8_t clampUint8(uint8_t value, uint8_t minValue, uint8_t maxValue);