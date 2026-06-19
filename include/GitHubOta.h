#pragma once

#include <Arduino.h>

struct InternetCheckInfo {
  bool ok = false;
  String message;
  String details;
  int httpCode = 0;
  String resolvedIp;
};

struct GitHubUpdateInfo {
  bool ok = false;
  bool updateAvailable = false;
  bool remoteOlder = false;
  bool sameVersion = false;
  int httpCode = 0;
  String message;
  String details;
  String version;
  String buildSha;
  String buildDate;
  String firmwareUrl;
  String notes;
  uint32_t sizeBytes = 0;
};

InternetCheckInfo checkInternetConnectivityNow();
GitHubUpdateInfo checkGitHubUpdateNow(bool verboseLog = false);
typedef void (*OtaProgressCallback)();
bool performGitHubOtaUpdate(String& messageOut, OtaProgressCallback progressCallback = nullptr);
void otaClearLog(const String& firstLine = "");
void otaAppendLog(const String& line);
String currentFirmwareBuildSha();
String shortBuildSha(const String& sha);
