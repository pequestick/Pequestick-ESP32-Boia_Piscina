#pragma once

#include <Arduino.h>

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

GitHubUpdateInfo checkGitHubUpdateNow();
bool performGitHubOtaUpdate(String& messageOut);
String currentFirmwareBuildSha();
String shortBuildSha(const String& sha);
