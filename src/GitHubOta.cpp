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


static void parseVersionTriple(const String& version, int out[3]) {
  out[0] = 0;
  out[1] = 0;
  out[2] = 0;
  int idx = 0;
  String number;
  bool started = false;
  for (int i = 0; i < (int)version.length() && idx < 3; i++) {
    char c = version[i];
    if (isDigit((unsigned char)c)) {
      number += c;
      started = true;
    } else if (started) {
      out[idx++] = number.toInt();
      number = "";
      started = false;
      if (c != '.') {
        // Continue scanning because versions like "v1.6.3-name" are valid.
      }
    }
  }
  if (idx < 3 && number.length()) {
    out[idx++] = number.toInt();
  }
}

static int compareFirmwareVersionStrings(const String& remoteVersion, const String& currentVersion) {
  int r[3];
  int c[3];
  parseVersionTriple(remoteVersion, r);
  parseVersionTriple(currentVersion, c);
  for (int i = 0; i < 3; i++) {
    if (r[i] > c[i]) return 1;
    if (r[i] < c[i]) return -1;
  }
  return 0;
}

static String bytesText(uint32_t bytes) {
  if (bytes == 0) return "mida no informada";
  if (bytes < 1024) return String(bytes) + " B";
  if (bytes < 1024UL * 1024UL) return String(bytes / 1024.0f, 1) + " KB";
  return String(bytes / (1024.0f * 1024.0f), 2) + " MB";
}


void otaClearLog(const String& firstLine) {
  appState.otaLog = "";
  appState.otaLogSeq++;
  if (firstLine.length()) {
    otaAppendLog(firstLine);
  }
}

void otaAppendLog(const String& line) {
  String entry = "[" + String(millis() / 1000UL) + "s] " + line;
  Serial.print("[OTA] " );
  Serial.println(line);

  appState.otaLog += entry;
  appState.otaLog += "\n";
  appState.otaLogSeq++;

  const uint16_t maxLen = 3600;
  if (appState.otaLog.length() > maxLen) {
    int cut = appState.otaLog.indexOf('\n', appState.otaLog.length() - maxLen);
    if (cut > 0) {
      appState.otaLog = appState.otaLog.substring(cut + 1);
    } else {
      appState.otaLog = appState.otaLog.substring(appState.otaLog.length() - maxLen);
    }
  }
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

GitHubUpdateInfo checkGitHubUpdateNow(bool verboseLog) {
  GitHubUpdateInfo info;
  if (verboseLog) otaAppendLog("Comprovant manifest GitHub: " + configGithubManifestUrl);

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
    if (verboseLog) otaAppendLog("Manifest GitHub no accessible. HTTP " + String(code));
    info.httpCode = code;
    info.message = "Manifest no accessible";
    info.details = "HTTP " + String(code) + ". ";
    if (code == 404) {
      info.details += "Revisa que sigui una URL raw.githubusercontent.com i que existeixi firmware/manifest.json.";
    } else {
      info.details += "Revisa Internet, DNS, GitHub o el manifest configurat.";
    }
    return info;
  }
  info.httpCode = code;
  if (verboseLog) otaAppendLog("Manifest GitHub llegit correctament. Mida resposta: " + String(manifest.length()) + " bytes");

  info.version = jsonValue(manifest, "version");
  info.buildSha = jsonValue(manifest, "build_sha");
  if (info.buildSha.length() == 0) info.buildSha = jsonValue(manifest, "sha");
  info.buildDate = jsonValue(manifest, "build_date");
  info.firmwareUrl = jsonValue(manifest, "firmware_url");
  info.notes = jsonValue(manifest, "notes");
  info.sizeBytes = (uint32_t)jsonValue(manifest, "size").toInt();

  if (info.firmwareUrl.length() == 0) {
    if (verboseLog) otaAppendLog("Manifest invalid: no porta firmware_url");
    info.message = "Manifest GitHub invalid";
    info.details = "El manifest s'ha llegit, però no porta firmware_url.";
    return info;
  }

  String currentVersion = String(FIRMWARE_VERSION);
  String currentSha = currentFirmwareBuildSha();
  int versionCmp = 0;
  if (info.version.length() > 0) {
    versionCmp = compareFirmwareVersionStrings(info.version, currentVersion);
  }

  info.sameVersion = info.version.length() > 0 && versionCmp == 0;
  info.remoteOlder = info.version.length() > 0 && versionCmp < 0;

  if (info.version.length() == 0) {
    // Manifest antic o incomplet: només podem comparar SHA.
    info.updateAvailable = info.buildSha.length() > 0 && info.buildSha != currentSha;
  } else {
    info.updateAvailable = versionCmp > 0;
  }

  info.ok = true;

  String remoteVersion = info.version.length() ? info.version : String("sense versio");
  String remoteSha = info.buildSha.length() ? shortBuildSha(info.buildSha) : String("sense SHA");
  info.details = "Remota " + remoteVersion + " · SHA " + remoteSha + " · " + bytesText(info.sizeBytes);
  if (info.buildDate.length()) {
    info.details += " · " + info.buildDate;
  }

  if (info.updateAvailable) {
    info.message = "Nova versio disponible";
    if (verboseLog) otaAppendLog("Nova versio disponible: " + remoteVersion + " · " + remoteSha);
  } else if (info.remoteOlder) {
    info.message = "GitHub te una versio mes antiga";
    if (verboseLog) otaAppendLog("GitHub publica una versio mes antiga: " + remoteVersion);
    info.details += ". No s'ofereix downgrade.";
  } else if (info.sameVersion) {
    info.message = "Ja tens aquesta versio";
    if (verboseLog) otaAppendLog("La versio remota coincideix amb la local: " + remoteVersion);
  } else {
    info.message = "Firmware al dia";
  }

  return info;
}

