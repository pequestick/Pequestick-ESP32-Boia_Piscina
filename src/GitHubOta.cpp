#include "GitHubOta.h"

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>

#include "AppConfig.h"
#include "AppState.h"

#ifndef FIRMWARE_BUILD_SHA
#define FIRMWARE_BUILD_SHA "local"
#endif

#ifndef FIRMWARE_BUILD_DATE
#define FIRMWARE_BUILD_DATE "local"
#endif

extern AppState appState;

static String jsonValue(const String& json, const String& key) {
  String needle = "\"" + key + "\"";
  int p = json.indexOf(needle);
  if (p < 0) return "";
  p = json.indexOf(':', p);
  if (p < 0) return "";
  p++;
  while (p < (int)json.length() && isspace((unsigned char)json[p])) p++;
  if (p >= (int)json.length()) return "";

  if (json[p] == '"') {
    p++;
    String out;
    bool esc = false;
    for (int i = p; i < (int)json.length(); i++) {
      char c = json[i];
      if (esc) {
        out += c;
        esc = false;
      } else if (c == '\\') {
        esc = true;
      } else if (c == '"') {
        break;
      } else {
        out += c;
      }
    }
    return out;
  }

  int e = p;
  while (e < (int)json.length() && json[e] != ',' && json[e] != '}') e++;
  String out = json.substring(p, e);
  out.trim();
  return out;
}

static bool httpGetString(const String& url, String& body, int& code) {
  body = "";
  code = 0;
  if (url.length() == 0) {
    code = -1;
    return false;
  }

  WiFiClientSecure secureClient;
  WiFiClient plainClient;
  HTTPClient http;

  bool https = url.startsWith("https://");
  if (https) {
    secureClient.setInsecure();
    if (!http.begin(secureClient, url)) {
      code = -2;
      return false;
    }
  } else {
    if (!http.begin(plainClient, url)) {
      code = -2;
      return false;
    }
  }

  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(15000);
  code = http.GET();
  if (code == HTTP_CODE_OK) {
    body = http.getString();
    http.end();
    return true;
  }

  http.end();
  return false;
}

String currentFirmwareBuildSha() {
  return String(FIRMWARE_BUILD_SHA);
}

String shortBuildSha(const String& sha) {
  if (sha.length() <= 12) return sha;
  return sha.substring(0, 12);
}


InternetCheckInfo checkInternetConnectivityNow() {
  InternetCheckInfo info;

  if (WiFi.status() != WL_CONNECTED) {
    info.message = "Wi-Fi no connectat";
    info.details = "La boia no pot provar Internet perquè no està connectada en mode STA.";
    return info;
  }

  IPAddress ip;
  if (WiFi.hostByName("raw.githubusercontent.com", ip)) {
    info.resolvedIp = ip.toString();
  } else {
    info.message = "DNS no resol raw.githubusercontent.com";
    info.details = "Hi ha Wi-Fi, però el DNS no pot resoldre GitHub raw. Revisa DNS/gateway de la xarxa IoT.";
    return info;
  }

  WiFiClient plainClient;
  HTTPClient http;
  String testUrl = "http://connectivitycheck.gstatic.com/generate_204";
  if (!http.begin(plainClient, testUrl)) {
    info.message = "No puc iniciar prova HTTP";
    info.details = "HTTPClient no ha pogut obrir la prova de connectivitat.";
    info.httpCode = -2;
    return info;
  }

  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(8000);
  int code = http.GET();
  http.end();

  info.httpCode = code;
  if (code == 204 || code == 200) {
    info.ok = true;
    info.message = "Internet accessible";
    info.details = "DNS GitHub OK (" + info.resolvedIp + ") · prova HTTP OK " + String(code);
    return info;
  }

  info.message = "Internet no confirmat. HTTP " + String(code);
  info.details = "DNS GitHub OK (" + info.resolvedIp + "), però la prova HTTP externa no ha respost bé. Pot ser tall d'Internet, firewall o portal captiu.";
  return info;
}

