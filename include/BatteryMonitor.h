#pragma once

#include <Arduino.h>

void initBatteryMonitor();
void performBatteryRead();

String batteryVoltageText();
String batteryPercentText();
String batteryStatusText();
String batteryRemainingTimeText();
String batteryRemainingDetailText();
bool isBatteryMonitorEnabled();