bool performGitHubOtaUpdate(String& messageOut, OtaProgressCallback progressCallback) {
  otaClearLog("Inici GitHub OTA");
  appState.otaInProgress = true;
  appState.otaSuccess = false;
  appState.otaProgressSource = "GitHub";
  appState.otaProgressPhase = "comprovant";
  appState.otaProgressBytes = 0;
  appState.otaProgressTotal = 0;
  appState.otaProgressPercent = 0;
  appState.otaProgressMillis = millis();
  appState.otaLastMessage = "Comprovant manifest GitHub";
  if (progressCallback) progressCallback();

  GitHubUpdateInfo info = checkGitHubUpdateNow(true);
  if (!info.ok) {
    messageOut = info.message;
    appState.otaInProgress = false;
    appState.otaProgressPhase = "error";
    appState.otaLastMessage = messageOut;
    otaAppendLog("Aturat: " + messageOut);
    if (progressCallback) progressCallback();
    return false;
  }

  if (info.remoteOlder && !configGithubAllowSameVersionUpdate) {
    messageOut = "La versio publicada a GitHub es mes antiga que la local. No faig downgrade.";
    appState.otaInProgress = false;
    appState.otaProgressPhase = "error";
    appState.otaLastMessage = messageOut;
    otaAppendLog("Aturat: downgrade bloquejat");
    if (progressCallback) progressCallback();
    return false;
  }

  if (!info.updateAvailable && !configGithubAllowSameVersionUpdate) {
    messageOut = "No hi ha cap actualitzacio nova";
    appState.otaInProgress = false;
    appState.otaProgressPhase = "error";
    appState.otaLastMessage = messageOut;
    otaAppendLog("Aturat: no hi ha update aplicable");
    if (progressCallback) progressCallback();
    return false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    messageOut = "Wi-Fi no connectat";
    appState.otaInProgress = false;
    appState.otaProgressPhase = "error";
    appState.otaLastMessage = messageOut;
    otaAppendLog("Aturat: Wi-Fi no connectat");
    if (progressCallback) progressCallback();
    return false;
  }

  uint32_t totalSize = info.sizeBytes;
  if (totalSize == 0) {
    messageOut = "Manifest sense mida de firmware";
    appState.otaInProgress = false;
    appState.otaProgressPhase = "error";
    appState.otaLastMessage = messageOut;
    otaAppendLog("Aturat: el manifest no porta size o size es 0");
    if (progressCallback) progressCallback();
    return false;
  }

  otaAppendLog("Firmware URL: " + info.firmwareUrl);
  otaAppendLog("Mida firmware segons manifest: " + String(totalSize) + " bytes");
  otaAppendLog("Mode descarrega: HTTP Range per blocs de 32 KB amb reconnexio si GitHub talla o s'adorm");

  appState.otaInProgress = true;
  appState.otaSuccess = false;
  appState.otaProgressSource = "GitHub";
  appState.otaProgressPhase = "descarregant";
  appState.otaProgressBytes = 0;
  appState.otaProgressTotal = totalSize;
  appState.otaProgressPercent = 0;
  appState.otaProgressMillis = millis();
  appState.otaLastMessage = "Preparant OTA GitHub";
  if (progressCallback) progressCallback();

  if (!Update.begin((size_t)totalSize)) {
    messageOut = "No hi ha espai per OTA";
    appState.otaInProgress = false;
    appState.otaProgressPhase = "error";
    appState.otaLastMessage = messageOut;
    otaAppendLog("Update.begin ha fallat. Error " + String(Update.getError()));
    if (progressCallback) progressCallback();
    return false;
  }
  otaAppendLog("Update.begin OK. Començo a escriure flash per blocs.");

  const size_t rangeBlockSize = 32768;
  uint8_t buffer[2048];
  size_t written = 0;
  uint8_t lastLoggedPercent = 255;
  unsigned long lastProgressMillis = 0;
  unsigned long lastWaitLogMillis = 0;
  uint8_t reconnectsWithoutProgress = 0;

  while (written < (size_t)totalSize) {
    size_t startByte = written;
    size_t endByte = startByte + rangeBlockSize - 1;
    if (endByte >= (size_t)totalSize) endByte = (size_t)totalSize - 1;
    size_t expectedThisRequest = endByte - startByte + 1;

    WiFiClientSecure secureClient;
    WiFiClient plainClient;
    HTTPClient http;
    bool https = info.firmwareUrl.startsWith("https://");

    if (https) {
      secureClient.setInsecure();
      secureClient.setTimeout(20000);
      if (!http.begin(secureClient, info.firmwareUrl)) {
        messageOut = "No puc obrir URL firmware";
        Update.abort();
        appState.otaInProgress = false;
        appState.otaProgressPhase = "error";
        appState.otaLastMessage = messageOut;
        otaAppendLog("Error obrint HTTPS firmware");
        if (progressCallback) progressCallback();
        return false;
      }
    } else {
      plainClient.setTimeout(20000);
      if (!http.begin(plainClient, info.firmwareUrl)) {
        messageOut = "No puc obrir URL firmware";
        Update.abort();
        appState.otaInProgress = false;
        appState.otaProgressPhase = "error";
        appState.otaLastMessage = messageOut;
        otaAppendLog("Error obrint HTTP firmware");
        if (progressCallback) progressCallback();
        return false;
      }
    }

    String rangeHeader = "bytes=" + String(startByte) + "-" + String(endByte);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(20000);
    http.setReuse(false);
    http.useHTTP10(true);
    http.addHeader("Range", rangeHeader);
    http.addHeader("Connection", "close");

    if (lastLoggedPercent == 255 || millis() - lastWaitLogMillis > 8000UL) {
      lastWaitLogMillis = millis();
      otaAppendLog("Demanant bloc GitHub Range " + rangeHeader);
    }

    int code = http.GET();
    if (code != HTTP_CODE_PARTIAL_CONTENT && !(code == HTTP_CODE_OK && startByte == 0 && expectedThisRequest == (size_t)totalSize)) {
      messageOut = "GitHub no accepta descarrega parcial. HTTP " + String(code);
      Update.abort();
      appState.otaInProgress = false;
      appState.otaProgressPhase = "error";
      appState.otaLastMessage = messageOut;
      otaAppendLog("Error HTTP Range. Code " + String(code) + " per " + rangeHeader);
      http.end();
      if (progressCallback) progressCallback();
      return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    stream->setTimeout(8000);
    size_t receivedThisRequest = 0;
    unsigned long lastDataMillis = millis();
    bool requestHadProgress = false;

    while (receivedThisRequest < expectedThisRequest && written < (size_t)totalSize) {
      size_t remainingInRequest = expectedThisRequest - receivedThisRequest;
      size_t toRead = remainingInRequest;
      if (toRead > sizeof(buffer)) toRead = sizeof(buffer);

      int bytesRead = stream->readBytes(buffer, toRead);
      if (bytesRead <= 0) {
        unsigned long now = millis();
        if (now - lastDataMillis > 25000UL) {
          otaAppendLog("Bloc sense dades. Reconnectare des del byte " + String(written));
          break;
        }
        if (now - lastWaitLogMillis > 5000UL) {
          lastWaitLogMillis = now;
          appState.otaLastMessage = "Esperant dades GitHub... " + String(written) + "/" + String(totalSize) + " bytes";
          otaAppendLog(appState.otaLastMessage);
          if (progressCallback) progressCallback();
        }
        delay(20);
        continue;
      }

      lastDataMillis = millis();
      size_t bytesWritten = Update.write(buffer, (size_t)bytesRead);
      if (bytesWritten != (size_t)bytesRead) {
        messageOut = "Error escrivint OTA a la flash";
        Update.abort();
        appState.otaInProgress = false;
        appState.otaProgressPhase = "error";
        appState.otaLastMessage = messageOut;
        otaAppendLog("Update.write parcial. Llegits " + String(bytesRead) + " bytes, escrits " + String(bytesWritten));
        http.end();
        if (progressCallback) progressCallback();
        return false;
      }

      written += bytesWritten;
      receivedThisRequest += bytesWritten;
      requestHadProgress = true;
      appState.otaProgressBytes = (uint32_t)written;
      appState.otaProgressPercent = (uint8_t)((written * 100UL) / (size_t)totalSize);
      appState.otaProgressMillis = millis();
      appState.otaLastMessage = "GitHub OTA descarregant " + String(appState.otaProgressPercent) + "%";

      if (lastLoggedPercent == 255 || appState.otaProgressPercent >= lastLoggedPercent + 5 || appState.otaProgressPercent == 100) {
        lastLoggedPercent = appState.otaProgressPercent;
        otaAppendLog("Progres descarrega: " + String(appState.otaProgressPercent) + "% · " + String(written) + "/" + String(totalSize) + " bytes");
      }

      if (progressCallback && millis() - lastProgressMillis > 350) {
        lastProgressMillis = millis();
        progressCallback();
      }
      delay(0);
    }

    http.end();

    if (requestHadProgress) {
      reconnectsWithoutProgress = 0;
    } else {
      reconnectsWithoutProgress++;
      otaAppendLog("Reconnexio sense progres (" + String(reconnectsWithoutProgress) + "/8)");
      if (reconnectsWithoutProgress >= 8) {
        messageOut = "Timeout OTA: GitHub no envia mes dades";
        Update.abort();
        appState.otaInProgress = false;
        appState.otaProgressPhase = "error";
        appState.otaLastMessage = messageOut;
        otaAppendLog("Aturat: massa reconnexions sense progres. Bytes escrits: " + String(written) + "/" + String(totalSize));
        if (progressCallback) progressCallback();
        return false;
      }
      delay(300);
    }
  }

  if (written != (size_t)totalSize) {
    messageOut = "OTA incompleta: " + String(written) + "/" + String(totalSize);
    Update.abort();
    appState.otaInProgress = false;
    appState.otaProgressPhase = "error";
    appState.otaLastMessage = messageOut;
    if (progressCallback) progressCallback();
    return false;
  }

  appState.otaProgressPhase = "verificant";
  appState.otaLastMessage = "Verificant firmware";
  otaAppendLog("Descarrega completa. Verificant firmware.");
  if (progressCallback) progressCallback();

  if (!Update.end()) {
    messageOut = "Error finalitzant OTA: " + String(Update.getError());
    appState.otaInProgress = false;
    appState.otaProgressPhase = "error";
    appState.otaLastMessage = messageOut;
    otaAppendLog(messageOut);
    if (progressCallback) progressCallback();
    return false;
  }

  if (!Update.isFinished()) {
    messageOut = "OTA no finalitzada";
    appState.otaInProgress = false;
    appState.otaProgressPhase = "error";
    appState.otaLastMessage = messageOut;
    otaAppendLog(messageOut);
    if (progressCallback) progressCallback();
    return false;
  }

  appState.otaProgressPhase = "completada";
  appState.otaProgressPercent = 100;
  appState.otaProgressBytes = appState.otaProgressTotal;
  appState.otaProgressMillis = millis();
  appState.otaSuccess = true;
  appState.otaLastMessage = "GitHub OTA completada. Reiniciant...";
  otaAppendLog("OTA completada correctament. Reiniciant boia.");
  if (progressCallback) progressCallback();
  messageOut = appState.otaLastMessage;
  return true;
}