GitHubUpdateInfo checkGitHubUpdateNow() {
  GitHubUpdateInfo info;

  if (!configGithubOtaEnabled) {
    info.message = "GitHub OTA desactivat";
    return info;
  }

  if (WiFi.status() != WL_CONNECTED) {
    info.message = "Wi-Fi no connectat";
    return info;
  }

  String manifest;
  int code = 0;
  if (!httpGetString(configGithubManifestUrl, manifest, code)) {
    info.message = "No puc llegir manifest GitHub. HTTP " + String(code);
    if (code == 404) {
      info.message += ". Revisa que sigui raw.githubusercontent.com i no github.com/blob ni raw.githubuser.com";
    }
    return info;
  }

  info.version = jsonValue(manifest, "version");
  info.buildSha = jsonValue(manifest, "build_sha");
  if (info.buildSha.length() == 0) info.buildSha = jsonValue(manifest, "sha");
  info.buildDate = jsonValue(manifest, "build_date");
  info.firmwareUrl = jsonValue(manifest, "firmware_url");
  info.notes = jsonValue(manifest, "notes");
  info.sizeBytes = (uint32_t)jsonValue(manifest, "size").toInt();

  if (info.firmwareUrl.length() == 0) {
    info.message = "Manifest GitHub sense firmware_url";
    return info;
  }

  String currentSha = currentFirmwareBuildSha();
  info.updateAvailable = info.buildSha.length() > 0 && info.buildSha != currentSha;
  info.ok = true;

  if (info.updateAvailable) {
    info.message = "Nova actualitzacio disponible: " + (info.version.length() ? info.version : String("sense versio"));
  } else {
    info.message = "Firmware al dia";
  }

  return info;
}

bool performGitHubOtaUpdate(String& messageOut) {
  GitHubUpdateInfo info = checkGitHubUpdateNow();
  if (!info.ok) {
    messageOut = info.message;
    return false;
  }

  if (!info.updateAvailable && !configGithubAllowSameVersionUpdate) {
    messageOut = "No hi ha cap actualitzacio nova";
    return false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    messageOut = "Wi-Fi no connectat";
    return false;
  }

  WiFiClientSecure secureClient;
  WiFiClient plainClient;
  HTTPClient http;

  bool https = info.firmwareUrl.startsWith("https://");
  if (https) {
    secureClient.setInsecure();
    if (!http.begin(secureClient, info.firmwareUrl)) {
      messageOut = "No puc obrir URL firmware";
      return false;
    }
  } else {
    if (!http.begin(plainClient, info.firmwareUrl)) {
      messageOut = "No puc obrir URL firmware";
      return false;
    }
  }

  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(30000);
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    messageOut = "Error descarregant firmware. HTTP " + String(code);
    http.end();
    return false;
  }

  int contentLength = http.getSize();
  if (contentLength <= 0) {
    messageOut = "Firmware sense mida valida";
    http.end();
    return false;
  }

  appState.otaInProgress = true;
  appState.otaLastMessage = "GitHub OTA descarregant";

  if (!Update.begin((size_t)contentLength)) {
    messageOut = "No hi ha espai per OTA";
    appState.otaInProgress = false;
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  size_t written = Update.writeStream(*stream);
  if (written != (size_t)contentLength) {
    messageOut = "OTA incompleta: " + String(written) + "/" + String(contentLength);
    Update.abort();
    appState.otaInProgress = false;
    http.end();
    return false;
  }

  if (!Update.end()) {
    messageOut = "Error finalitzant OTA: " + String(Update.getError());
    appState.otaInProgress = false;
    http.end();
    return false;
  }

  if (!Update.isFinished()) {
    messageOut = "OTA no finalitzada";
    appState.otaInProgress = false;
    http.end();
    return false;
  }

  http.end();
  appState.otaSuccess = true;
  appState.otaLastMessage = "GitHub OTA completada. Reiniciant...";
  messageOut = appState.otaLastMessage;
  return true;
}
