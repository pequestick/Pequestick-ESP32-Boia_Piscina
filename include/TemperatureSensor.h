#pragma once

#include <Arduino.h>

void initTemperatureSensor();
bool readTemperature(float &temperatureC, String &errorMessage);
void performTemperatureRead();