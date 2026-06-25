#pragma once

#include <Arduino.h>

void initBatteryMonitor();
void performBatteryRead();

String batteryVoltageText();
String batteryPercentText();
String batteryStatusText();
bool isBatteryMonitorEnabled();
