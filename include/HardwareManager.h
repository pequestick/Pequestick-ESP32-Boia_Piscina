#pragma once

#include <Arduino.h>

void initHardwareManager();
void handleHardwareManager();

String hardwareButtonStatusText();
String hardwareLedStatusText();
String hardwareBoardLedStatusText();
String hardwareLastActionText();
String hardwareResetButtonPinText();
String hardwareStatusLedPinText();
String hardwareReadyStateText();
