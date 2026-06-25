#pragma once

#include <Arduino.h>

void initSdManager();
void handleSdManager();

bool isSdEnabled();
bool isSdMounted();
bool refreshSdInfo();
bool appendSdHistoryRecord();
bool logicalFormatSdCard();
bool ensureSdHistoryFile();

String sdStatusText();
String sdCardTypeText();
String sdTotalText();
String sdUsedText();
String sdFreeText();
String sdUsedPercentText();
String sdHistoryPathText();
String sdLastErrorText();
String sdInfoJson();
