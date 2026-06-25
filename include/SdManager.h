#pragma once

#include <Arduino.h>

using SdMqttPublishCallback = bool (*)(const String& topic, const String& payload);

void initSdManager();
void handleSdManager();

bool isSdEnabled();
bool isSdMounted();
bool refreshSdInfo();
bool appendSdHistoryRecord();
bool logicalFormatSdCard();
bool ensureSdHistoryFile();
bool ensureSdBaseStructure();

bool appendSdSystemLog(const String& level, const String& message);
bool writeSdConfigSnapshot();
bool writeSdBootBlackbox();
bool writeSdVersionFile();

bool appendSdMqttPending(const String& payload);
bool flushSdMqttPending(const String& topic, SdMqttPublishCallback publishCallback);
bool hasSdMqttPending();

bool normalizeSdPath(const String& input, String& output);
bool sdPathExists(const String& path);
bool sdPathIsDirectory(const String& path);
String sdReadTextFileLimited(const String& path, size_t maxBytes, bool& truncated);
String sdDirectoryListingJson(const String& path);
String sdDirectoryListingHtml(const String& path);

String sdStatusText();
String sdCardTypeText();
String sdTotalText();
String sdUsedText();
String sdFreeText();
String sdUsedPercentText();
String sdHistoryPathText();
String sdDailyStatsPathText();
String sdSystemLogPathText();
String sdPendingMqttPathText();
String sdLastErrorText();
String sdInfoJson();
