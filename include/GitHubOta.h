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
  String message;
  String version;
  String buildSha;
  String buildDate;
  String firmwareUrl;
  String notes;
  uint32_t sizeBytes = 0;
};

InternetCheckInfo checkInternetConnectivityNow();
GitHubUpdateInfo checkGitHubUpdateNow();
bool performGitHubOtaUpdate(String& messageOut);
String currentFirmwareBuildSha();
String shortBuildSha(const String& sha);
