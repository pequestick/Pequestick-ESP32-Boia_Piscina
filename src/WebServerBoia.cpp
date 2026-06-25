#include "WebServerBoia.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <WebSocketsServer.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <FS.h>
#include <SD.h>
#include <mbedtls/sha256.h>

#include "AppConfig.h"
#include "AppState.h"
#include "Utils.h"
#include "WifiManagerBoia.h"
#include "MqttManager.h"
#include "HardwareManager.h"
#include "GitHubOta.h"
#include "AuthManager.h"
#include "BatteryMonitor.h"
#include "SdManager.h"

// ==========================
// OBJECTE WEB SERVER
// ==========================

static WebServer server(WEB_SERVER_PORT);
static WebSocketsServer webSocket(81);
static unsigned long lastWebSocketBroadcastMillis = 0;
static const unsigned long WEBSOCKET_BROADCAST_INTERVAL_MS = 1000;
static void notifyOtaProgressNow();
static String buildStatusJsonPayload();
static unsigned long lastFailedLoginMillis = 0;
static mbedtls_sha256_context localOtaShaContext;
static bool localOtaShaActive = false;
static String localOtaExpectedSha256;
static uint32_t localOtaExpectedSize = 0;
static uint32_t localOtaReceivedSize = 0;
static String localOtaUploadPath;
static bool localOtaChunkRejected = false;
static bool restartPending = false;
static unsigned long restartAtMillis = 0;

static void scheduleRestart(unsigned long delayMs = 8000) {
  restartPending = true;
  restartAtMillis = millis() + delayMs;
  Serial.println("Reinici programat després de respondre al navegador.");
}

static void finishLocalOtaHash() {
  if (!localOtaShaActive) return;
  mbedtls_sha256_free(&localOtaShaContext);
  localOtaShaActive = false;
}

static bool validSha256Header(const String& value) {
  if (value.length() != 64) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value.charAt(i);
    if (!isHexadecimalDigit(c)) return false;
  }
  return true;
}

static String sha256Hex(const uint8_t digest[32]) {
  static const char* HEX_CHARS = "0123456789abcdef";
  String output;
  output.reserve(64);
  for (size_t i = 0; i < 32; ++i) {
    output += HEX_CHARS[(digest[i] >> 4) & 0x0f];
    output += HEX_CHARS[digest[i] & 0x0f];
  }
  return output;
}

// ==========================
// HTML HELPERS
// ==========================


static String elapsedText(unsigned long whenMillis) {
  if (whenMillis == 0) return "mai";
  unsigned long ageSec = (millis() - whenMillis) / 1000UL;
  if (ageSec < 5) return "ara mateix";
  if (ageSec < 60) return "fa " + String(ageSec) + " s";
  unsigned long ageMin = ageSec / 60UL;
  if (ageMin < 60) return "fa " + String(ageMin) + " min";
  unsigned long ageHour = ageMin / 60UL;
  return "fa " + String(ageHour) + " h";
}

static String uptimeText(unsigned long seconds) {
  unsigned long days = seconds / 86400UL;
  seconds %= 86400UL;
  unsigned long hours = seconds / 3600UL;
  seconds %= 3600UL;
  unsigned long minutes = seconds / 60UL;
  seconds %= 60UL;

  String out = "";
  if (days > 0) {
    out += String(days);
    out += days == 1 ? " dia " : " dies ";
  }
  if (hours > 0 || days > 0) {
    out += String(hours);
    out += " h ";
  }
  if (minutes > 0 || hours > 0 || days > 0) {
    out += String(minutes);
    out += " min";
  } else {
    out += String(seconds);
    out += " s";
  }
  out.trim();
  return out;
}

static void redirectToMaintenanceOta() {
  server.sendHeader("Location", "/maintenance?section=mnt-ota", true);
  server.send(303, "text/plain", "");
}

static String otaUpdateClass() {
  if (!appState.githubUpdateChecked) return "info";
  if (!appState.githubUpdateOk) return "bad";
  if (appState.githubUpdateAvailable) return "warn";
  if (appState.githubRemoteOlder) return "bad";
  return "ok";
}

static String internetClass() {
  if (!appState.internetCheckDone) return "info";
  return appState.internetCheckOk ? "ok" : "bad";
}

static String githubClass() {
  if (!appState.githubUpdateChecked) return "info";
  return appState.githubUpdateOk ? "ok" : "bad";
}

static String statusClass(bool ok);

static void appendHtmlHeader(String& html, const String& title, bool autoRefresh) {
  html += "<!DOCTYPE html>";
  html += "<html lang='ca'>";
  html += "<head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";

  (void)autoRefresh;

  html += "<title>";
  html += htmlEscape(title);
  html += "</title>";

  html += "<style>";
  html += ":root{--bg:#0b1120;--panel:#111827;--panel2:#172033;--line:#263449;--text:#e5e7eb;--muted:#94a3b8;--brand:#38bdf8;--brand2:#2563eb;--ok:#22c55e;--warn:#facc15;--bad:#ef4444;}";
  html += "*{box-sizing:border-box;}html{scroll-behavior:smooth;min-height:100%;}";
  html += "body{font-family:Inter,Segoe UI,Arial,sans-serif;background:radial-gradient(circle at top left,#17324d 0,#0b1120 38%,#050816 100%);background-attachment:fixed;color:var(--text);margin:0;padding:0;min-height:100vh;}";
  html += ".container{max-width:1440px;margin:0 auto;padding:18px;min-height:100vh;display:flex;flex-direction:column;}";
  html += ".app-shell{display:grid;grid-template-columns:250px minmax(0,1fr);gap:18px;align-items:start;flex:1;min-height:0;}";
  html += ".sidebar{position:sticky;top:14px;margin-top:18px;background:rgba(15,23,42,.90);border:1px solid rgba(148,163,184,.16);border-radius:18px;padding:12px;box-shadow:0 22px 60px rgba(0,0,0,.24);backdrop-filter:blur(10px);overflow:visible;}";
  html += ".main{min-width:0;display:flex;flex-direction:column;min-height:100%;}";
  html += ".side-brand{padding:4px 4px 10px;margin-bottom:10px;border-bottom:1px solid rgba(148,163,184,.14);}";
  html += ".menu-title{font-size:11px;font-weight:950;letter-spacing:.14em;color:#94a3b8;text-transform:uppercase;padding:0 3px 8px;border-bottom:1px solid rgba(148,163,184,.16);margin-bottom:8px;}";
  html += ".menu-group{margin:0 0 5px;}.menu-parent{display:flex;align-items:center;justify-content:space-between;gap:8px;}.subnav{display:none;gap:2px;margin:4px 0 4px 13px;padding-left:9px;border-left:1px solid rgba(148,163,184,.16);}.menu-group.open .subnav{display:grid;}.subnav a{display:block;color:#94a3b8;text-decoration:none;font-size:12px;font-weight:750;padding:5px 8px;border-radius:8px;}.subnav a:hover,.subnav a.active{background:rgba(56,189,248,.10);color:#e0f2fe;}.chev{margin-left:auto;color:#64748b;font-size:11px;transition:transform .16s ease;}.menu-group.open .chev{transform:rotate(90deg);color:#7dd3fc;}";
  html += ".topbar{background:rgba(15,23,42,.86);border:1px solid rgba(148,163,184,.16);border-radius:22px;padding:18px;margin-bottom:18px;box-shadow:0 22px 60px rgba(0,0,0,.28);backdrop-filter:blur(10px);}";
  html += ".brandrow{display:flex;align-items:flex-start;justify-content:space-between;gap:14px;flex-wrap:wrap;}";
  html += ".brand h1{margin:0;font-size:30px;letter-spacing:-.03em;}";
  html += ".brand .sub{margin-top:5px;color:var(--muted);font-size:14px;}";
  html += ".servicebar{display:flex;gap:8px;flex-wrap:wrap;justify-content:flex-end;}";
  html += ".header-right{display:flex;flex-direction:column;align-items:flex-end;gap:10px;min-width:0;}.header-actions{display:flex;gap:8px;align-items:center;justify-content:flex-end;flex-wrap:wrap}.header-actions form{display:inline-flex;margin:0;gap:0}.header-btn{display:inline-flex;align-items:center;justify-content:center;border-radius:999px;padding:8px 12px;font-size:13px;font-weight:900;text-decoration:none;border:1px solid #334155;background:#1e293b;color:#e5e7eb;line-height:1}.header-btn.secondary{background:#334155}.header-btn.danger{background:#7f1d1d;border-color:rgba(248,113,113,.45);color:#fecaca}.header-btn:hover{filter:brightness(1.12)}";
  html += ".svc{display:inline-flex;align-items:center;gap:7px;border:1px solid var(--line);border-radius:999px;padding:8px 11px;background:#0f172a;color:#cbd5e1;font-size:13px;font-weight:700;}";
  html += ".svc.ok{border-color:rgba(34,197,94,.45);background:rgba(34,197,94,.12);color:#bbf7d0;}";
  html += ".svc.warn{border-color:rgba(250,204,21,.45);background:rgba(250,204,21,.12);color:#fef08a;}";
  html += ".svc.bad{border-color:rgba(239,68,68,.5);background:rgba(239,68,68,.12);color:#fecaca;}";
  html += ".tabs{display:grid;gap:7px;margin:12px 0 0;padding:0;}";
  html += ".subtabs{display:none;}";
  html += ".subtabs-title{font-size:12px;color:var(--muted);font-weight:900;text-transform:uppercase;letter-spacing:.07em;margin-right:4px;}";
  html += ".subtab{display:inline-flex;align-items:center;gap:6px;color:#cbd5e1;text-decoration:none;border:1px solid #334155;border-radius:999px;padding:8px 11px;background:#0b1220;font-size:13px;font-weight:850;}";
  html += ".subtab.active{background:rgba(56,189,248,.18);color:#fff;border-color:#38bdf8;box-shadow:0 0 0 3px rgba(56,189,248,.08);}";
  html += ".subtab:hover{background:rgba(56,189,248,.12);color:#fff;border-color:#38bdf8;}";
  html += ".card[id]{scroll-margin-top:92px;}";
  html += ".tab{position:relative;display:flex;align-items:center;gap:8px;width:100%;color:#cbd5e1;text-decoration:none;padding:9px 10px;font-weight:850;border-radius:12px;border:1px solid transparent;margin:0;background:transparent;font-size:14px;text-align:left;line-height:1.1;}";
  html += ".tab:hover{background:rgba(56,189,248,.08);color:#fff;}button.tab{cursor:pointer;}";
  html += ".tab.active,.menu-group.open>.tab{background:linear-gradient(180deg,rgba(56,189,248,.16),rgba(37,99,235,.10));color:#fff;border-color:rgba(56,189,248,.35);box-shadow:inset 3px 0 0 var(--brand);}";
  html += ".card{background:rgba(17,24,39,.94);border:1px solid rgba(148,163,184,.14);border-radius:18px;padding:20px;margin:18px 0;box-shadow:0 12px 32px rgba(0,0,0,.22);}";
  html += "h2{margin:0 0 14px;font-size:18px;color:#bae6fd;letter-spacing:-.01em;}";
  html += "h3{margin:18px 0 10px;font-size:16px;color:#dbeafe;}";
  html += ".temp{font-size:58px;font-weight:900;margin:8px 0;letter-spacing:-.04em;}";
  html += ".unit{font-size:24px;color:#cbd5e1;}";
  html += ".grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px;}";
  html += ".grid3{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px;}";
  html += ".item{background:linear-gradient(180deg,#182235,#121a2b);border:1px solid rgba(148,163,184,.12);border-radius:14px;padding:14px;min-width:0;}";
  html += ".label{font-size:12px;color:var(--muted);margin-bottom:7px;text-transform:uppercase;letter-spacing:.06em;}";
  html += ".value{font-size:19px;font-weight:850;word-break:break-word;}";
  html += ".ok{color:#86efac;}.warn{color:#fef08a;}.bad{color:#fca5a5;}";
  html += ".small{font-size:13px;color:var(--muted);line-height:1.45;margin-top:6px;}";
  html += ".hint{font-size:14px;color:#cbd5e1;line-height:1.55;}";
  html += "a{color:#7dd3fc;}";
  html += "form{display:grid;gap:14px;}";
  html += "input,select,textarea{width:100%;background:#0b1220;color:var(--text);border:1px solid #334155;border-radius:12px;padding:11px 12px;font-size:15px;outline:none;}";
  html += "input:focus,select:focus,textarea:focus{border-color:#38bdf8;box-shadow:0 0 0 3px rgba(56,189,248,.12);}";
  html += "input[type=checkbox]{width:auto;transform:scale(1.2);margin-right:8px;}input[type=file]{padding:9px;}";
  html += ".password-wrap{position:relative;}.password-wrap input{padding-right:52px;}.eye-button{position:absolute;right:6px;top:50%;transform:translateY(-50%);background:#1e293b;color:#e5e7eb;border:1px solid #475569;border-radius:9px;padding:7px 10px;font-size:15px;line-height:1;cursor:pointer;}";
  html += ".wifi-scan-actions{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin-top:10px}.wifi-scan-list{display:grid;gap:7px;margin-top:10px}.wifi-network{display:flex;justify-content:space-between;align-items:center;gap:12px;width:100%;text-align:left;background:#0b1220;border:1px solid #334155;padding:10px 12px}.wifi-network:hover{border-color:#38bdf8;background:rgba(56,189,248,.10)}.wifi-network-name{font-weight:850;overflow-wrap:anywhere}.wifi-network-meta{color:#94a3b8;font-size:12px;white-space:nowrap}";
  html += "button,.action-link,.btn{display:inline-flex;align-items:center;justify-content:center;background:linear-gradient(180deg,#2563eb,#1d4ed8);color:white;border:0;border-radius:12px;padding:12px 16px;font-size:15px;font-weight:850;cursor:pointer;text-decoration:none}button.secondary,.btn.secondary{background:#475569;}button.danger,.btn.danger{background:#b91c1c;}.buttons,.actions{display:flex;gap:10px;flex-wrap:wrap;align-items:center;}";
  html += "hr{border:0;border-top:1px solid #263449;margin:20px 0;}";
  html += ".footer{color:#94a3b8;font-size:12px;text-align:center;padding:16px 0 4px;margin-top:auto;}";
  html += ".tag{display:inline-flex;align-items:center;border:1px solid #334155;border-radius:999px;padding:5px 9px;background:#0b1220;color:#cbd5e1;font-size:12px;font-weight:750;margin:3px 4px 3px 0;}";
  html += ".ota-hero{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:12px;margin:14px 0;}";
  html += ".ota-tile{position:relative;background:linear-gradient(180deg,#182235,#111827);border:1px solid rgba(148,163,184,.15);border-radius:16px;padding:15px;min-height:122px;overflow:hidden;}";
  html += ".ota-tile:before{content:'';position:absolute;inset:0 0 auto 0;height:3px;background:#334155;}";
  html += ".ota-tile.ok:before{background:#22c55e}.ota-tile.warn:before{background:#facc15}.ota-tile.bad:before{background:#ef4444}.ota-tile.info:before{background:#38bdf8}";
  html += ".ota-title{font-size:11px;color:#94a3b8;font-weight:950;letter-spacing:.08em;text-transform:uppercase;margin-bottom:9px;}";
  html += ".ota-main{font-size:20px;font-weight:950;line-height:1.15;margin-bottom:8px;}";
  html += ".ota-main.ok{color:#86efac}.ota-main.warn{color:#fef08a}.ota-main.bad{color:#fca5a5}.ota-main.info{color:#bae6fd}";
  html += ".ota-meta{font-size:12px;color:#94a3b8;line-height:1.45;word-break:break-word;}";
  html += ".ota-badges{display:flex;gap:8px;flex-wrap:wrap;margin:8px 0 14px}.ota-badge{display:inline-flex;align-items:center;gap:6px;border:1px solid #334155;border-radius:999px;padding:6px 9px;background:#0b1220;color:#cbd5e1;font-size:12px;font-weight:850}.ota-badge.ok{border-color:rgba(34,197,94,.45);color:#bbf7d0;background:rgba(34,197,94,.10)}.ota-badge.warn{border-color:rgba(250,204,21,.45);color:#fef08a;background:rgba(250,204,21,.10)}.ota-badge.bad{border-color:rgba(239,68,68,.50);color:#fecaca;background:rgba(239,68,68,.10)}";
  html += ".ota-config{margin-top:14px;padding-top:14px;border-top:1px solid rgba(148,163,184,.14)}";
  html += "body.ota-locked{overflow:hidden}.ota-progress-card{position:fixed;inset:0;z-index:2000;display:flex;align-items:center;justify-content:center;padding:16px;background:rgba(2,6,23,.82);backdrop-filter:blur(7px)}.ota-progress-card.hidden{display:none}.ota-progress-dialog{width:min(900px,100%);max-height:calc(100vh - 32px);overflow:auto;padding:18px;border:1px solid rgba(56,189,248,.38);background:linear-gradient(180deg,#10243a,#0f172a);border-radius:18px;box-shadow:0 28px 80px rgba(0,0,0,.65)}.ota-progress-head{display:flex;justify-content:space-between;gap:12px;align-items:flex-start;margin-bottom:10px}.ota-progress-title{font-weight:950;color:#e0f2fe}.ota-progress-text{font-size:12px;color:#94a3b8;line-height:1.45}.ota-dismiss{display:none;margin-top:10px;padding:7px 10px;font-size:12px}.ota-progress-card.error .ota-dismiss,.ota-progress-card.done .ota-dismiss{display:inline-block}.progress-track{height:15px;background:#020617;border:1px solid #263449;border-radius:999px;overflow:hidden;position:relative}.progress-fill{height:100%;width:0%;background:linear-gradient(90deg,#2563eb,#38bdf8,#22c55e);border-radius:999px;transition:width .25s ease}.progress-fill.indeterminate{width:38%;position:absolute;animation:indeterminate 1.2s infinite ease-in-out}.ota-progress-card.done .progress-fill{background:linear-gradient(90deg,#16a34a,#86efac)}.ota-progress-card.error .progress-fill{background:linear-gradient(90deg,#dc2626,#fca5a5)}@keyframes indeterminate{0%{left:-40%}100%{left:105%}}";
  html += ".ota-log{margin-top:12px;background:#020617;border:1px solid #263449;border-radius:14px;padding:12px;color:#c7d2fe;font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:12px;line-height:1.45;max-height:230px;overflow:auto;white-space:pre-wrap}.ota-log-head{display:flex;justify-content:space-between;align-items:center;margin-top:12px;color:#bfdbfe;font-size:12px;font-weight:900;text-transform:uppercase;letter-spacing:.06em}.ota-log small{color:#64748b}";
  html += "button:disabled{opacity:.45;cursor:not-allowed;background:#334155;}";
  html += "pre{white-space:pre-wrap;word-break:break-word;background:#050b14;border:1px solid #334155;border-radius:14px;padding:14px;color:#dbeafe;}";
  html += "table{width:100%;border-collapse:collapse;margin-top:10px;background:#0b1220;border:1px solid #263449;border-radius:12px;overflow:hidden}th,td{padding:9px 10px;border-bottom:1px solid #263449;text-align:left;font-size:13px;vertical-align:top}th{color:#bae6fd;background:#111827;font-weight:900}td{color:#cbd5e1}tr:last-child td{border-bottom:0}code{background:#050b14;border:1px solid #263449;border-radius:6px;padding:2px 5px;color:#dbeafe}.file-preview{max-height:520px;overflow:auto;white-space:pre;font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:12px}";
  html += ".sd-explorer{border:1px solid #263449;border-radius:16px;overflow:hidden;background:#0b1220}.sd-toolbar{display:flex;gap:8px;align-items:center;flex-wrap:wrap;padding:10px;border-bottom:1px solid #263449;background:#111827}.sd-address{flex:1;min-width:220px;border:1px solid #334155;background:#020617;border-radius:10px;padding:9px 11px;color:#dbeafe;font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:13px}.sd-status{font-size:12px;color:#94a3b8;padding:0 10px 10px}.sd-body{display:grid;grid-template-columns:250px minmax(0,1fr);min-height:390px}.sd-tree{border-right:1px solid #263449;background:#0f172a;padding:10px;display:grid;align-content:start;gap:7px}.sd-tree button{justify-content:flex-start;width:100%;padding:9px 10px;background:#182235;border:1px solid #263449}.sd-tree button:hover{border-color:#38bdf8;background:rgba(56,189,248,.10)}.sd-pane{min-width:0}.sd-list-head,.sd-row{display:grid;grid-template-columns:42px minmax(0,1fr) 110px 150px;align-items:center;gap:8px;padding:9px 12px;border-bottom:1px solid #263449}.sd-list-head{color:#94a3b8;font-size:11px;text-transform:uppercase;letter-spacing:.08em;font-weight:950;background:#101827}.sd-row{color:#cbd5e1;cursor:pointer;background:#0b1220}.sd-row:hover{background:rgba(56,189,248,.08)}.sd-row.selected{background:rgba(56,189,248,.16);outline:1px solid rgba(56,189,248,.35);outline-offset:-1px}.sd-icon{font-size:20px;text-align:center}.sd-name{font-weight:800;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.sd-meta{font-size:12px;color:#94a3b8}.sd-empty{padding:24px;color:#94a3b8}.sd-modal{position:fixed;inset:0;z-index:2200;background:rgba(2,6,23,.82);backdrop-filter:blur(6px);display:flex;align-items:center;justify-content:center;padding:16px}.sd-modal.hidden{display:none}.sd-dialog{width:min(1040px,100%);max-height:calc(100vh - 32px);display:flex;flex-direction:column;background:#0f172a;border:1px solid rgba(56,189,248,.35);border-radius:18px;box-shadow:0 30px 80px rgba(0,0,0,.65);overflow:hidden}.sd-dialog-head{display:flex;justify-content:space-between;gap:12px;align-items:flex-start;padding:14px 16px;border-bottom:1px solid #263449;background:#111827}.sd-dialog-title{font-weight:950;color:#e0f2fe}.sd-dialog-path{font-size:12px;color:#94a3b8;margin-top:4px;word-break:break-all}.sd-dialog-actions{display:flex;gap:8px;flex-wrap:wrap}.sd-preview{margin:0;padding:14px;overflow:auto;white-space:pre;font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:12px;line-height:1.45;color:#dbeafe;background:#020617;min-height:300px}";
  html += ".temp-card{position:relative;overflow:hidden;min-height:350px;padding-bottom:92px;}";
  html += ".temp-card canvas{position:absolute;inset:0;width:100%;height:100%;opacity:.26;pointer-events:none;}";
  html += ".temp-card .temp-content{position:relative;z-index:1}.temp-card h2{padding-right:210px}.temp-metric-row{display:grid;grid-template-columns:repeat(2,minmax(0,360px));gap:12px;margin-top:16px;max-width:760px}.temp-battery-card,.temp-uptime-card{margin-top:0;max-width:none;}";
  html += ".temp-trend{display:inline-block;margin-left:9px;font-size:36px;font-weight:950;line-height:1;vertical-align:7px;text-shadow:0 2px 12px rgba(0,0,0,.35)}.temp-trend.up{color:#22c55e}.temp-trend.down{color:#ef4444}.temp-trend.stable{color:#facc15}.temp-trend.pending{visibility:hidden}";
  html += ".internal-mini{position:absolute;top:0;right:0;display:flex;align-items:center;gap:5px;padding:6px 9px;border:1px solid rgba(125,211,252,.24);border-radius:10px;background:rgba(2,6,23,.58);font-size:11px;color:#94a3b8;white-space:nowrap}.internal-mini b{color:#e0f2fe;font-size:12px}.internal-mini .separator{color:#475569;margin:0 1px}";
  html += ".chart-note{position:absolute;right:18px;bottom:12px;color:#94a3b8;font-size:11px;z-index:2}.history-toolbar{position:absolute;left:18px;bottom:38px;z-index:3;display:flex;gap:5px;flex-wrap:wrap}.history-range{padding:5px 8px;border-radius:999px;background:rgba(15,23,42,.82);border:1px solid #334155;font-size:11px;color:#cbd5e1}.history-range.active{border-color:var(--range-color,#38bdf8);box-shadow:0 0 0 2px color-mix(in srgb,var(--range-color,#38bdf8) 24%,transparent);color:#fff}.chart-legend{position:absolute;left:18px;bottom:12px;z-index:2;display:flex;gap:12px;color:#94a3b8;font-size:11px}.legend-item{display:inline-flex;align-items:center;gap:5px}.legend-line{width:18px;height:2px;background:var(--range-color,#38bdf8)}.legend-band{width:18px;height:8px;border:1px solid var(--range-color,#38bdf8);background:color-mix(in srgb,var(--range-color,#38bdf8) 22%,transparent)}.history-panel{position:relative;overflow:hidden;min-height:270px}.history-panel canvas{position:absolute;inset:48px 0 0;width:100%;height:calc(100% - 48px);opacity:.72;pointer-events:none}.history-panel h3{position:relative;z-index:2;margin:0;padding-right:12px}.history-panel .history-toolbar{top:46px;bottom:auto}.history-panel .chart-note,.history-panel .chart-legend{bottom:10px}";
  html += ".temp-card{padding-bottom:76px}.temp-chart-footer{position:absolute;left:18px;right:18px;bottom:12px;z-index:4;display:grid;grid-template-columns:minmax(0,1fr) auto minmax(0,1fr);gap:12px;align-items:center;pointer-events:none}.temp-chart-footer>*{pointer-events:auto}.temp-card .history-toolbar{position:static;justify-content:center}.temp-card .chart-note{position:static;justify-self:end;white-space:nowrap}.chart-footer-right{display:flex;justify-content:flex-end;align-items:center;gap:10px;min-width:0}.temp-card .chart-legend{position:static;justify-self:auto;display:flex;gap:10px}.temp-card .temp-metric-row{display:flex;gap:7px;align-items:center;justify-content:flex-start;flex-wrap:wrap;margin:0;max-width:none}.temp-battery-card,.temp-uptime-card{margin:0!important;max-width:none!important;padding:5px 8px!important;border-radius:999px!important;background:rgba(15,23,42,.80)!important;border:1px solid rgba(148,163,184,.22)!important;display:flex;align-items:center;gap:5px;white-space:nowrap;box-shadow:none}.temp-battery-card .label,.temp-uptime-card .label{font-size:10px;margin:0;text-transform:none;letter-spacing:.02em;color:#94a3b8}.temp-battery-card .value,.temp-uptime-card .value{font-size:12px;font-weight:950;word-break:normal}.temp-battery-card .small,.temp-uptime-card .small{font-size:10px;margin:0;color:#94a3b8}.temp-battery-card .small span{font-size:10px}";
  html += "@media(max-width:920px){.ota-hero{grid-template-columns:1fr}.app-shell{grid-template-columns:1fr}.sidebar{position:relative;top:0;margin-top:0}.tabs{grid-template-columns:1fr}.container{padding:12px}.grid,.grid3{grid-template-columns:1fr}.temp{font-size:44px}.temp-trend{font-size:29px;vertical-align:5px}.temp-card{min-height:390px;padding-bottom:105px}.temp-card h2{padding-right:165px}.temp-metric-row{grid-template-columns:1fr;max-width:none}.temp-battery-card{max-width:none}.internal-mini{font-size:10px;padding:5px 7px;gap:4px}.internal-mini b{font-size:11px}.history-toolbar{left:12px;bottom:40px}.chart-legend{left:12px;gap:8px}.chart-note{right:12px}.brandrow{display:block}.header-right{align-items:flex-start;margin-top:12px}.servicebar{justify-content:flex-start}.header-actions{justify-content:flex-start}.tab{padding:10px 10px;font-size:14px}.subnav{margin-left:12px}.sd-body{grid-template-columns:1fr}.sd-tree{border-right:0;border-bottom:1px solid #263449;grid-template-columns:repeat(2,minmax(0,1fr))}.sd-list-head,.sd-row{grid-template-columns:34px minmax(0,1fr) 82px}.sd-list-head .sd-col-type,.sd-row .sd-col-type{display:none}}";
  html += "@media(max-width:920px){.temp-card{padding-bottom:134px}.temp-chart-footer{left:12px;right:12px;bottom:12px;grid-template-columns:1fr;gap:7px;align-items:center}.temp-card .temp-metric-row{justify-content:center}.temp-card .history-toolbar{justify-content:center}.chart-footer-right{justify-content:center}.temp-card .chart-note{justify-self:center}.temp-card .chart-legend{display:none}.temp-battery-card .small,.temp-uptime-card .small{display:none}}";
  html += "</style>";

  html += "<script>";
  html += "function togglePassword(id,btn){var input=document.getElementById(id);if(!input)return;if(input.type==='password'){input.type='text';btn.textContent='🙈';btn.setAttribute('aria-label','Ocultar password');}else{input.type='password';btn.textContent='👁️';btn.setAttribute('aria-label','Mostrar password');}}";
  html += "function scanWifiNetworks(){var button=document.getElementById('wifi-scan-button');var status=document.getElementById('wifi-scan-status');var list=document.getElementById('wifi-scan-list');if(!button||!status||!list)return;button.disabled=true;status.textContent='Buscant xarxes properes...';list.replaceChildren();fetch('/wifi-scan',{cache:'no-store'}).then(function(r){if(!r.ok)throw new Error('HTTP '+r.status);return r.json();}).then(function(data){if(data.error)throw new Error(data.error);var networks=Array.isArray(data.networks)?data.networks:[];status.textContent=networks.length?networks.length+' xarxes trobades. Toca una xarxa per seleccionar-la.':'No hi ha cap xarxa visible.';networks.forEach(function(network){var item=document.createElement('button');item.type='button';item.className='wifi-network';var name=document.createElement('span');name.className='wifi-network-name';name.textContent=network.ssid;var meta=document.createElement('span');meta.className='wifi-network-meta';meta.textContent=(network.secure?'🔒 ':'🔓 ')+network.rssi+' dBm';item.appendChild(name);item.appendChild(meta);item.addEventListener('click',function(){var ssid=document.getElementById('wifi_ssid');if(ssid){ssid.value=network.ssid;}var staticIp=document.getElementById('wifi_use_static_ip');if(staticIp)staticIp.checked=false;var password=document.getElementById('wifi_password');if(password)password.focus();status.textContent='Xarxa seleccionada: '+network.ssid+' · aplicarem DHCP';});list.appendChild(item);});}).catch(function(error){status.textContent='No es pot escanejar: '+error.message;}).then(function(){button.disabled=false;});}";
  html += "function txt(id,v){var e=document.getElementById(id);if(e)e.textContent=(v===null||v===undefined)?'--':v;}";
  html += "function cls(id,c){var e=document.getElementById(id);if(e){e.classList.remove('ok','warn','bad');e.classList.add(c);}}";
  html += "function service(id,on,bad,text){var e=document.getElementById(id);if(!e)return;e.classList.remove('ok','warn','bad');e.classList.add(on?'ok':(bad?'bad':'warn'));if(text!==undefined){var sp=e.querySelector('span:last-child');if(sp)sp.textContent=text;}}";
  html += "function bytesHuman(n){n=parseInt(n||0,10);if(!n)return '--';if(n<1024)return n+' B';if(n<1048576)return (n/1024).toFixed(1)+' KB';if(n<1073741824)return (n/1048576).toFixed(2)+' MB';return (n/1073741824).toFixed(2)+' GB';}";
  html += "function uptimeHuman(sec){sec=parseInt(sec||0,10);var d=Math.floor(sec/86400);sec%=86400;var h=Math.floor(sec/3600);sec%=3600;var m=Math.floor(sec/60);if(d>0)return d+' d '+h+' h '+m+' min';if(h>0)return h+' h '+m+' min';if(m>0)return m+' min';return sec+' s';}";
  html += "function updateGithubOtaStatus(d){var internetCls=d.internet_check_done?(d.internet_check_ok?'ok':'bad'):'info';var ghCls=d.github_update_checked?(d.github_update_ok?'ok':'bad'):'info';var updCls='info';if(d.github_update_checked){if(d.github_update_available)updCls='warn';else if(d.github_remote_older)updCls='bad';else if(d.github_update_ok)updCls='ok';}txt('ota-internet-main',d.internet_check_done?d.internet_check_message:'Comprovant...');txt('ota-internet-meta',(d.internet_check_details||'')+(d.internet_resolved_ip?' · DNS '+d.internet_resolved_ip:'')+(d.internet_check_done?' · última prova ara':''));txt('ota-github-main',d.github_update_checked?(d.github_update_ok?'Manifest llegit':'Manifest fallit'):'Comprovant...');txt('ota-github-version',d.github_update_version||'--');txt('ota-github-sha',d.github_update_sha_short||d.github_update_sha||'--');txt('ota-github-date',d.github_update_date||'--');txt('ota-update-main',d.github_update_message||'Encara no comprovat');txt('ota-update-details',d.github_update_details||'');cls('ota-internet-tile',internetCls);cls('ota-github-tile',ghCls);cls('ota-update-tile',updCls);cls('ota-internet-main',internetCls);cls('ota-github-main',ghCls);cls('ota-update-main',updCls);var b=document.getElementById('github-install-button');if(b){b.disabled=!(d.github_update_available||(d.github_remote_same_version&&d.github_allow_same_version_update));}}";
  html += "function updateOtaProgress(d){txt('live-internal-temp',d.internal_temperature_c===null?'Sense dades':d.internal_temperature_c+' °C');txt('live-internal-humidity',d.internal_humidity_percent===null?'Sense dades':d.internal_humidity_percent+' %');var card=document.getElementById('ota-progress-card');if(!card)return;var pct=parseInt(d.ota_progress_percent||0,10);var inProg=!!d.ota_in_progress;var phase=d.ota_progress_phase||'espera';var source=d.ota_progress_source||'cap';var active=(source!=='cap'&&phase!=='espera')||inProg;var interrupted=sessionStorage.getItem('boiaOtaPending')==='1'&&!active;if(interrupted){active=true;source='OTA';phase='interrompuda';}if(phase==='error'||phase==='completada')sessionStorage.removeItem('boiaOtaPending');if(active)card.classList.remove('hidden');else card.classList.add('hidden');setOtaModal(active);var fill=document.getElementById('ota-progress-fill');var pctEl=document.getElementById('ota-progress-percent');var phaseEl=document.getElementById('ota-progress-phase');var msgEl=document.getElementById('ota-progress-message');var bytesEl=document.getElementById('ota-progress-bytes');card.classList.remove('done','error');if(phase==='error'||interrupted)card.classList.add('error');if(phase==='completada')card.classList.add('done');if(fill){fill.classList.remove('indeterminate');if(inProg&&(!pct||pct<1)){fill.classList.add('indeterminate');fill.style.width='38%';}else{fill.style.width=Math.max(0,Math.min(100,pct))+'%';}}if(pctEl)pctEl.textContent=(pct?pct:0)+'%';if(phaseEl)phaseEl.textContent=(source||'OTA')+' · '+phase;if(msgEl)msgEl.textContent=interrupted?'L actualitzacio ha perdut la connexio o la boia ha reiniciat. Comprova la versio i el log abans de repetir-la.':(d.ota_last_message||'Esperant accio OTA');if(bytesEl)bytesEl.textContent=bytesHuman(d.ota_progress_bytes)+' / '+bytesHuman(d.ota_progress_total);var log=document.getElementById('ota-log');if(log&&d.ota_log!==undefined){var atBottom=(log.scrollTop+log.clientHeight+24)>=log.scrollHeight;log.textContent=d.ota_log||'Sense log OTA';if(atBottom)log.scrollTop=log.scrollHeight;}}";
  html += "function runOtaAutoChecks(){if(location.pathname!=='/maintenance'||location.search.indexOf('section=mnt-ota')<0)return;txt('ota-internet-main','Comprovant...');txt('ota-github-main','Comprovant...');fetch('/internet-check-run',{cache:'no-store'}).then(function(r){return r.json();}).then(function(d){applyStatus(d);return fetch('/github-check-update-run',{cache:'no-store'});}).then(function(r){return r.json();}).then(function(d){applyStatus(d);}).catch(function(){txt('ota-update-main','No puc actualitzar estat OTA');txt('ota-update-details','La comprovació automàtica ha fallat. Prova els botons manuals.');});}";
  html += "function applyStatus(d){txt('live-temp',d.temperature_c===null?'Sense dades':d.temperature_c);txt('live-battery',d.battery_percent===null?'Sense dades':d.battery_percent+' %');txt('live-battery-voltage',d.battery_voltage===null?'Sense dades':d.battery_voltage+' V');txt('live-wifi',d.wifi_connected?'Connectat':(d.wifi_ap_active?'AP setup':'Desconnectat'));txt('live-ip',d.ip);txt('live-rssi',d.rssi_dbm===null?'Sense senyal':d.rssi_dbm+' dBm');txt('live-mqtt',d.mqtt_enabled?(d.mqtt_connected?'Connectat':'Desconnectat'):'Desactivat');txt('live-uptime',uptimeHuman(d.uptime_seconds));txt('live-sensor',d.sensor_status||'UNKNOWN');txt('live-reads',d.valid_reads+'/'+d.total_reads);txt('live-hostname',d.device_hostname);txt('live-device-name',d.device_name);service('svc-wifi',d.wifi_connected,d.wifi_ap_active?false:true,d.wifi_connected?'Connectat':(d.wifi_ap_active?'AP setup':'Error'));service('svc-ap',d.wifi_ap_active,false,d.wifi_ap_active?'Actiu':'Inactiu');service('svc-mqtt',d.mqtt_enabled&&d.mqtt_connected,d.mqtt_enabled&&!d.mqtt_connected,d.mqtt_enabled?(d.mqtt_connected?'Connectat':'Error'):'Off');service('svc-ha',d.ha_discovery_enabled&&d.ha_discovery_published,d.ha_discovery_enabled&&!d.ha_discovery_published,d.ha_discovery_enabled?(d.ha_discovery_published?'OK':'Pendent'):'Off');service('svc-sensor',d.sensor_status==='OK',d.sensor_status==='ERROR',d.sensor_status||'UNKNOWN');service('svc-sd',d.sd_mounted,d.sd_enabled&&!d.sd_mounted,d.sd_enabled?(d.sd_mounted?'OK':'Error'):'Off');service('svc-ota',!d.ota_in_progress,d.ota_in_progress,d.ota_in_progress?'En curs':'Disponible');updateGithubOtaStatus(d);updateOtaProgress(d);}";
  html += "function startWS(){if(location.pathname==='/login'||location.pathname==='/change-password')return;try{var ws=new WebSocket('ws://'+location.hostname+':81/');ws.onmessage=function(ev){try{applyStatus(JSON.parse(ev.data));}catch(e){}};ws.onclose=function(){setTimeout(startWS,3000);};ws.onerror=function(){try{ws.close();}catch(e){}};}catch(e){}}";
  html += "function bindConfirms(){document.querySelectorAll('form[data-confirm]').forEach(function(f){if(f.id==='ota-local-form'||f.id==='github-install-form')return;f.addEventListener('submit',function(e){if(!confirm(f.getAttribute('data-confirm'))){e.preventDefault();}});});}";
  html += "function bindAccordion(){document.querySelectorAll('.menu-toggle').forEach(function(btn){btn.addEventListener('click',function(){var g=btn.closest('.menu-group');if(!g)return;var isOpen=g.classList.contains('open');document.querySelectorAll('.menu-group.has-sub').forEach(function(x){x.classList.remove('open');});if(!isOpen)g.classList.add('open');});});}";
  html += "function applySubpage(){var links=[].slice.call(document.querySelectorAll('.subtab'));if(!links.length)return;var params=new URLSearchParams(location.search);var selected=params.get('section');var ids=[];links.forEach(function(a){var u=new URL(a.href,location.href);var id=u.searchParams.get('section');if(id){ids.push(id);}});if(!selected||ids.indexOf(selected)<0)selected=ids[0];ids.forEach(function(id){var el=document.getElementById(id);if(el)el.style.display=(id===selected)?'':'none';});document.querySelectorAll('.subpage-extra').forEach(function(el){el.style.display=(el.getAttribute('data-parent')===selected)?'':'none';});links.forEach(function(a){var u=new URL(a.href,location.href);a.classList.toggle('active',u.searchParams.get('section')===selected);});}";
  html += "function sdParentPath(path){path=(path||'/boia').replace(/\\\\/g,'/');if(path.length>1&&path.endsWith('/'))path=path.slice(0,-1);var i=path.lastIndexOf('/');return i<=0?'/':path.slice(0,i);}";
  html += "function sdName(path){path=(path||'').replace(/\\\\/g,'/');var i=path.lastIndexOf('/');return i>=0?path.slice(i+1):path;}";
  html += "function sdIcon(item){if(item.directory)return '📁';var n=(item.name||'').toLowerCase();if(n.endsWith('.csv'))return '📊';if(n.endsWith('.json')||n.endsWith('.jsonl'))return '🧾';if(n.endsWith('.log')||n.endsWith('.txt'))return '📄';return '📦';}";
  html += "function sdSetStatus(msg,bad){var e=document.getElementById('sd-explorer-status');if(e){e.textContent=msg||'';e.classList.toggle('bad',!!bad);}}";
  html += "function sdSelectRow(row,item){document.querySelectorAll('.sd-row.selected').forEach(function(x){x.classList.remove('selected');});if(row)row.classList.add('selected');var b=document.getElementById('sd-download-selected');if(b){b.disabled=!!(item&&item.directory);b.dataset.path=(item&&!item.directory)?item.path:'';}}";
  html += "function renderSdExplorer(data){var list=document.getElementById('sd-file-list'),addr=document.getElementById('sd-address');if(!list)return;var path=(data&&data.path)||'/boia';if(addr)addr.value=path;list.replaceChildren();if(!data||!data.mounted){list.innerHTML=\"<div class='sd-empty bad'>SD no muntada.</div>\";sdSetStatus('SD no muntada',true);return;}var items=Array.isArray(data.items)?data.items:[];items.sort(function(a,b){if(a.directory!==b.directory)return a.directory?-1:1;return String(a.name).localeCompare(String(b.name));});sdSetStatus(items.length+' elements a '+path,false);if(!items.length){list.innerHTML=\"<div class='sd-empty'>Directori buit</div>\";return;}items.forEach(function(item){var row=document.createElement('div');row.className='sd-row';row.innerHTML=\"<div class='sd-icon'></div><div class='sd-name'></div><div class='sd-meta'></div><div class='sd-meta sd-col-type'></div>\";row.querySelector('.sd-icon').textContent=sdIcon(item);row.querySelector('.sd-name').textContent=item.name||sdName(item.path);row.querySelectorAll('.sd-meta')[0].textContent=item.directory?'--':bytesHuman(item.size);row.querySelector('.sd-col-type').textContent=item.directory?'Directori':'Fitxer';row.addEventListener('click',function(){sdSelectRow(row,item);});row.addEventListener('dblclick',function(){if(item.directory)loadSdExplorer(item.path);else openSdFileModal(item.path,item.name);});list.appendChild(row);});}";
  html += "async function loadSdExplorer(path){var root=document.getElementById('sd-explorer');if(!root)return;path=path||root.dataset.startPath||'/boia';sdSetStatus('Carregant '+path+'...',false);try{var r=await fetch('/sd-list?path='+encodeURIComponent(path),{cache:'no-store'});var d=await r.json();renderSdExplorer(d);}catch(e){sdSetStatus('No puc llegir el directori: '+e.message,true);}}";
  html += "async function openSdFileModal(path,name){var modal=document.getElementById('sd-file-modal');if(!modal)return;modal.classList.remove('hidden');txt('sd-modal-title',name||sdName(path));txt('sd-modal-path',path);var dl=document.getElementById('sd-modal-download');if(dl)dl.href='/sd-download?path='+encodeURIComponent(path);var pre=document.getElementById('sd-modal-content');if(pre)pre.textContent='Carregant fitxer...';try{var r=await fetch('/sd-read?path='+encodeURIComponent(path),{cache:'no-store'});var d=await r.json();if(!r.ok||d.error)throw new Error(d.error||('HTTP '+r.status));if(pre)pre.textContent=(d.truncated?'[Vista retallada: descarrega el fitxer per veure-l complet.]\\n\\n':'')+(d.content||'');}catch(e){if(pre)pre.textContent='No puc obrir el fitxer: '+e.message;}}";
  html += "function closeSdFileModal(){var modal=document.getElementById('sd-file-modal');if(modal)modal.classList.add('hidden');}";
  html += "function bindSdExplorer(){var root=document.getElementById('sd-explorer');if(!root)return;var addr=document.getElementById('sd-address');var openBtn=document.getElementById('sd-open-address');var upBtn=document.getElementById('sd-up-button');var refreshBtn=document.getElementById('sd-refresh-button');var dlBtn=document.getElementById('sd-download-selected');document.querySelectorAll('[data-sd-path]').forEach(function(b){b.addEventListener('click',function(){loadSdExplorer(b.getAttribute('data-sd-path'));});});if(openBtn)openBtn.addEventListener('click',function(){loadSdExplorer(addr?addr.value:'/boia');});if(addr)addr.addEventListener('keydown',function(e){if(e.key==='Enter')loadSdExplorer(addr.value);});if(upBtn)upBtn.addEventListener('click',function(){loadSdExplorer(sdParentPath(addr?addr.value:'/boia'));});if(refreshBtn)refreshBtn.addEventListener('click',function(){loadSdExplorer(addr?addr.value:'/boia');});if(dlBtn)dlBtn.addEventListener('click',function(){if(dlBtn.dataset.path)location.href='/sd-download?path='+encodeURIComponent(dlBtn.dataset.path);});var close=document.getElementById('sd-modal-close');if(close)close.addEventListener('click',closeSdFileModal);var modal=document.getElementById('sd-file-modal');if(modal)modal.addEventListener('click',function(e){if(e.target===modal)closeSdFileModal();});window.addEventListener('keydown',function(e){if(e.key==='Escape')closeSdFileModal();});loadSdExplorer(root.dataset.startPath||'/boia');}";
  html += "function extractHaPoints(raw){var out=[];try{if(raw&&Array.isArray(raw.points)){raw.points.forEach(function(x){var v=parseFloat(x.v);var tm=x.t;if(isFinite(v)&&tm){out.push({t:new Date(tm).getTime(),v:v});}});return out;}var arr=raw;if(Array.isArray(arr)&&Array.isArray(arr[0]))arr=arr[0];if(!Array.isArray(arr))return out;arr.forEach(function(x){var st=(x.state!==undefined)?x.state:x.s;var tm=x.last_changed||x.last_updated||x.lc||x.lu;var v=parseFloat(st);if(isFinite(v)&&tm){out.push({t:new Date(tm).getTime(),v:v});}});}catch(e){}return out;}";
  html += "function updateTempTrend(points){var e=document.getElementById('temp-trend');if(!e)return;e.className='temp-trend pending';if(!points||points.length<3)return;var lastT=points[points.length-1].t;var sample=points.filter(function(p){return p.t>=lastT-6*3600000;});if(sample.length<3)sample=points.slice(-3);var t0=sample[0].t,n=sample.length,sx=0,sy=0,sxy=0,sxx=0;sample.forEach(function(p){var x=(p.t-t0)/3600000;sx+=x;sy+=p.v;sxy+=x*p.v;sxx+=x*x;});var den=n*sxx-sx*sx;if(!den)return;var slope=(n*sxy-sx*sy)/den;var cls=slope>0.05?'up':(slope<-0.05?'down':'stable');var arrow=cls==='up'?'↑':(cls==='down'?'↓':'→');var label=cls==='up'?'L’aigua s’està escalfant':(cls==='down'?'L’aigua s’està refredant':'Temperatura estable');e.className='temp-trend '+cls;e.textContent=arrow;e.title=label+' · '+(slope>=0?'+':'')+slope.toFixed(2)+' °C/h · últimes '+((lastT-t0)/3600000).toFixed(1)+' h';e.setAttribute('aria-label',e.title);}";
  html += "function historyRangeSpec(key){var now=Date.now(),day=86400000;if(key==='31d')return{key:key,start:now-31*day,period:'day',label:'31 dies · resolució diària',hex:'#34d399',rgb:'52,211,153'};if(key==='6m')return{key:key,start:now-183*day,period:'day',label:'6 mesos · resolució diària',hex:'#a78bfa',rgb:'167,139,250'};if(key==='1y')return{key:key,start:now-365*day,period:'day',label:'1 any · resolució diària',hex:'#fb923c',rgb:'251,146,60'};return{key:'48h',start:now-48*3600000,period:'hour',label:'48 hores · resolució horària',hex:'#38bdf8',rgb:'56,189,248'};}";
  html += "function extractHaStatistics(raw,entity){var root=raw&&raw.service_response?raw.service_response:(raw&&raw.result?raw.result:raw);if(root&&root.statistics)root=root.statistics;var arr=Array.isArray(root)?root:(root&&root[entity]);if(!Array.isArray(arr))return[];var out=[];arr.forEach(function(x){var t=typeof x.start==='number'?x.start:new Date(x.start).getTime();var mean=parseFloat(x.mean),mn=parseFloat(x.min),mx=parseFloat(x.max);if(isFinite(t)&&isFinite(mean)){out.push({t:t,mean:mean,min:isFinite(mn)?mn:mean,max:isFinite(mx)?mx:mean});}});return out;}";
  html += "function drawStatisticsChart(c,points,spec){if(!c)return;var note=document.getElementById(c.id+'-note');var ctx=c.getContext('2d'),r=c.getBoundingClientRect(),d=devicePixelRatio||1,w=Math.max(1,Math.floor(r.width*d)),h=Math.max(1,Math.floor(r.height*d));if(c.width!==w)c.width=w;if(c.height!==h)c.height=h;ctx.clearRect(0,0,w,h);var host=c.closest('.history-panel')||c.closest('.card');if(host)host.style.setProperty('--range-color',spec.hex);if(!points||points.length<2){if(note)note.textContent='Sense estadístiques HA per aquesta entitat';return;}var low=Math.min.apply(null,points.map(function(p){return p.min;})),high=Math.max.apply(null,points.map(function(p){return p.max;}));if(high-low<0.2){high+=0.1;low-=0.1;}var padY=h*.15,padL=8*d,padR=78*d,t0=points[0].t,t1=points[points.length-1].t;if(t1<=t0)t1=t0+1;var xMax=Math.max(padL+1,w-padR);var px=function(p){return padL+(p.t-t0)/(t1-t0)*(xMax-padL);},py=function(v){return h-padY-(v-low)/(high-low)*(h-padY*2);};ctx.save();ctx.lineWidth=Math.max(1,d);ctx.strokeStyle='rgba(148,163,184,.18)';ctx.fillStyle='rgba(148,163,184,.78)';ctx.font=(10*d)+'px sans-serif';ctx.textAlign='left';ctx.textBaseline='middle';for(var gi=0;gi<=4;gi++){var gv=low+(high-low)*gi/4,gy=py(gv);ctx.beginPath();ctx.moveTo(padL,gy);ctx.lineTo(xMax,gy);ctx.stroke();if(gi>0&&gi<4)ctx.fillText(gv.toFixed(1),xMax+7*d,gy);}ctx.restore();ctx.beginPath();points.forEach(function(p,i){var x=px(p),y=py(p.max);if(i)ctx.lineTo(x,y);else ctx.moveTo(x,y);});for(var i=points.length-1;i>=0;i--)ctx.lineTo(px(points[i]),py(points[i].min));ctx.closePath();ctx.fillStyle='rgba('+spec.rgb+',.20)';ctx.fill();ctx.lineWidth=Math.max(1,d);ctx.strokeStyle='rgba('+spec.rgb+',.50)';['max','min'].forEach(function(k){ctx.beginPath();points.forEach(function(p,i){if(i)ctx.lineTo(px(p),py(p[k]));else ctx.moveTo(px(p),py(p[k]));});ctx.stroke();});ctx.beginPath();points.forEach(function(p,i){if(i)ctx.lineTo(px(p),py(p.mean));else ctx.moveTo(px(p),py(p.mean));});ctx.lineWidth=Math.max(2,2*d);ctx.strokeStyle='rgba('+spec.rgb+',1)';ctx.stroke();ctx.font=(11*d)+'px sans-serif';ctx.fillStyle='rgba(226,232,240,.95)';ctx.textAlign='left';ctx.textBaseline='middle';ctx.fillText('Màx '+high.toFixed(1),xMax+7*d,Math.max(13*d,py(high)+11*d));ctx.fillText('Mín '+low.toFixed(1),xMax+7*d,Math.min(h-8*d,py(low)-8*d));if(note)note.textContent=spec.label;if(c.dataset.trend==='water')updateTempTrend(points.map(function(p){return{t:p.t,v:p.mean};}));}";
  html += "async function loadStatisticsChart(c,key){if(!c||!c.dataset.entity)return;var spec=historyRangeSpec(key||c.dataset.range||'48h'),end=new Date();c.dataset.range=spec.key;var note=document.getElementById(c.id+'-note');if(note)note.textContent='Carregant '+spec.label+'...';document.querySelectorAll('.history-range').forEach(function(b){if(b.dataset.target===c.id){b.classList.toggle('active',b.dataset.range===spec.key);b.style.setProperty('--range-color',spec.hex);}});try{var url='/ha-statistics?entity='+encodeURIComponent(c.dataset.entity)+'&period='+spec.period+'&start='+encodeURIComponent(new Date(spec.start).toISOString())+'&end='+encodeURIComponent(end.toISOString());var response=await fetch(url,{cache:'no-store'}),raw=await response.json();if(raw.error)throw new Error(raw.error);var points=extractHaStatistics(raw,c.dataset.entity);if(points.length<2)throw new Error('Home Assistant encara no té estadístiques per aquesta entitat');drawStatisticsChart(c,points,spec);}catch(error){if(c.dataset.trend==='water'&&spec.key==='48h'){try{var fallback=await fetch('/ha-history?hours=48&start='+encodeURIComponent(new Date(spec.start).toISOString())+'&end='+encodeURIComponent(end.toISOString())),legacy=await fallback.json();if(!legacy.error){var points=extractHaPoints(legacy).map(function(p){return{t:p.t,mean:p.v,min:p.v,max:p.v};});drawStatisticsChart(c,points,spec);return;}}catch(ignore){}}if(note)note.textContent=error.message||'No puc llegir estadístiques HA';}}";
  html += "function bindHistoryCharts(){document.querySelectorAll('.history-range').forEach(function(b){b.addEventListener('click',function(){loadStatisticsChart(document.getElementById(b.dataset.target),b.dataset.range);});});document.querySelectorAll('canvas.statistics-chart').forEach(function(c){if(c.offsetParent!==null)loadStatisticsChart(c,c.dataset.range||'48h');});}";
  html += "function setOtaModal(active){document.body.classList.toggle('ota-locked',!!active);}function showOtaProgress(source,msg){var card=document.getElementById('ota-progress-card');if(!card)return;sessionStorage.setItem('boiaOtaPending','1');card.classList.remove('hidden','done','error');setOtaModal(true);txt('ota-progress-phase',source+' · iniciant');txt('ota-progress-message',msg||'Preparant actualitzacio');txt('ota-progress-percent','0%');txt('ota-progress-bytes','-- / --');var fill=document.getElementById('ota-progress-fill');if(fill){fill.classList.remove('indeterminate');fill.style.width='0%';}}function dismissOtaProgress(){sessionStorage.removeItem('boiaOtaPending');var card=document.getElementById('ota-progress-card');if(card)card.classList.add('hidden');setOtaModal(false);}";
  html += "function otaUiError(message){sessionStorage.removeItem('boiaOtaPending');txt('ota-progress-message',message);txt('ota-progress-phase','OTA · error');var card=document.getElementById('ota-progress-card');if(card)card.classList.add('error');var fill=document.getElementById('ota-progress-fill');if(fill)fill.classList.remove('indeterminate');}";
  html += "function sendFirmwareBlock(buffer,status,offset,end){return new Promise(function(resolve,reject){var local=document.getElementById('ota-local-form');if(!local){reject(new Error('Ruta de pujada OTA no disponible'));return;}var xhr=new XMLHttpRequest();xhr.open('POST',local.action);var sha=status.github_firmware_sha256||status.sha256||'';var size=status.github_firmware_size||status.size||buffer.byteLength;if(sha)xhr.setRequestHeader('X-Firmware-SHA256',sha);xhr.setRequestHeader('X-Firmware-Size',String(size));xhr.setRequestHeader('X-Firmware-Offset',String(offset));xhr.upload.onprogress=function(ev){if(!ev.lengthComputable)return;var sent=offset+ev.loaded;var pct=Math.round(sent*100/buffer.byteLength);var fill=document.getElementById('ota-progress-fill');if(fill)fill.style.width=pct+'%';txt('ota-progress-percent',pct+'%');txt('ota-progress-bytes',bytesHuman(sent)+' / '+bytesHuman(buffer.byteLength));};xhr.onload=function(){if(xhr.status>=200&&xhr.status<300)resolve();else reject(new Error('La boia ha rebutjat el bloc. HTTP '+xhr.status));};xhr.onerror=function(){reject(new Error('Connexio local tallada durant un bloc'));};var data=new FormData();data.append('update',new Blob([buffer.slice(offset,end)],{type:'application/octet-stream'}),'firmware.bin');xhr.send(data);});}";
  html += "async function uploadFirmwareBlocks(buffer,status,label){var fill=document.getElementById('ota-progress-fill');if(fill)fill.classList.remove('indeterminate');var offset=0,retries=0,blockSize=65536;while(offset<buffer.byteLength){var end=Math.min(offset+blockSize,buffer.byteLength);try{await sendFirmwareBlock(buffer,status,offset,end);offset=end;retries=0;}catch(error){retries++;try{var check=await fetch('/status',{cache:'no-store'});var current=await check.json();var confirmed=parseInt(current.ota_progress_bytes||0,10);if(confirmed>offset&&confirmed<=buffer.byteLength){offset=confirmed;retries=0;}}catch(ignore){}if(retries>=3)throw error;await new Promise(function(resolve){setTimeout(resolve,500);});}}sessionStorage.removeItem('boiaOtaPending');txt('ota-progress-message','Firmware verificat i instal·lat. La boia es reiniciara.');txt('ota-progress-phase',label+' · completada');var card=document.getElementById('ota-progress-card');if(card)card.classList.add('done');}";
  html += "async function uploadGithubFirmware(buffer,status){txt('ota-progress-phase','GitHub OTA · pujant a la boia');txt('ota-progress-message','Firmware descarregat. Pujant en blocs recuperables de 64 KiB...');await uploadFirmwareBlocks(buffer,status,'GitHub OTA');}";
  html += "async function installGithubViaBrowser(){showOtaProgress('GitHub OTA','Llegint el manifest publicat');var fill=document.getElementById('ota-progress-fill');if(fill)fill.classList.add('indeterminate');try{var check=await fetch('/github-check-update-run',{cache:'no-store'});if(!check.ok)throw new Error('No puc comprovar GitHub. HTTP '+check.status);var status=await check.json();if(!status.github_firmware_url||!status.github_firmware_sha256||!status.github_firmware_size)throw new Error('El manifest no porta URL, SHA-256 o mida valida');txt('ota-progress-phase','GitHub OTA · descarregant al navegador');txt('ota-progress-message','Descarregant '+bytesHuman(status.github_firmware_size)+' des de GitHub...');var separator=status.github_firmware_url.indexOf('?')>=0?'&':'?';var response=await fetch(status.github_firmware_url+separator+'build='+encodeURIComponent(status.github_update_sha||Date.now()),{cache:'no-store'});if(!response.ok)throw new Error('GitHub ha respost HTTP '+response.status);var buffer=await response.arrayBuffer();if(buffer.byteLength!==parseInt(status.github_firmware_size,10))throw new Error('Mida incorrecta: '+buffer.byteLength+' bytes');await uploadGithubFirmware(buffer,status);}catch(error){otaUiError(error.message||'Error OTA GitHub');}}";
  html += "function bindOtaForms(){var local=document.getElementById('ota-local-form');if(local){local.addEventListener('submit',async function(e){e.preventDefault();if(local.getAttribute('data-confirm')&&!confirm(local.getAttribute('data-confirm')))return;var input=document.getElementById('ota-local-file');if(!input||!input.files||!input.files.length){alert('Tria primer un firmware.bin');return;}showOtaProgress('OTA local','Llegint el fitxer i preparant blocs de 64 KiB');try{var buffer=await input.files[0].arrayBuffer();txt('ota-progress-phase','OTA local · pujant a la boia');await uploadFirmwareBlocks(buffer,{size:buffer.byteLength},'OTA local');}catch(error){otaUiError(error.message||'Error pujant firmware local');}});}var gh=document.getElementById('github-install-form');if(gh){gh.addEventListener('submit',function(e){e.preventDefault();if(gh.getAttribute('data-confirm')&&!confirm(gh.getAttribute('data-confirm')))return;installGithubViaBrowser();});}}";
  html += "window.addEventListener('load',function(){bindConfirms();bindAccordion();applySubpage();startWS();bindHistoryCharts();bindSdExplorer();bindOtaForms();setTimeout(runOtaAutoChecks,600);});";
  html += "</script>";

  html += "</head>";
  html += "<body>";
  html += "<div class='container'>";
}


static void appendServicePill(String& html, const String& id, const String& icon, const String& label, const String& stateClass, const String& text) {
  html += "<span id='";
  html += id;
  html += "' class='svc ";
  html += stateClass;
  html += "'><span>";
  html += icon;
  html += "</span><span>";
  html += htmlEscape(label);
  html += ": </span><span>";
  html += htmlEscape(text);
  html += "</span></span>";
}

static void appendTabs(String& html, const String& active) {
  String section = server.arg("section");
  html += "<div class='menu-title'>MENU</div>";
  html += "<div class='tabs'>";

  html += "<div class='menu-group'>";
  html += "<a class='tab "; html += active == "status" ? "active" : ""; html += "' href='/'>📊 Estat</a>";
  html += "</div>";

  html += "<div class='menu-group has-sub "; html += active == "storage" ? "open" : ""; html += "'>";
  html += "<button class='tab menu-toggle "; html += active == "storage" ? "active" : ""; html += "' type='button'>💾 SD / Històric <span class='chev'>▶</span></button>";
  html += "<div class='subnav'>";
  html += "<a class='"; html += (active == "storage" && (section == "" || section == "sd-summary")) ? "active" : ""; html += "' href='/storage?section=sd-summary'>Estat</a>";
  html += "<a class='"; html += (active == "storage" && section == "sd-stats") ? "active" : ""; html += "' href='/storage?section=sd-stats'>Dades</a>";
  html += "<a class='"; html += (active == "storage" && section == "sd-map") ? "active" : ""; html += "' href='/storage?section=sd-map'>Mapa fitxers</a>";
  html += "<a class='"; html += (active == "storage" && section == "sd-last") ? "active" : ""; html += "' href='/storage?section=sd-last'>Últim registre</a>";
  html += "<a class='"; html += (active == "storage" && section == "sd-browser") ? "active" : ""; html += "' href='/storage?section=sd-browser'>Explorador</a>";
  html += "<a class='"; html += (active == "storage" && section == "sd-maintenance") ? "active" : ""; html += "' href='/storage?section=sd-maintenance'>Manteniment</a>";
  html += "</div></div>";

  html += "<div class='menu-group has-sub "; html += active == "config" ? "open" : ""; html += "'>";
  html += "<button class='tab menu-toggle "; html += active == "config" ? "active" : ""; html += "' type='button'>🌡️ Temperatura <span class='chev'>▶</span></button>";
  html += "<div class='subnav'>";
  html += "<a class='"; html += (active == "config" && (section == "" || section == "temp-reading")) ? "active" : ""; html += "' href='/config?section=temp-reading'>Lectura</a>";
  html += "<a class='"; html += (active == "config" && section == "temp-calibration") ? "active" : ""; html += "' href='/config?section=temp-calibration'>Calibratge</a>";
  html += "<a class='"; html += (active == "config" && section == "temp-reset") ? "active" : ""; html += "' href='/config?section=temp-reset'>Reset</a>";
  html += "</div></div>";

  html += "<div class='menu-group has-sub "; html += active == "wifi" ? "open" : ""; html += "'>";
  html += "<button class='tab menu-toggle "; html += active == "wifi" ? "active" : ""; html += "' type='button'>📶 Wi-Fi <span class='chev'>▶</span></button>";
  html += "<div class='subnav'>";
  html += "<a class='"; html += (active == "wifi" && (section == "" || section == "wifi-status")) ? "active" : ""; html += "' href='/wifi?section=wifi-status'>Estat</a>";
  html += "<a class='"; html += (active == "wifi" && section == "wifi-credentials") ? "active" : ""; html += "' href='/wifi?section=wifi-credentials'>Credencials</a>";
  html += "<a class='"; html += (active == "wifi" && section == "wifi-network") ? "active" : ""; html += "' href='/wifi?section=wifi-network'>Xarxa avançada</a>";
  html += "<a class='"; html += (active == "wifi" && section == "wifi-recovery") ? "active" : ""; html += "' href='/wifi?section=wifi-recovery'>Rescat</a>";
  html += "</div></div>";

  html += "<div class='menu-group has-sub "; html += active == "mqtt" ? "open" : ""; html += "'>";
  html += "<button class='tab menu-toggle "; html += active == "mqtt" ? "active" : ""; html += "' type='button'>📡 MQTT / HA <span class='chev'>▶</span></button>";
  html += "<div class='subnav'>";
  html += "<a class='"; html += (active == "mqtt" && (section == "" || section == "mqtt-status")) ? "active" : ""; html += "' href='/mqtt?section=mqtt-status'>Estat</a>";
  html += "<a class='"; html += (active == "mqtt" && section == "mqtt-broker") ? "active" : ""; html += "' href='/mqtt?section=mqtt-broker'>Broker</a>";
  html += "<a class='"; html += (active == "mqtt" && section == "mqtt-ha") ? "active" : ""; html += "' href='/mqtt?section=mqtt-ha'>Home Assistant</a>";
  html += "<a class='"; html += (active == "mqtt" && section == "mqtt-actions") ? "active" : ""; html += "' href='/mqtt?section=mqtt-actions'>Accions</a>";
  html += "</div></div>";

  html += "<div class='menu-group has-sub "; html += active == "system" ? "open" : ""; html += "'>";
  html += "<button class='tab menu-toggle "; html += active == "system" ? "active" : ""; html += "' type='button'>⚙️ Sistema <span class='chev'>▶</span></button>";
  html += "<div class='subnav'>";
  html += "<a class='"; html += (active == "system" && (section == "" || section == "sys-summary")) ? "active" : ""; html += "' href='/system?section=sys-summary'>Resum</a>";
  html += "<a class='"; html += (active == "system" && section == "sys-internal-env") ? "active" : ""; html += "' href='/system?section=sys-internal-env'>Interior boia / alarmes</a>";
  html += "<a class='"; html += (active == "system" && section == "sys-battery") ? "active" : ""; html += "' href='/system?section=sys-battery'>Bateria</a>";
  html += "<a class='"; html += (active == "system" && section == "sys-identity") ? "active" : ""; html += "' href='/system?section=sys-identity'>Identitat</a>";
  html += "<a class='"; html += (active == "system" && section == "sys-mode") ? "active" : ""; html += "' href='/system?section=sys-mode'>Mode</a>";
  html += "<a class='"; html += (active == "system" && section == "sys-leds") ? "active" : ""; html += "' href='/system?section=sys-leds'>LEDs</a>";
  html += "<a class='"; html += (active == "system" && section == "sys-users") ? "active" : ""; html += "' href='/system?section=sys-users'>Usuaris</a>";
  html += "</div></div>";

  html += "<div class='menu-group has-sub "; html += active == "maintenance" ? "open" : ""; html += "'>";
  html += "<button class='tab menu-toggle "; html += active == "maintenance" ? "active" : ""; html += "' type='button'>🛠️ Manteniment <span class='chev'>▶</span></button>";
  html += "<div class='subnav'>";
  html += "<a class='"; html += (active == "maintenance" && (section == "" || section == "mnt-health")) ? "active" : ""; html += "' href='/maintenance?section=mnt-health'>Salut</a>";
  html += "<a class='"; html += (active == "maintenance" && section == "mnt-diagnostics") ? "active" : ""; html += "' href='/maintenance?section=mnt-diagnostics'>Diagnòstic</a>";
  html += "<a class='"; html += (active == "maintenance" && section == "mnt-actions") ? "active" : ""; html += "' href='/maintenance?section=mnt-actions'>Accions</a>";
  html += "<a class='"; html += (active == "maintenance" && section == "mnt-ota") ? "active" : ""; html += "' href='/maintenance?section=mnt-ota'>OTA</a>";
  html += "<a class='"; html += (active == "maintenance" && section == "mnt-backup") ? "active" : ""; html += "' href='/maintenance?section=mnt-backup'>Backup</a>";
  html += "<a class='"; html += (active == "maintenance" && section == "mnt-recovery") ? "active" : ""; html += "' href='/maintenance?section=mnt-recovery'>Rescat</a>";
  html += "<a class='"; html += (active == "maintenance" && section == "mnt-danger") ? "active" : ""; html += "' href='/maintenance?section=mnt-danger'>Destructives</a>";
  html += "</div></div>";

  html += "<div class='menu-group has-sub "; html += active == "help" ? "open" : ""; html += "'>";
  html += "<button class='tab menu-toggle "; html += active == "help" ? "active" : ""; html += "' type='button'>❔ Centre d'ajuda <span class='chev'>▶</span></button>";
  html += "<div class='subnav'>";
  html += "<a class='"; html += (active == "help" && (section == "" || section == "help-firmware")) ? "active" : ""; html += "' href='/help?section=help-firmware'>Firmware</a>";
  html += "<a class='"; html += (active == "help" && section == "help-hardware") ? "active" : ""; html += "' href='/help?section=help-hardware'>Hardware / GPIO</a>";
  html += "<a class='"; html += (active == "help" && section == "help-future") ? "active" : ""; html += "' href='/help?section=help-future'>Ampliacions futures</a>";
  html += "<a class='"; html += (active == "help" && section == "help-recovery") ? "active" : ""; html += "' href='/help?section=help-recovery'>Rescat</a>";
  html += "</div></div>";

  html += "<div class='menu-group'>";
  html += "<a class='tab' href='/status'>{} JSON</a>";
  html += "</div>";
  html += "<div class='menu-group'>";
  html += "<a class='tab' href='/logout'>Tancar sessió</a>";
  html += "</div>";
  html += "</div>";

}


static void appendSubTabs(String& html, const String& title, const char* const labels[], const char* const anchors[], size_t count) {
  String selected = server.arg("section");
  if (selected.length() == 0 && count > 0) selected = String(anchors[0]);
  html += "<div class='subtabs'><span class='subtabs-title'>";
  html += htmlEscape(title);
  html += "</span>";
  for (size_t i = 0; i < count; i++) {
    html += "<a class='subtab ";
    html += selected == String(anchors[i]) ? "active" : "";
    html += "' href='";
    html += server.uri();
    html += "?section=";
    html += anchors[i];
    html += "'>";
    html += labels[i];
    html += "</a>";
  }
  html += "</div>";
}

static void appendPageStart(String& html, const String& active, bool autoRefresh) {
  appendHtmlHeader(html, configDeviceName, autoRefresh);

  html += "<div class='topbar'>";
  html += "<div class='brandrow'>";
  html += "<div class='brand'><h1>";
  html += htmlEscape(configDeviceName);
  html += "</h1><div class='sub'>Hostname: <b id='live-hostname'>";
  html += htmlEscape(configDeviceHostname);
  html += "</b> · IP: <b id='live-ip'>";
  html += htmlEscape(wifiStaIpText());
  html += "</b></div></div>";
  html += "<div class='header-right'>";
  html += "<div class='servicebar'>";
  appendServicePill(html, "svc-wifi", "📶", "Wi-Fi", isWifiConnected() ? "ok" : "bad", wifiStatusText());
  appendServicePill(html, "svc-ap", "🛟", "AP", isWifiApActive() ? "warn" : "ok", isWifiApActive() ? "Actiu" : "Inactiu");
  appendServicePill(html, "svc-mqtt", "📡", "MQTT", configMqttEnabled ? statusClass(isMqttConnected()) : "warn", mqttStatusText());
  appendServicePill(html, "svc-ha", "🏠", "HA", (configHaDiscoveryEnabled && appState.mqttDiscoveryPublished) ? "ok" : "warn", configHaDiscoveryEnabled ? (appState.mqttDiscoveryPublished ? "OK" : "Pendent") : "Off");
  appendServicePill(html, "svc-sensor", "🌡️", "Sonda", appState.sensorStatus == "OK" ? "ok" : (appState.sensorStatus == "ERROR" ? "bad" : "warn"), appState.sensorStatus);
  appendServicePill(html, "svc-sd", "💾", "SD", isSdMounted() ? "ok" : (isSdEnabled() ? "bad" : "warn"), isSdEnabled() ? (isSdMounted() ? "OK" : "Error") : "Off");
  appendServicePill(html, "svc-ota", "⬆️", "OTA", appState.otaInProgress ? "warn" : "ok", appState.otaInProgress ? "En curs" : "Disponible");
  html += "</div>";
  html += "<div class='header-actions'>";
  html += "<form method='POST' action='/restart' data-confirm='Segur que vols reiniciar la boia?'><button class='header-btn danger' type='submit'>⏻ Reiniciar boia</button></form>";
  html += "<a class='header-btn secondary' href='/logout'>Sortir / tancar sessió</a>";
  html += "</div></div></div>";
  html += "</div>";

  html += "<div class='app-shell'>";
  html += "<aside class='sidebar'>";
  appendTabs(html, active);
  html += "</aside>";
  html += "<main class='main'>";
}

static void appendPageEnd(String& html) {
  html += "<div class='footer'>";
  html += htmlEscape(configDeviceName);
  html += " · firmware ";
  html += FIRMWARE_VERSION;
  html += " · ESP32-C6 · WebSocket live :81";
  html += "</div>";
  html += "</main>";
  html += "</div>";
  html += "</div>";
  html += "</body>";
  html += "</html>";
}


static String statusClass(bool ok) {
  return ok ? "ok" : "bad";
}

static String enabledText(bool enabled) {
  return enabled ? "Activat" : "Desactivat";
}

static String floatText(float value, uint8_t decimals) {
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%.*f", (int)decimals, value);
  return String(buffer);
}

static void appendStatisticsHistoryPanel(String& html, const String& id, const String& title, const String& entityId) {
  html += "<div class='item history-panel'><h3>" + htmlEscape(title) + "</h3>";
  html += "<canvas id='" + id + "' class='statistics-chart' data-entity='" + htmlEscape(entityId) + "' data-range='48h' aria-hidden='true'></canvas>";
  html += "<div class='history-toolbar'>";
  html += "<button class='history-range active' type='button' data-target='" + id + "' data-range='48h'>48 h</button>";
  html += "<button class='history-range' type='button' data-target='" + id + "' data-range='31d'>31 d</button>";
  html += "<button class='history-range' type='button' data-target='" + id + "' data-range='6m'>6 mesos</button>";
  html += "<button class='history-range' type='button' data-target='" + id + "' data-range='1y'>1 any</button></div>";
  html += "<div class='chart-legend'><span class='legend-item'><i class='legend-line'></i>Mitjana</span><span class='legend-item'><i class='legend-band'></i>Mínim–màxim</span></div>";
  html += "<div id='" + id + "-note' class='chart-note'>Carregant estadístiques HA...</div></div>";
}

// ==========================
// PAGINES
// ==========================

static String buildStatusPage() {
  String tempText = formatTemperature(appState.lastValidTemperatureC, configTemperatureDecimals);
  String html = "";

  appendPageStart(html, "status", true);

  html += "<div class='card temp-card'>";
  html += "<canvas id='temp-history-chart' class='statistics-chart' data-entity='";
  html += htmlEscape(configHaHistoryEntityId);
  html += "' data-range='48h' data-trend='water' aria-hidden='true'></canvas>";
  html += "<div class='temp-content'>";
  html += "<h2>Temperatura actual</h2>";
  html += "<div class='temp'><span id='live-temp'>";
  html += tempText;

  html += "</span>";

  if (!isnan(appState.lastValidTemperatureC)) {
    html += " <span class='unit'>&deg;C</span>";
  }

  html += "<span id='temp-trend' class='temp-trend pending' aria-live='polite'>&rarr;</span>";

  html += "</div>";

  if (appState.failedReads == 0 && appState.validReads > 0) {
    html += "<div class='ok'>Estat: lectures correctes</div>";
  } else if (appState.validReads == 0) {
    html += "<div class='warn'>Estat: esperant lectura valida</div>";
  } else {
    html += "<div class='warn'>Estat: hi ha hagut alguna lectura fallida</div>";
  }

  html += "<div class='internal-mini'><span>Interior:</span><b id='live-internal-temp'>";
  html += isnan(appState.lastInternalTemperatureC) ? "Sense dades" : formatTemperature(appState.lastInternalTemperatureC, 2) + " °C";
  html += "</b><span class='separator'>·</span><b id='live-internal-humidity'>";
  html += isnan(appState.lastInternalHumidityPercent) ? "Sense dades" : formatTemperature(appState.lastInternalHumidityPercent, 1) + " %";
  html += "</b><span>HR</span></div>";
  html += "</div>";
  html += "<div class='temp-chart-footer'>";
  html += "<div class='temp-metric-row'>";
  html += "<div class='item temp-battery-card'><div class='label'>🔋</div><div class='value' id='live-battery'>";
  html += batteryPercentText();
  html += "</div><div class='small'><span id='live-battery-voltage'>";
  html += batteryVoltageText();
  html += "</span></div></div>";
  html += "<div class='item temp-uptime-card'><div class='label'>⏱</div><div class='value' id='live-uptime'>";
  html += uptimeText(getUptimeSeconds());
  html += "</div></div>";
  html += "</div>";
  html += "<div class='history-toolbar'>";
  html += "<button class='history-range active' type='button' data-target='temp-history-chart' data-range='48h'>48 h</button>";
  html += "<button class='history-range' type='button' data-target='temp-history-chart' data-range='31d'>31 d</button>";
  html += "<button class='history-range' type='button' data-target='temp-history-chart' data-range='6m'>6 mesos</button>";
  html += "<button class='history-range' type='button' data-target='temp-history-chart' data-range='1y'>1 any</button></div>";
  html += "<div class='chart-footer-right'><div class='chart-legend'><span class='legend-item'><i class='legend-line'></i>Mitjana</span><span class='legend-item'><i class='legend-band'></i>Mínim–màxim</span></div>";
  html += "<div id='temp-history-chart-note' class='chart-note'>Carregant estadístiques HA...</div></div></div></div>";

  appendPageEnd(html);
  return html;
}


static String buildStoragePage() {
  refreshSdInfo();
  String browsePath = server.arg("path");
  if (browsePath.length() == 0) browsePath = SD_BASE_DIR;

  String html = "";
  appendPageStart(html, "storage", false);

  static const char* sdLabels[] = {"Estat", "Dades", "Mapa", "Últim registre", "Explorador", "Manteniment"};
  static const char* sdAnchors[] = {"sd-summary", "sd-stats", "sd-map", "sd-last", "sd-browser", "sd-maintenance"};
  appendSubTabs(html, "SD", sdLabels, sdAnchors, 6);

  html += "<div id='sd-summary' class='card'>";
  html += "<h2>microSD / Històric local / Buffer HA</h2>";
  html += "<p class='hint'>Aquesta pàgina converteix la microSD en caixa negra local: històric per dies, estadístiques precalculades, logs, snapshot de configuració, blackbox d'arrencada, buffer MQTT offline i explorador de fitxers. Si no hi ha SD, la boia continua funcionant igual, però perd aquesta capa local.</p>";

  html += "<div class='grid3'>";
  html += "<div class='item'><div class='label'>Estat SD</div><div class='value ";
  html += isSdMounted() ? "ok" : "bad";
  html += "'>" + htmlEscape(sdStatusText()) + "</div><div class='small'>" + htmlEscape(sdLastErrorText()) + "</div></div>";
  html += "<div class='item'><div class='label'>Tipus targeta</div><div class='value'>" + htmlEscape(sdCardTypeText()) + "</div><div class='small'>SPI " + String(SD_SPI_FREQUENCY_HZ / 1000000UL) + " MHz</div></div>";
  html += "<div class='item'><div class='label'>Fitxer històric actual</div><div class='value' style='font-size:15px'>" + htmlEscape(sdHistoryPathText()) + "</div><div class='small'>Un CSV per dia</div></div>";
  html += "<div class='item'><div class='label'>Capacitat total</div><div class='value'>" + htmlEscape(sdTotalText()) + "</div></div>";
  html += "<div class='item'><div class='label'>Espai ocupat</div><div class='value'>" + htmlEscape(sdUsedText()) + "</div><div class='small'>" + htmlEscape(sdUsedPercentText()) + "</div></div>";
  html += "<div class='item'><div class='label'>Espai lliure</div><div class='value'>" + htmlEscape(sdFreeText()) + "</div></div>";
  html += "<div class='item'><div class='label'>Registres escrits</div><div class='value'>" + String((unsigned long)appState.sdHistoryWriteCount) + "</div><div class='small'>Errors: " + String((unsigned long)appState.sdHistoryWriteFailCount) + "</div></div>";
  html += "<div class='item'><div class='label'>Buffer MQTT pendent</div><div class='value ";
  html += appState.sdMqttPendingCount == 0 ? "ok" : "warn";
  html += "'>" + String((unsigned long)appState.sdMqttPendingCount) + "</div><div class='small'>Enviats des de buffer: " + String((unsigned long)appState.sdMqttFlushCount) + "</div></div>";
  html += "<div class='item'><div class='label'>Última escriptura</div><div class='value'>";
  html += appState.sdLastWriteMillis == 0 ? "Mai" : elapsedText(appState.sdLastWriteMillis);
  html += "</div></div>";
  html += "</div>";

  html += "<h3>Cablejat configurat al firmware</h3>";
  html += "<div class='grid'>";
  html += "<div class='item'><div class='label'>CS</div><div class='value'>GPIO" + String(SD_SPI_CS_PIN) + "</div></div>";
  html += "<div class='item'><div class='label'>MOSI</div><div class='value'>GPIO" + String(SD_SPI_MOSI_PIN) + "</div></div>";
  html += "<div class='item'><div class='label'>CLK / SCK</div><div class='value'>GPIO" + String(SD_SPI_CLK_PIN) + "</div></div>";
  html += "<div class='item'><div class='label'>MISO</div><div class='value'>GPIO" + String(SD_SPI_MISO_PIN) + "</div></div>";
  html += "</div>";

  html += "<div class='actions' style='margin-top:16px'>";
  html += "<a class='btn secondary' href='/storage?section=sd-summary'>Recarregar estat</a>";
  html += "<a class='btn secondary' href='/sd-info'>JSON SD</a>";
  if (isSdMounted()) {
    html += "<a class='btn secondary' href='/sd-history.csv'>CSV d'avui</a>";
    html += "<a class='btn secondary' href='/sd-daily-stats.csv'>Estadístiques</a>";
    html += "<a class='btn secondary' href='/storage?section=sd-browser'>Anar a l'explorador</a>";
  }
  html += "</div>";
  html += "</div>";

  html += "<div id='sd-stats' class='card'>";
  html += "<h2>Dades i estadística precalculada del dia</h2>";
  html += "<p class='hint'>Aquesta secció mostra el resum que la boia manté a la SD per no haver de rellegir tot el detall cada vegada.</p>";
  html += "<div class='grid3'>";
  html += "<div class='item'><div class='label'>Dia</div><div class='value'>" + htmlEscape(appState.sdStatsDay) + "</div><div class='small'>Registres: " + String((unsigned long)appState.sdDailyRecordCount) + "</div></div>";
  html += "<div class='item'><div class='label'>Temperatura min / mitjana / max</div><div class='value'>";
  html += isnan(appState.sdDailyTempMin) ? "Sense dades" : formatTemperature(appState.sdDailyTempMin, 2) + " / " + formatTemperature(appState.sdDailyTempAvg, 2) + " / " + formatTemperature(appState.sdDailyTempMax, 2) + " °C";
  html += "</div></div>";
  html += "<div class='item'><div class='label'>Bateria min / mitjana / max</div><div class='value'>";
  html += isnan(appState.sdDailyBatteryMin) ? "Sense dades" : formatTemperature(appState.sdDailyBatteryMin, 3) + " / " + formatTemperature(appState.sdDailyBatteryAvg, 3) + " / " + formatTemperature(appState.sdDailyBatteryMax, 3) + " V";
  html += "</div><div class='small'>Errors del dia: " + String((unsigned long)appState.sdDailyErrorCount) + "</div></div>";
  html += "</div>";
  html += "<div class='actions' style='margin-top:16px'>";
  if (isSdMounted()) {
    html += "<a class='btn secondary' href='/sd-history.csv'>Descarregar detall d'avui</a>";
    html += "<a class='btn secondary' href='/sd-daily-stats.csv'>Descarregar estadístiques</a>";
    html += "<a class='btn secondary' href='/sd-pending-mqtt.jsonl'>Buffer MQTT pendent</a>";
  }
  html += "</div>";
  html += "</div>";

  html += "<div id='sd-map' class='card'>";
  html += "<h2>Mapa de fitxers locals</h2>";
  html += "<div class='grid'>";
  html += "<div class='item'><div class='label'>Històric detall</div><div class='value' style='font-size:15px'>/boia/history/YYYY-MM-DD.csv</div><div class='small'>Una fila per lectura real.</div></div>";
  html += "<div class='item'><div class='label'>Precalculat</div><div class='value' style='font-size:15px'>" + htmlEscape(sdDailyStatsPathText()) + "</div><div class='small'>Snapshots min/max/mitjana del dia.</div></div>";
  html += "<div class='item'><div class='label'>Logs</div><div class='value' style='font-size:15px'>/boia/logs/YYYY-MM-DD.log</div><div class='small'>BOOT, SD, MQTT i errors importants.</div></div>";
  html += "<div class='item'><div class='label'>Buffer HA/MQTT</div><div class='value' style='font-size:15px'>" + htmlEscape(sdPendingMqttPathText()) + "</div><div class='small'>JSONL pendent d'enviar quan torni MQTT.</div></div>";
  html += "<div class='item'><div class='label'>Config</div><div class='value' style='font-size:15px'>/boia/config/config_snapshot.json</div><div class='small'>Còpia llegible de la configuració activa.</div></div>";
  html += "<div class='item'><div class='label'>Blackbox</div><div class='value' style='font-size:15px'>/boia/blackbox/last_boot.json</div><div class='small'>Dades d'arrencada i reset.</div></div>";
  html += "</div>";
  html += "</div>";

  html += "<div id='sd-last' class='card'>";
  html += "<h2>Últim registre guardat</h2>";
  if (appState.sdLastHistoryLine.length() == 0) {
    html += "<p class='hint'>Encara no s'ha escrit cap registre en aquesta arrencada. Quan es faci la pròxima lectura, la boia intentarà afegir una línia al CSV diari.</p>";
  } else {
    html += "<pre>" + htmlEscape(appState.sdLastHistoryLine) + "</pre>";
  }
  html += "<p class='small'>Columnes: unix_time, iso_time, uptime_seconds, water_temperature_c, raw_temperature_c, water_sensor_status, internal_temperature_c, internal_humidity_percent, internal_env_status, battery_voltage_v, battery_percent, battery_status, wifi_rssi_dbm.</p>";
  html += "</div>";

  html += "<div id='sd-browser' class='card'>";
  html += "<h2>Explorador de fitxers</h2>";
  html += "<p class='hint'>Explorador sense canvi de pàgina: doble clic a una carpeta per entrar-hi, doble clic a un fitxer per veure'l en una finestra modal. La descàrrega continua disponible per CSV, JSON, JSONL i logs.</p>";
  if (!isSdMounted()) {
    html += "<p class='hint bad'>SD no muntada. L'explorador s'activarà quan la targeta funcioni.</p>";
  } else {
    String cleanBrowse;
    if (!normalizeSdPath(browsePath, cleanBrowse)) cleanBrowse = SD_BASE_DIR;
    html += "<div id='sd-explorer' class='sd-explorer' data-start-path='" + htmlEscape(cleanBrowse) + "'>";
    html += "<div class='sd-toolbar'>";
    html += "<button id='sd-up-button' class='secondary' type='button'>⬆ Pujar</button>";
    html += "<button id='sd-refresh-button' class='secondary' type='button'>🔄 Actualitzar</button>";
    html += "<input id='sd-address' class='sd-address' type='text' value='" + htmlEscape(cleanBrowse) + "' spellcheck='false'>";
    html += "<button id='sd-open-address' class='secondary' type='button'>Obrir ruta</button>";
    html += "<button id='sd-download-selected' class='secondary' type='button' disabled>Descarregar seleccionat</button>";
    html += "</div>";
    html += "<div id='sd-explorer-status' class='sd-status'>Preparant explorador...</div>";
    html += "<div class='sd-body'>";
    html += "<div class='sd-tree'>";
    html += "<button type='button' data-sd-path='/boia'>🏠 /boia</button>";
    html += "<button type='button' data-sd-path='/boia/history'>📊 Històric</button>";
    html += "<button type='button' data-sd-path='/boia/stats'>📈 Stats</button>";
    html += "<button type='button' data-sd-path='/boia/logs'>📄 Logs</button>";
    html += "<button type='button' data-sd-path='/boia/mqtt'>📡 Buffer MQTT</button>";
    html += "<button type='button' data-sd-path='/boia/config'>⚙️ Config</button>";
    html += "<button type='button' data-sd-path='/boia/blackbox'>🧰 Blackbox</button>";
    html += "<button type='button' data-sd-path='/boia/system'>ℹ️ Sistema</button>";
    html += "</div>";
    html += "<div class='sd-pane'>";
    html += "<div class='sd-list-head'><div></div><div>Nom</div><div>Mida</div><div class='sd-col-type'>Tipus</div></div>";
    html += "<div id='sd-file-list'><div class='sd-empty'>Carregant directori...</div></div>";
    html += "</div>";
    html += "</div>";
    html += "</div>";
    html += "<div id='sd-file-modal' class='sd-modal hidden'>";
    html += "<div class='sd-dialog'>";
    html += "<div class='sd-dialog-head'>";
    html += "<div><div id='sd-modal-title' class='sd-dialog-title'>Fitxer</div><div id='sd-modal-path' class='sd-dialog-path'></div></div>";
    html += "<div class='sd-dialog-actions'><a id='sd-modal-download' class='btn secondary' href='#'>Descarregar</a><button id='sd-modal-close' class='secondary' type='button'>Tancar</button></div>";
    html += "</div>";
    html += "<pre id='sd-modal-content' class='sd-preview'>Carregant...</pre>";
    html += "</div>";
    html += "</div>";
  }
  html += "</div>";

  html += "<div id='sd-maintenance' class='card'>";
  html += "<h2>Formatar / netejar SD</h2>";
  html += "<p class='hint'>El botó següent fa una neteja lògica: esborra tots els fitxers de la microSD i recrea l'estructura <code>/boia</code>. Això no reparticiona ni refà el FAT físic. Si la targeta està corrupta de veritat, formata-la al PC en FAT32.</p>";
  html += "<form method='POST' action='/sd-format' data-confirm='Això esborrarà TOTS els fitxers de la microSD. Segur?'>";
  html += "<button class='btn danger' type='submit'>Netejar SD i recrear estructura</button>";
  html += "</form>";
  html += "</div>";

  appendPageEnd(html);
  return html;
}

static String buildSdFileViewPage(const String& path) {
  String clean;
  if (!normalizeSdPath(path, clean)) clean = SD_BASE_DIR;

  bool truncated = false;
  String content = sdReadTextFileLimited(clean, 24576, truncated);

  int parentSlash = clean.lastIndexOf('/');
  String parentPath = parentSlash <= 0 ? String("/") : clean.substring(0, parentSlash);

  String html = "";
  appendPageStart(html, "storage", false);
  html += "<div class='card'>";
  html += "<h2>Visualitzador de fitxer</h2>";
  html += "<p class='hint'>Ruta: <code>" + htmlEscape(clean) + "</code></p>";
  html += "<div class='actions' style='margin-bottom:12px'>";
  html += "<a class='btn secondary' href='/storage?path=" + htmlEscape(parentPath) + "'>Tornar al directori</a>";
  html += "<a class='btn secondary' href='/sd-download?path=" + htmlEscape(clean) + "'>Descarregar</a>";
  html += "</div>";
  if (truncated) {
    html += "<p class='hint warn'>Vista retallada: el fitxer és més gran. Descarrega'l per veure'l complet.</p>";
  }
  html += "<pre class='file-preview'>" + htmlEscape(content) + "</pre>";
  html += "</div>";
  appendPageEnd(html);
  return html;
}

static String buildConfigPage() {
  String html = "";
  appendPageStart(html, "config", false);

  static const char* labels[] = {"Lectura", "Calibratge", "Reset"};
  static const char* anchors[] = {"temp-reading", "temp-calibration", "temp-reset"};
  appendSubTabs(html, "Temperatura", labels, anchors, 3);

  html += "<div id='temp-reading' class='card'>";
  html += "<h2>Configuracio de temperatura</h2>";
  html += "<form method='POST' action='/config'>";

  html += "<div>";
  html += "<div class='label'>Interval de lectura interna, en segons ";
  html += "(" + String(MIN_READ_INTERVAL_SECONDS) + " - " + String(MAX_READ_INTERVAL_SECONDS) + ")";
  html += "</div>";
  html += "<input name='read_interval' type='number' min='";
  html += String(MIN_READ_INTERVAL_SECONDS);
  html += "' max='";
  html += String(MAX_READ_INTERVAL_SECONDS);
  html += "' value='";
  html += String(configReadIntervalSeconds);
  html += "'>";
  html += "</div>";

  html += "<div>";
  html += "<div class='label'>Decimals temperatura ";
  html += "(" + String(MIN_TEMPERATURE_DECIMALS) + " - " + String(MAX_TEMPERATURE_DECIMALS) + ")";
  html += "</div>";
  html += "<input name='decimals' type='number' min='";
  html += String(MIN_TEMPERATURE_DECIMALS);
  html += "' max='";
  html += String(MAX_TEMPERATURE_DECIMALS);
  html += "' value='";
  html += String(configTemperatureDecimals);
  html += "'>";
  html += "</div>";

  html += "<h3>Comptadors de lectures</h3><div class='grid'>";
  html += "<div class='item'><div class='label'>Totals</div><div class='value'>" + String(appState.totalReads) + "</div></div>";
  html += "<div class='item'><div class='label'>Vàlides</div><div class='value ok'>" + String(appState.validReads) + "</div></div>";
  html += "<div class='item'><div class='label'>Fallides</div><div class='value ";
  html += appState.failedReads == 0 ? "ok" : "bad";
  html += "'>" + String(appState.failedReads) + "</div></div>";
  html += "<div class='item'><div class='label'>Últim error</div><div class='value' style='font-size:15px'>" + htmlEscape(appState.lastErrorMessage) + "</div></div></div>";

  html += "<div class='buttons'>";
  html += "<button type='submit'>Guardar lectura</button>";
  html += "</div>";

  html += "</form>";
  html += "</div>";

  html += "<div id='temp-calibration' class='card'>";
  html += "<h2>Calibratge i proteccio de la sonda</h2>";
  html += "<p class='hint'>L'offset s'aplica a la lectura abans de publicar-la. Els valors fora del rang logic es descarten. Les lectures tipiques dolentes de DS18B20, -127 C i 85 C, tambe es descarten.</p>";
  html += "<form method='POST' action='/config'>";

  html += "<div class='grid'>";

  html += "<div>";
  html += "<div class='label'>Offset temperatura, en C ";
  html += "(" + floatText(MIN_TEMPERATURE_OFFSET_C, 1) + " a " + floatText(MAX_TEMPERATURE_OFFSET_C, 1) + ")";
  html += "</div>";
  html += "<input name='temperature_offset' type='number' step='0.1' min='";
  html += floatText(MIN_TEMPERATURE_OFFSET_C, 1);
  html += "' max='";
  html += floatText(MAX_TEMPERATURE_OFFSET_C, 1);
  html += "' value='";
  html += floatText(configTemperatureOffsetC, 2);
  html += "'>";
  html += "</div>";

  html += "<div>";
  html += "<div class='label'>Temperatura minima valida, en C</div>";
  html += "<input name='min_valid_temp' type='number' step='0.1' min='";
  html += floatText(ABSOLUTE_MIN_VALID_TEMP_C, 1);
  html += "' max='";
  html += floatText(ABSOLUTE_MAX_VALID_TEMP_C, 1);
  html += "' value='";
  html += floatText(configMinValidTempC, 2);
  html += "'>";
  html += "</div>";

  html += "<div>";
  html += "<div class='label'>Temperatura maxima valida, en C</div>";
  html += "<input name='max_valid_temp' type='number' step='0.1' min='";
  html += floatText(ABSOLUTE_MIN_VALID_TEMP_C, 1);
  html += "' max='";
  html += floatText(ABSOLUTE_MAX_VALID_TEMP_C, 1);
  html += "' value='";
  html += floatText(configMaxValidTempC, 2);
  html += "'>";
  html += "</div>";

  html += "<div class='item'>";
  html += "<div class='label'>Estat sonda</div><div class='value ";
  html += appState.sensorStatus == "OK" ? "ok" : (appState.sensorStatus == "ERROR" ? "bad" : "warn");
  html += "'>";
  html += htmlEscape(appState.sensorStatus);
  html += "</div><div class='small'>Errors consecutius: ";
  html += String(appState.consecutiveSensorErrors);
  html += "</div></div>";

  html += "</div>";

  html += "<div class='buttons'>";
  html += "<button type='submit'>Guardar calibratge i sonda</button>";
  html += "</div>";

  html += "</form>";
  html += "</div>";

  html += "<div id='temp-reset' class='card'>";
  html += "<h2>Restaurar temperatura</h2>";
  html += "<p class='hint'>Torna els paràmetres de lectura, decimals, offset i rang vàlid als valors base del firmware.</p>";
  html += "<form method='POST' action='/defaults'>";
  html += "<button class='secondary' type='submit'>Restaurar valors per defecte de temperatura i sonda</button>";
  html += "</form>";

  html += "</div>";

  appendPageEnd(html);
  return html;
}

static String buildWifiPage() {
  String html = "";
  appendPageStart(html, "wifi", false);

  static const char* labels[] = {"Estat", "Credencials", "Xarxa avançada", "Rescat"};
  static const char* anchors[] = {"wifi-status", "wifi-credentials", "wifi-network", "wifi-recovery"};
  appendSubTabs(html, "Wi-Fi", labels, anchors, 4);

  html += "<div id='wifi-status' class='card'>";
  html += "<h2>Estat Wi-Fi</h2>";
  html += "<div class='grid'>";

  html += "<div class='item'><div class='label'>Estat</div><div class='value ";
  html += isWifiConnected() ? "ok" : "warn";
  html += "'>";
  html += wifiStatusText();
  html += "</div></div>";

  html += "<div class='item'><div class='label'>Mode</div><div class='value'>";
  html += htmlEscape(wifiModeText());
  html += "</div></div>";

  html += "<div class='item'><div class='label'>SSID configurat</div><div class='value'>";
  html += htmlEscape(wifiConfiguredSsidText());
  html += "</div></div>";

  html += "<div class='item'><div class='label'>SSID connectat</div><div class='value'>";
  html += htmlEscape(WiFi.SSID());
  html += "</div></div>";

  html += "<div class='item'><div class='label'>IP STA</div><div class='value'>";
  html += htmlEscape(wifiStaIpText());
  html += "</div></div>";

  html += "<div class='item'><div class='label'>Tipus IP</div><div class='value'>";
  html += configWifiUseStaticIp ? "IP fixa" : "DHCP";
  html += "</div></div>";

  html += "<div class='item'><div class='label'>Gateway</div><div class='value'>";
  html += isWifiConnected() ? WiFi.gatewayIP().toString() : htmlEscape(configWifiGateway);
  html += "</div></div>";

  html += "<div class='item'><div class='label'>DNS</div><div class='value'>";
  html += isWifiConnected() ? WiFi.dnsIP().toString() : htmlEscape(configWifiDns1);
  html += "</div></div>";

  html += "<div class='item'><div class='label'>IP AP</div><div class='value'>";
  html += htmlEscape(wifiApIpText());
  html += "</div></div>";

  html += "<div class='item'><div class='label'>RSSI</div><div class='value'>";
  html += isWifiConnected() ? String(WiFi.RSSI()) + " dBm" : "Sense senyal";
  html += "</div></div>";

  html += "<div class='item'><div class='label'>AP de rescat</div><div class='value'>";
  html += htmlEscape(String(WIFI_AP_SSID));
  html += "</div></div>";

  html += "</div>";
  html += "</div>";

  html += "<div id='wifi-credentials' class='card'>";
  html += "<h2>Canviar Wi-Fi</h2>";
  html += "<p class='hint'>Si guardes un Wi-Fi incorrecte, la boia obrira automaticament l'AP de rescat <b>";
  html += htmlEscape(String(WIFI_AP_SSID));
  html += "</b>. Des d'aquest AP, entra a <b>http://192.168.4.1</b> i corregeix la configuracio.</p>";
  html += "<p class='small'>Per privacitat, Android, iOS i el navegador no permeten que aquesta web llegeixi les xarxes desades al mobil. La boia sí que pot buscar les xarxes que té a prop.</p>";

  html += "<form method='POST' action='/wifi'>";

  html += "<div>";
  html += "<div class='label'>SSID</div>";
  html += "<input id='wifi_ssid' name='ssid' type='text' maxlength='64' value='";
  html += htmlEscape(configWifiSsid);
  html += "'>";
  html += "<div class='wifi-scan-actions'><button id='wifi-scan-button' class='secondary' type='button' onclick='scanWifiNetworks()'>Buscar xarxes Wi-Fi</button><span id='wifi-scan-status' class='small'>L'escaneig pot trigar uns segons.</span></div>";
  html += "<div id='wifi-scan-list' class='wifi-scan-list' aria-live='polite'></div>";
  html += "</div>";

  html += "<div>";
  html += "<div class='label'>Password</div>";
  html += "<div class='password-wrap'>";
  html += "<input id='wifi_password' name='password' type='password' maxlength='64' value='";
  html += htmlEscape(configWifiPassword);
  html += "' placeholder='Password Wi-Fi'>";
  html += "<button class='eye-button' type='button' aria-label='Mostrar password' onclick=\"togglePassword('wifi_password',this)\">👁️</button>";
  html += "</div>";
  html += "</div>";

  html += "<div class='buttons'>";
  html += "<button type='submit'>Guardar Wi-Fi i reconnectar</button>";
  html += "</div>";
  html += "</form>";
  html += "</div>";

  html += "<div id='wifi-network' class='card'>";
  html += "<h2>Xarxa avançada</h2>";
  html += "<p class='hint'>DHCP és l'opcio segura. IP fixa només si ho tens clar. Si la IP fixa queda malament, l'AP de rescat ha de salvar-te.</p>";
  html += "<p class='small'>Quan canvies l'SSID, la boia força DHCP per evitar heretar una IP fixa incompatible. Després de connectar-la a la xarxa nova, pots tornar aquí i activar una IP fixa expressament.</p>";
  html += "<form method='POST' action='/wifi-network'>";

  html += "<div>";
  html += "<label><input id='wifi_use_static_ip' name='use_static_ip' type='checkbox' value='1' ";
  html += configWifiUseStaticIp ? "checked" : "";
  html += ">Fer servir IP fixa</label>";
  html += "</div>";

  html += "<div class='grid'>";

  html += "<div><div class='label'>IP fixa</div><input name='static_ip' type='text' maxlength='15' value='";
  html += htmlEscape(configWifiStaticIp);
  html += "'></div>";

  html += "<div><div class='label'>Gateway</div><input name='gateway' type='text' maxlength='15' value='";
  html += htmlEscape(configWifiGateway);
  html += "'></div>";

  html += "<div><div class='label'>Subnet mask</div><input name='subnet' type='text' maxlength='15' value='";
  html += htmlEscape(configWifiSubnet);
  html += "'></div>";

  html += "<div><div class='label'>DNS 1</div><input name='dns1' type='text' maxlength='15' value='";
  html += htmlEscape(configWifiDns1);
  html += "'></div>";

  html += "<div><div class='label'>DNS 2</div><input name='dns2' type='text' maxlength='15' value='";
  html += htmlEscape(configWifiDns2);
  html += "'></div>";

  html += "</div>";

  html += "<div class='buttons'>";
  html += "<button type='submit'>Guardar configuració de xarxa</button>";
  html += "</div>";

  html += "</form>";
  html += "</div>";

  html += "<div id='wifi-recovery' class='card'>";
  html += "<h2>Rescat i restauració Wi-Fi</h2>";
  html += "<p class='hint'>Si et quedes sense accés, connecta't a l'AP de rescat <b>";
  html += htmlEscape(String(WIFI_AP_SSID));
  html += "</b> i obre <b>http://192.168.4.1</b>.</p>";
  html += "<div class='buttons'>";
  html += "<form method='POST' action='/wifi-network-reset' style='display:inline;'>";
  html += "<button class='secondary' type='submit'>Restaurar DHCP</button>";
  html += "</form>";
  html += "<form method='POST' action='/wifi-reset' style='display:inline;'>";
  html += "<button class='danger' type='submit'>Restaurar Wi-Fi per defecte</button>";
  html += "</form>";
  html += "</div>";

  html += "</div>";

  appendPageEnd(html);
  return html;
}

static String buildMqttPage() {
  String html = "";
  appendPageStart(html, "mqtt", false);

  static const char* labels[] = {"Estat", "Broker", "Home Assistant", "Accions"};
  static const char* anchors[] = {"mqtt-status", "mqtt-broker", "mqtt-ha", "mqtt-actions"};
  appendSubTabs(html, "MQTT / Home Assistant", labels, anchors, 4);

  html += "<div id='mqtt-status' class='card'>";
  html += "<h2>Estat MQTT</h2>";
  html += "<div class='grid'>";

  html += "<div class='item'><div class='label'>MQTT</div><div class='value ";
  html += configMqttEnabled ? statusClass(isMqttConnected()) : "warn";
  html += "'>";
  html += mqttStatusText();
  html += "</div></div>";

  html += "<div class='item'><div class='label'>Activat</div><div class='value'>";
  html += enabledText(configMqttEnabled);
  html += "</div></div>";

  html += "<div class='item'><div class='label'>Broker</div><div class='value'>";
  html += htmlEscape(configMqttHost);
  html += ":";
  html += String(configMqttPort);
  html += "</div></div>";

  html += "<div class='item'><div class='label'>Usuari</div><div class='value'>";
  html += configMqttUser.length() > 0 ? htmlEscape(configMqttUser) : "Sense usuari";
  html += "</div></div>";

  html += "<div class='item'><div class='label'>Topic base</div><div class='value'>";
  html += htmlEscape(configMqttTopicBase);
  html += "</div></div>";

  html += "<div class='item'><div class='label'>Availability topic</div><div class='value'>";
  html += htmlEscape(mqttAvailabilityTopic());
  html += "</div></div>";

  html += "<div class='item'><div class='label'>Publicacions</div><div class='value'>";
  html += String(appState.mqttPublishCount);
  html += "</div></div>";

  html += "<div class='item'><div class='label'>Errors</div><div class='value ";
  html += appState.mqttFailCount == 0 ? "ok" : "bad";
  html += "'>";
  html += String(appState.mqttFailCount);
  html += "</div></div>";

  html += "<div class='item'><div class='label'>Discovery HA</div><div class='value ";
  html += configHaDiscoveryEnabled ? "ok" : "warn";
  html += "'>";
  html += enabledText(configHaDiscoveryEnabled);
  html += "</div></div>";

  html += "<div class='item'><div class='label'>Discovery publicat</div><div class='value ";
  html += appState.mqttDiscoveryPublished ? "ok" : "warn";
  html += "'>";
  html += appState.mqttDiscoveryPublished ? "Si" : "No";
  html += "</div></div>";

  html += "<div class='item'><div class='label'>Prefix HA</div><div class='value'>";
  html += htmlEscape(configHaDiscoveryPrefix);
  html += "</div></div>";

  html += "<div class='item'><div class='label'>Topic restart</div><div class='value' style='font-size:15px;'>";
  html += htmlEscape(mqttCommandRestartTopic());
  html += "</div></div>";

  html += "<div class='item'><div class='label'>Topic publicar Discovery</div><div class='value' style='font-size:15px;'>";
  html += htmlEscape(mqttCommandPublishDiscoveryTopic());
  html += "</div></div>";

  html += "<div class='item'><div class='label'>Controls Home Assistant</div><div class='value ok'>v1.0 actius</div></div>";

  html += "</div>";
  html += "</div>";

  html += "<div id='mqtt-broker' class='card'>";
  html += "<h2>Configuracio MQTT</h2>";
  html += "<p class='hint'>Aixo guarda MQTT a la memoria de l'ESP32. En guardar, es reinicia nomes la connexio MQTT, no tota la boia.</p>";

  html += "<form method='POST' action='/mqtt'>";

  html += "<div>";
  html += "<label><input name='enabled' type='checkbox' value='1' ";
  html += configMqttEnabled ? "checked" : "";
  html += ">Activar MQTT</label>";
  html += "</div>";

  html += "<div>";
  html += "<div class='label'>Broker / host</div>";
  html += "<input name='host' type='text' maxlength='96' value='";
  html += htmlEscape(configMqttHost);
  html += "'>";
  html += "</div>";

  html += "<div>";
  html += "<div class='label'>Port</div>";
  html += "<input name='port' type='number' min='1' max='65535' value='";
  html += String(configMqttPort);
  html += "'>";
  html += "</div>";

  html += "<div>";
  html += "<div class='label'>Usuari</div>";
  html += "<input name='user' type='text' maxlength='64' value='";
  html += htmlEscape(configMqttUser);
  html += "'>";
  html += "</div>";

  html += "<div>";
  html += "<div class='label'>Password</div>";
  html += "<div class='password-wrap'>";
  html += "<input id='mqtt_password' name='password' type='password' maxlength='64' value='";
  html += htmlEscape(configMqttPassword);
  html += "' placeholder='Password MQTT'>";
  html += "<button class='eye-button' type='button' aria-label='Mostrar password' onclick=\"togglePassword('mqtt_password',this)\">👁️</button>";
  html += "</div>";
  html += "</div>";

  html += "<div>";
  html += "<div class='label'>Topic base</div>";
  html += "<input name='topic_base' type='text' maxlength='96' value='";
  html += htmlEscape(configMqttTopicBase);
  html += "'>";
  html += "</div>";

  html += "<div>";
  html += "<div class='label'>Interval de publicacio MQTT, en segons ";
  html += "(" + String(MIN_MQTT_INTERVAL_SECONDS) + " - " + String(MAX_MQTT_INTERVAL_SECONDS) + ")";
  html += "</div>";
  html += "<input name='publish_interval' type='number' min='";
  html += String(MIN_MQTT_INTERVAL_SECONDS);
  html += "' max='";
  html += String(MAX_MQTT_INTERVAL_SECONDS);
  html += "' value='";
  html += String(configMqttPublishIntervalSeconds);
  html += "'>";
  html += "</div>";

  html += "</div>";

  html += "<div id='mqtt-ha' class='card'>";
  html += "<h2>Home Assistant Discovery</h2>";
  html += "<p class='hint'>Aixo fa que Home Assistant detecti la boia automaticament com a dispositiu MQTT. El prefix normal de Home Assistant es <b>homeassistant</b>.</p>";
  html += "<p class='hint'><b>v1.0:</b> Home Assistant tambe pot modificar interval de lectura, decimals, interval de publicacio MQTT, reiniciar la boia i republicar Discovery. Compte amb el switch MQTT: si el desactives des de HA, despres l'hauras de reactivar des de la web.</p>";

  html += "<div>";
  html += "<label><input name='ha_discovery' type='checkbox' value='1' ";
  html += configHaDiscoveryEnabled ? "checked" : "";
  html += ">Activar Home Assistant Discovery</label>";
  html += "</div>";

  html += "<div>";
  html += "<div class='label'>Discovery prefix</div>";
  html += "<input name='ha_prefix' type='text' maxlength='64' value='";
  html += htmlEscape(configHaDiscoveryPrefix);
  html += "'>";
  html += "</div>";

  html += "<div>";
  html += "<div class='label'>Device ID Home Assistant</div>";
  html += "<input name='ha_device_id' type='text' maxlength='64' value='";
  html += htmlEscape(configHaDeviceId);
  html += "'>";
  html += "</div>";

  html += "<div>";
  html += "<div class='label'>Nom dispositiu Home Assistant</div>";
  html += "<input name='ha_device_name' type='text' maxlength='96' value='";
  html += htmlEscape(configHaDeviceName);
  html += "'>";
  html += "</div>";

  html += "<div class='buttons'>";
  html += "<button type='submit'>Guardar MQTT</button>";
  html += "</div>";

  html += "</form>";
  html += "</div>";

  html += "<div id='mqtt-ha-api' class='card subpage-extra' data-parent='mqtt-ha'>";
  html += "<h2>Estadístiques de Home Assistant</h2>";
  html += "<p class='hint'>La web consulta les estadístiques agregades de Home Assistant: mitjana, mínim i màxim. Necessita un token de llarga durada i els Entity ID correctes.</p>";
  html += "<form method='POST' action='/ha-api'>";
  html += "<div><label><input name='ha_api_enabled' type='checkbox' value='1' ";
  html += configHaApiEnabled ? "checked" : "";
  html += ">Activar lectura d'històric des de Home Assistant</label></div>";
  html += "<div><div class='label'>URL Home Assistant</div><input name='ha_api_url' type='text' maxlength='128' value='";
  html += htmlEscape(configHaApiUrl);
  html += "' placeholder='http://homeassistant.local:8123'></div>";
  html += "<div><div class='label'>Entity ID de temperatura a Home Assistant</div><input name='ha_history_entity' type='text' maxlength='96' value='";
  html += htmlEscape(configHaHistoryEntityId);
  html += "' placeholder='sensor.boia_piscina_temperature'></div>";
  html += "<div><div class='label'>Entity ID temperatura interior boia</div><input name='ha_internal_temperature_entity' type='text' maxlength='96' value='";
  html += htmlEscape(configHaInternalTemperatureEntityId);
  html += "'></div>";
  html += "<div><div class='label'>Entity ID humitat interior boia</div><input name='ha_internal_humidity_entity' type='text' maxlength='96' value='";
  html += htmlEscape(configHaInternalHumidityEntityId);
  html += "'></div>";
  html += "<div><div class='label'>Entity ID bateria</div><input name='ha_battery_entity' type='text' maxlength='96' value='";
  html += htmlEscape(configHaBatteryEntityId);
  html += "' placeholder='sensor.boia_piscina_battery'></div>";
  html += "<div class='item'><div class='label'>Resolució automàtica</div><div class='small'>48 hores: horària · 31 dies, 6 mesos i 1 any: diària. Cada punt inclou mitjana, mínim i màxim.</div></div>";
  html += "<div><div class='label'>Token Home Assistant</div><div class='password-wrap'><input id='ha_api_token' name='ha_api_token' type='password' maxlength='256' value='";
  html += htmlEscape(configHaApiToken);
  html += "' placeholder='Token de llarga durada'><button class='eye-button' type='button' aria-label='Mostrar token' onclick=\"togglePassword('ha_api_token',this)\">👁️</button></div><div class='small'>Home Assistant → Perfil d'usuari → Tokens de llarga durada. Enganxa només el token, sense escriure Bearer. Si l'enganxes amb Bearer, la boia el neteja automàticament.</div></div>";
  html += "<div class='buttons'><button type='submit'>Guardar API HA</button></div>";
  html += "</form>";
  html += "</div>";

  html += "<div id='mqtt-actions' class='card'>";
  html += "<h2>Accions MQTT / HA</h2>";
  html += "<form method='POST' action='/mqtt-reset'>";
  html += "<button class='secondary' type='submit'>Restaurar MQTT per defecte</button>";
  html += "</form>";

  html += "<form method='POST' action='/mqtt-discovery'>";
  html += "<button class='secondary' type='submit'>Publicar Discovery ara</button>";
  html += "</form>";

  html += "</div>";

  appendPageEnd(html);
  return html;
}



static String rssiQualityText() {
  if (!isWifiConnected()) return "Sense senyal";
  int rssi = WiFi.RSSI();
  if (rssi >= -60) return "Excel·lent";
  if (rssi >= -70) return "Bona";
  if (rssi >= -80) return "Justa";
  return "Dolenta";
}

static String rssiQualityClass() {
  if (!isWifiConnected()) return "warn";
  int rssi = WiFi.RSSI();
  if (rssi >= -70) return "ok";
  if (rssi >= -80) return "warn";
  return "bad";
}

static String healthText() {
  if (!isWifiConnected() && !isWifiApActive()) return "ERROR";
  if (appState.sensorStatus == "ERROR") return "ERROR";
  if (configMqttEnabled && !isMqttConnected()) return "WARNING";
  if (isWifiConnected() && WiFi.RSSI() < -80) return "WARNING";
  if (appState.consecutiveSensorErrors > 0) return "WARNING";
  return "OK";
}

static String healthClass() {
  String h = healthText();
  if (h == "OK") return "ok";
  if (h == "WARNING") return "warn";
  return "bad";
}

static String healthDetails() {
  String details = "";
  if (!isWifiConnected() && !isWifiApActive()) details += "Wi-Fi desconnectat. ";
  if (isWifiConnected() && WiFi.RSSI() < -80) details += "RSSI dolent. ";
  if (appState.sensorStatus == "ERROR") details += "Sonda amb error. ";
  if (appState.consecutiveSensorErrors > 0) details += "Hi ha errors consecutius de sonda. ";
  if (appState.internalEnvStatus == "ERROR") details += "SHT41 amb error. ";
  if (configMqttEnabled && !isMqttConnected()) details += "MQTT activat però desconnectat. ";
  if (details.length() == 0) details = "Tots els serveis principals estan dins del rang esperat.";
  return details;
}

static String buildConfigExportJson(bool includePasswords) {
  String json = "{\n";
  json += "  \"firmware_version\": \"" + jsonEscape(String(FIRMWARE_VERSION)) + "\",\n";
  json += "  \"device_name\": \"" + jsonEscape(configDeviceName) + "\",\n";
  json += "  \"device_hostname\": \"" + jsonEscape(configDeviceHostname) + "\",\n";
  json += "  \"production_mode\": "; json += (configProductionMode ? "true" : "false"); json += ",\n";
  json += "  \"wifi_ssid\": \"" + jsonEscape(configWifiSsid) + "\",\n";
  if (includePasswords) json += "  \"wifi_password\": \"" + jsonEscape(configWifiPassword) + "\",\n";
  json += "  \"wifi_use_static_ip\": "; json += (configWifiUseStaticIp ? "true" : "false"); json += ",\n";
  json += "  \"wifi_static_ip\": \"" + jsonEscape(configWifiStaticIp) + "\",\n";
  json += "  \"wifi_gateway\": \"" + jsonEscape(configWifiGateway) + "\",\n";
  json += "  \"wifi_subnet\": \"" + jsonEscape(configWifiSubnet) + "\",\n";
  json += "  \"wifi_dns1\": \"" + jsonEscape(configWifiDns1) + "\",\n";
  json += "  \"wifi_dns2\": \"" + jsonEscape(configWifiDns2) + "\",\n";
  json += "  \"mqtt_enabled\": "; json += (configMqttEnabled ? "true" : "false"); json += ",\n";
  json += "  \"mqtt_host\": \"" + jsonEscape(configMqttHost) + "\",\n";
  json += "  \"mqtt_port\": " + String(configMqttPort) + ",\n";
  json += "  \"mqtt_user\": \"" + jsonEscape(configMqttUser) + "\",\n";
  if (includePasswords) json += "  \"mqtt_password\": \"" + jsonEscape(configMqttPassword) + "\",\n";
  json += "  \"mqtt_topic_base\": \"" + jsonEscape(configMqttTopicBase) + "\",\n";
  json += "  \"mqtt_publish_interval_seconds\": " + String(configMqttPublishIntervalSeconds) + ",\n";
  json += "  \"ha_discovery_enabled\": "; json += (configHaDiscoveryEnabled ? "true" : "false"); json += ",\n";
  json += "  \"ha_discovery_prefix\": \"" + jsonEscape(configHaDiscoveryPrefix) + "\",\n";
  json += "  \"ha_device_id\": \"" + jsonEscape(configHaDeviceId) + "\",\n";
  json += "  \"ha_device_name\": \"" + jsonEscape(configHaDeviceName) + "\",\n";
  json += "  \"ha_api_enabled\": "; json += (configHaApiEnabled ? "true" : "false"); json += ",\n";
  json += "  \"ha_api_url\": \"" + jsonEscape(configHaApiUrl) + "\",\n";
  if (includePasswords) json += "  \"ha_api_token\": \"" + jsonEscape(configHaApiToken) + "\",\n";
  json += "  \"ha_history_entity_id\": \"" + jsonEscape(configHaHistoryEntityId) + "\",\n";
  json += "  \"ha_internal_temperature_entity_id\": \"" + jsonEscape(configHaInternalTemperatureEntityId) + "\",\n";
  json += "  \"ha_internal_humidity_entity_id\": \"" + jsonEscape(configHaInternalHumidityEntityId) + "\",\n";
  json += "  \"ha_battery_entity_id\": \"" + jsonEscape(configHaBatteryEntityId) + "\",\n";
  json += "  \"ha_history_hours\": " + String(configHaHistoryHours) + ",\n";
  json += "  \"github_ota_enabled\": "; json += (configGithubOtaEnabled ? "true" : "false"); json += ",\n";
  json += "  \"github_manifest_url\": \"" + jsonEscape(configGithubManifestUrl) + "\",\n";
  json += "  \"github_allow_same_version_update\": "; json += (configGithubAllowSameVersionUpdate ? "true" : "false"); json += ",\n";
  json += "  \"internet_check_done\": "; json += (appState.internetCheckDone ? "true" : "false"); json += ",\n";
  json += "  \"internet_check_ok\": "; json += (appState.internetCheckOk ? "true" : "false"); json += ",\n";
  json += "  \"internet_check_message\": \"" + jsonEscape(appState.internetCheckMessage) + "\",\n";
  json += "  \"read_interval_seconds\": " + String(configReadIntervalSeconds) + ",\n";
  json += "  \"temperature_decimals\": " + String(configTemperatureDecimals) + ",\n";
  json += "  \"temperature_offset_c\": " + floatText(configTemperatureOffsetC, 2) + ",\n";
  json += "  \"min_valid_temp_c\": " + floatText(configMinValidTempC, 2) + ",\n";
  json += "  \"max_valid_temp_c\": " + floatText(configMaxValidTempC, 2) + ",\n";
  json += "  \"battery_empty_voltage\": " + floatText(configBatteryEmptyVoltage, 2) + ",\n";
  json += "  \"battery_full_voltage\": " + floatText(configBatteryFullVoltage, 2) + ",\n";
  json += "  \"battery_low_percent\": " + floatText(configBatteryLowPercent, 0) + ",\n";
  json += "  \"battery_calibration_factor\": " + floatText(configBatteryCalibrationFactor, 3) + ",\n";
  json += "  \"board_led_enabled\": "; json += (configBoardLedEnabled ? "true" : "false"); json += ",\n";
  json += "  \"board_led_mirror_status\": "; json += (configBoardLedMirrorStatus ? "true" : "false"); json += "\n";
  json += "}";
  return json;
}

static bool extractJsonStringValue(const String& json, const String& key, String& value) {
  String pattern = "\"" + key + "\"";
  int p = json.indexOf(pattern);
  if (p < 0) return false;
  int colon = json.indexOf(':', p + pattern.length());
  if (colon < 0) return false;
  int q1 = json.indexOf('"', colon + 1);
  if (q1 < 0) return false;
  int q2 = json.indexOf('"', q1 + 1);
  if (q2 < 0) return false;
  value = json.substring(q1 + 1, q2);
  value.replace("\\\"", "\"");
  return true;
}

static bool extractJsonBoolValue(const String& json, const String& key, bool& value) {
  String pattern = "\"" + key + "\"";
  int p = json.indexOf(pattern);
  if (p < 0) return false;
  int colon = json.indexOf(':', p + pattern.length());
  if (colon < 0) return false;
  String tail = json.substring(colon + 1, min((int)json.length(), colon + 12));
  tail.trim();
  if (tail.startsWith("true")) { value = true; return true; }
  if (tail.startsWith("false")) { value = false; return true; }
  return false;
}

static bool extractJsonFloatValue(const String& json, const String& key, float& value) {
  String pattern = "\"" + key + "\"";
  int p = json.indexOf(pattern);
  if (p < 0) return false;
  int colon = json.indexOf(':', p + pattern.length());
  if (colon < 0) return false;
  int end = colon + 1;
  while (end < (int)json.length() && json[end] != ',' && json[end] != '\n' && json[end] != '}') end++;
  String n = json.substring(colon + 1, end); n.trim();
  value = n.toFloat();
  return n.length() > 0;
}

static bool extractJsonUInt16Value(const String& json, const String& key, uint16_t& value) {
  float f = 0;
  if (!extractJsonFloatValue(json, key, f)) return false;
  value = (uint16_t)f;
  return true;
}

static String boolBadge(bool ok, const String& okText, const String& badText) {
  String html = "<span class='";
  html += ok ? "ok" : "bad";
  html += "'>";
  html += ok ? htmlEscape(okText) : htmlEscape(badText);
  html += "</span>";
  return html;
}

static void appendChecklistItem(String& html, const String& label, const String& valueHtml, const String& smallText) {
  html += "<div class='item'><div class='label'>";
  html += htmlEscape(label);
  html += "</div><div class='value'>";
  html += valueHtml;
  html += "</div><div class='small'>";
  html += htmlEscape(smallText);
  html += "</div></div>";
}

static void appendDiagnosticsSection(String& html) {
  bool wifiOk = isWifiConnected();
  bool mqttOk = !configMqttEnabled || isMqttConnected();
  bool sensorOk = appState.sensorStatus == "OK";
  bool tempOk = !isnan(appState.lastValidTemperatureC);
  bool internalEnvOk = appState.internalEnvStatus == "OK";
  bool discoveryOk = !configHaDiscoveryEnabled || appState.mqttDiscoveryPublished;
  bool otaOk = !appState.otaInProgress;
  bool overallReady = wifiOk && mqttOk && sensorOk && tempOk && internalEnvOk && discoveryOk && otaOk;

  html += "<div class='card'>";
  html += "<h2>Estat general</h2>";
  html += "<div class='item'><div class='label'>Preparació per tancar la boia</div><div class='value ";
  html += overallReady ? "ok" : "bad";
  html += "'>";
  html += overallReady ? "LLESTA" : "REVISAR";
  html += "</div><div class='small'>Això és el resum ràpid. Si surt REVISAR, no la tanquis encara dins el tub.</div></div>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<h2>Checklist diagnòstic</h2>";
  html += "<div class='grid'>";

  appendChecklistItem(html, "Wi-Fi connectat", boolBadge(wifiOk, "OK", "ERROR"), wifiStatusText() + " · IP: " + wifiStaIpText());
  appendChecklistItem(html, "MQTT", boolBadge(mqttOk, "OK", "ERROR"), mqttStatusText() + " · " + configMqttHost + ":" + String(configMqttPort));
  appendChecklistItem(html, "Home Assistant Discovery", boolBadge(discoveryOk, "OK", "PENDENT"), String(configHaDiscoveryEnabled ? "Activat" : "Desactivat") + " · publicat: " + String(appState.mqttDiscoveryPublished ? "si" : "no"));
  appendChecklistItem(html, "Sonda DS18B20", boolBadge(sensorOk, "OK", "ERROR"), String("Estat: ") + appState.sensorStatus + " · errors consecutius: " + String(appState.consecutiveSensorErrors));
  appendChecklistItem(html, "Sensor intern SHT41", boolBadge(internalEnvOk, "OK", "ERROR"), String("Estat: ") + appState.internalEnvStatus + " · " + appState.internalEnvLastError);
  bool batteryOk = appState.batteryStatus == "OK" || appState.batteryStatus == "LOW" || appState.batteryStatus == "UNKNOWN";
  appendChecklistItem(html, "Bateria GPIO1", boolBadge(batteryOk, batteryStatusText(), "ERROR"), batteryVoltageText() + " · " + batteryPercentText() + " · ADC: " + (isnan(appState.lastBatteryAdcMilliVolts) ? String("sense dades") : String(appState.lastBatteryAdcMilliVolts, 0) + " mV"));

  String tempValue = tempOk ? formatTemperature(appState.lastValidTemperatureC, configTemperatureDecimals) + " ºC" : String("Encara cap");
  appendChecklistItem(html, "Última temperatura vàlida", htmlEscape(tempValue), String("Últim error: ") + appState.lastErrorMessage);

  appendChecklistItem(html, "OTA", boolBadge(otaOk, "Disponible", "En curs"), appState.otaLastMessage);
  appendChecklistItem(html, "Mode xarxa", htmlEscape(String(configWifiUseStaticIp ? "IP fixa" : "DHCP")), String("AP rescat: ") + String(isWifiApActive() ? "actiu" : "inactiu") + " · AP IP: " + wifiApIpText());

  String rssiText = isWifiConnected() ? String(WiFi.RSSI()) + " dBm" : String("sense senyal");
  appendChecklistItem(html, "Uptime", htmlEscape(String(getUptimeSeconds()) + " s"), String("RSSI: ") + rssiText);

  html += "</div>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<h2>GPIO i cablejat utilitzat</h2>";
  html += "<p class='hint'>Aquest apartat és la llegenda del muntatge físic. Si canvies pins al codi, canvia aquesta taula o et faràs trampes a tu mateix.</p>";
  html += "<div class='grid'>";

  html += "<div class='item'><div class='label'>Sonda temperatura DS18B20</div><div class='value'>GPIO";
  html += String(ONE_WIRE_PIN);
  html += "</div><div class='small'>Bus OneWire DATA. Necessita resistència pull-up de 4.7 kΩ entre DATA i 3V3. Alimentació recomanada: 3V3 i GND de l'ESP32.</div></div>";

  html += "<div class='item'><div class='label'>Sensor intern SHT41</div><div class='value'>GPIO6 / GPIO7</div><div class='small'>I2C: SDA GPIO6, SCL GPIO7, adreça 0x44 i alimentació 3V3.</div></div>";

  html += "<div class='item'><div class='label'>Botó físic de rescat</div><div class='value'>";
  html += htmlEscape(hardwareResetButtonPinText());
  html += "</div><div class='small'>Entrada amb pull-up intern. Connecta el botó entre GPIO";
  html += String(RESET_BUTTON_PIN);
  html += " i GND. Pulsació 5 s: reinici. 10 s: força AP setup. 20 s: reset total + AP setup.</div></div>";

  html += "<div class='item'><div class='label'>LED d'estat</div><div class='value'>";
  html += htmlEscape(hardwareStatusLedPinText());
  html += "</div><div class='small'>Sortida digital per LED extern amb resistència. Fix: tot OK. Lent: Wi-Fi. Molt lent: MQTT. Ràpid: AP setup. Curt: error sonda.</div></div>";

  html += "<div class='item'><div class='label'>Estat botó</div><div class='value'>";
  html += htmlEscape(hardwareButtonStatusText());
  html += "</div><div class='small'>Última acció: ";
  html += htmlEscape(hardwareLastActionText());
  html += "</div></div>";

  html += "<div class='item'><div class='label'>Estat LED</div><div class='value'>";
  html += htmlEscape(hardwareLedStatusText());
  html += "</div><div class='small'>El LED només és diagnòstic local. No controla cap relé ni cap alimentació.</div></div>";

  html += "<div class='item'><div class='label'>LED intern placa</div><div class='value'>";
  html += htmlEscape(hardwareBoardLedStatusText());
  html += "</div><div class='small'>Serveix per diagnòstic local sense afegir LED extern. En mode mirall copia exactament el patró del LED d'estat.</div></div>";

  html += "<div class='item'><div class='label'>Hardware manager</div><div class='value'>";
  html += htmlEscape(hardwareReadyStateText());
  html += "</div><div class='small'>Gestió de botó, LED i accions físiques.</div></div>";

  html += "</div>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<h2>Prova abans de tancar</h2>";
  html += "<p class='hint'>Prova OTA dues o tres vegades, prova el botó físic, comprova que l'AP de rescat arrenca i valida la sonda dins d'aigua. Si alguna d'aquestes falla, no tanquis la boia encara.</p>";
  html += "</div>";
}

static String buildDiagnosticsPage() {
  String html = "";
  appendPageStart(html, "maintenance", false);
  appendDiagnosticsSection(html);
  appendPageEnd(html);
  return html;
}


static void appendFirmwareSection(String& html) {
  html += "<div class='card'>";
  html += "<h2>Descripció del firmware</h2>";
  html += "<p class='hint'><b>";
  html += htmlEscape(configDeviceName);
  html += "</b> és el firmware de la boia de piscina basada en ESP32-C6 i sonda DS18B20. L'objectiu és mesurar temperatura, publicar telemetria per MQTT, integrar-se amb Home Assistant i poder mantenir el dispositiu sense obrir-lo físicament.</p>";
  html += "<div class='grid3'>";
  html += "<div class='item'><div class='label'>Plataforma</div><div class='value'>ESP32-C6</div><div class='small'>Wi-Fi 6 2.4 GHz, Bluetooth LE 5 i ràdio IEEE 802.15.4. Zigbee/Thread són capacitats del xip, però aquest firmware encara no les utilitza.</div></div>";
  html += "<div class='item'><div class='label'>Sensor</div><div class='value'>DS18B20</div><div class='small'>Lectures validades, offset, rang mínim/màxim i descart de valors típics dolents.</div></div>";
  html += "<div class='item'><div class='label'>Comunicació</div><div class='value'>Wi-Fi + MQTT</div><div class='small'>Configuració web, AP de rescat, Home Assistant Discovery i controls MQTT.</div></div>";
  html += "</div></div>";

  html += "<div class='card'>";
  html += "<h2>Actualitzacions</h2>";
  html += "<div class='item'><div class='label'>v1.18.0-sd-blackbox</div><div class='value'>SD caixa negra i buffer</div><div class='small'>Històrics diaris, estadístiques precalculades, logs, blackbox, snapshot de configuració, buffer MQTT offline i explorador/visor web de fitxers.</div></div>";
  html += "<div class='item'><div class='label'>v1.17.0-battery-config</div><div class='value'>Bateria configurable</div><div class='small'>Afegeix Sistema / Bateria amb volts buit/ple, percentatge LOW, calibratge ADC i ajust visual de la fitxa inicial.</div></div>";
  html += "<div class='item'><div class='label'>v1.16.0-sd-history</div><div class='value'>microSD i històric local</div><div class='small'>Afegeix suport SPI per microSD, pàgina SD / Històric, espai ocupat, descàrrega CSV, neteja lògica i guardat local de lectures a /boia/history/YYYY-MM-DD.csv.</div></div>";
  html += "<div class='item'><div class='label'>v1.15.0-battery-gpio1</div><div class='value'>Bateria per GPIO1</div><div class='small'>Lectura de bateria amb divisor 100k/100k, volts, percentatge aproximat i publicació MQTT/Home Assistant.</div></div>";
  html += "<div class='item'><div class='label'>v1.6.7-ota-auto-check-clean-log</div><div class='value'>OTA més neta i comprovació automàtica</div><div class='small'>La pantalla OTA comprova automàticament Internet i GitHub en entrar, actualitza les targetes sense canviar de pàgina i només mostra el log quan realment hi ha una actualització en curs o acabada.</div></div><div class='item'><div class='label'>v1.6.6-ota-diagnostics-log</div><div class='value'>Log OTA en directe i timeout</div><div class='small'>La pantalla OTA mostra un log detallat de cada pas de GitHub OTA i OTA local, amb missatges també al monitor sèrie. Si la descàrrega queda encallada sense dades, talla amb error en comptes de quedar-se indefinidament.</div></div><div class='item'><div class='label'>v1.6.5-ota-progress-ui</div><div class='value'>Barra de progrés OTA</div><div class='small'>Les actualitzacions OTA locals i des de GitHub mostren barra de progrés, fase, percentatge i bytes perquè es vegi clarament que la boia està treballant.</div></div><div class='item'><div class='label'>v1.6.4-release-helper</div><div class='value'>Release helper</div><div class='small'>Afegeix un script de release que llegeix el nom del canvi des del firmware i fa commit, rebase i push amb un missatge coherent.</div></div><div class='item'><div class='label'>v1.6.3-github-ota-ui-refresh</div><div class='value'>Pantalla GitHub OTA professional</div><div class='small'>La pantalla d'OTA mostra Internet, GitHub, manifest i actualització en targetes clares, guarda el resultat de les comprovacions i evita oferir downgrades quan GitHub publica una versió més antiga.</div></div><div class='item'><div class='label'>v1.6.2-auto-version-manifest</div><div class='value'>Manifest amb versió automàtica</div><div class='small'>GitHub Actions llegeix FIRMWARE_VERSION des d'AppConfig.cpp i genera manifest.json amb la mateixa versió, sense hardcodejar-la al workflow.</div></div><div class='item'><div class='label'>v1.6.1-internet-check</div><div class='value'>Actualitzacions automatiques des de GitHub</div><div class='small'>GitHub Actions pot compilar el firmware en cada push, publicar firmware.bin i manifest.json, i la boia pot detectar una build nova i instal·lar-la per Wi-Fi des de la pestanya Manteniment / OTA.</div></div><div class='item'><div class='label'>v1.5.3-ha-history-hourly</div><div class='value'>Històric HA configurable i reduït per hores</div><div class='small'>El gràfic permet triar les últimes hores a mostrar i el proxy de la boia retorna mostres horàries compactes per evitar carregar massa l'ESP32 amb tot l'històric cru de Home Assistant.</div></div><div class='item'><div class='label'>v1.5.2-ha-token-fix</div><div class='value'>Correcció token Home Assistant</div><div class='small'>La configuració de l'API de Home Assistant ara neteja espais, cometes i el prefix Bearer si s'ha enganxat per error. També mostra un missatge més clar quan Home Assistant respon 401.</div></div><div class='item'><div class='label'>v1.5.0-ha-history-ui</div><div class='value'>Històric de temperatura des de Home Assistant</div><div class='small'>La fitxa de temperatura pot dibuixar un gràfic de fons amb l'històric de l'última setmana llegit des de l'API local de Home Assistant. La configuració d'URL, token i entity_id queda dins MQTT / HA.</div></div><div class='item'><div class='label'>v1.4.9-menu-align</div><div class='value'>Alineació visual del menú</div><div class='small'>El menú lateral queda alineat amb la targeta de contingut principal, mantenint el format acordió compacte sota la capçalera.</div></div><div class='item'><div class='label'>v1.4.8-accordion-menu</div><div class='value'>Menú lateral compacte desplegable</div><div class='small'>El menú lateral passa a funcionar com un acordió: les seccions generals despleguen les subopcions només quan cal. També s'alinea el menú just sota la capçalera principal i es redueix l'efecte de scroll lateral.</div></div><div class='item'><div class='label'>v1.4.7-help-center-menu</div><div class='value'>Centre d'ajuda i menú lateral refinat</div><div class='small'>Firmware, Hardware, Futur i ajuda de rescat passen al Centre d'ajuda. Sistema i Manteniment queden més nets. El menú lateral incorpora subdirectoris i s'arregla el fons quan el contingut és curt.</div></div><div class='item'><div class='label'>v1.4.6-left-menu-subpages</div><div class='value'>Menú lateral i subpàgines</div><div class='small'>La navegació principal passa a l'esquerra i les seccions grans funcionen com a subpàgines amb URL pròpia per secció, sense ancoratges.</div></div><div class='item'><div class='label'>v1.4.5-subtabs</div><div class='value'>Subpestanyes internes</div><div class='small'>Les pàgines grans queden ordenades amb subpestanyes: Sistema, Manteniment, Wi-Fi, MQTT i Temperatura tenen navegació interna per seccions.</div></div><div class='item'><div class='label'>v1.4.4-board-leds</div><div class='value'>Control LED intern de placa</div><div class='small'>Opció per activar/desactivar el LED intern de l'ESP32-C6 i usar-lo com a mirall del LED d'estat extern o com a heartbeat local.</div></div><div class='item'><div class='label'>v1.4.3-future-sensors-prep</div><div class='value'>Preparació sensors interns i energia</div><div class='small'>Documentació i reserves per temperatura interna, humitat interna, bateria, placa solar i GPIO d'expansió. Encara no activa sensors nous fins triar hardware concret.</div></div><div class='item'><div class='label'>v1.4.2-tabs-consolidated</div><div class='value'>Pestanyes consolidades</div><div class='small'>Firmware i Hardware passen dins de Sistema. Diagnòstic passa dins de Manteniment. La navegació queda més curta i menys carregada.</div></div>";
  html += "<div class='item'><div class='label'>v1.4.1-recovery-help</div><div class='value'>Ajuda de rescat</div><div class='small'>Documentació visible a la web sobre què fer després d'un reset total: Wi-Fi AP de rescat, IP per defecte i passos de recuperació.</div></div>";
  html += "<div class='item'><div class='label'>v1.4-maintenance-polish</div><div class='value'>Manteniment i poliment</div><div class='small'>Pestanya Manteniment, Hardware separat, export/import de configuració, confirmacions, salut general, qualitat RSSI i publicació manual de telemetria.</div></div>";
  html += "<div class='item'><div class='label'>v1.3-web-facelift</div><div class='value'>Rentat de cara web</div><div class='small'>Nova capçalera professional, pestanyes reals, footer amb versió, estat en directe via WebSocket, nom/hostname configurable, apartat firmware i capacitats ESP32-C6 documentades.</div></div>";
  html += "<div class='item'><div class='label'>v1.2-diagnostics-button-led</div><div class='value'>Diagnòstic físic</div><div class='small'>Pàgina de diagnòstic, botó físic de rescat, LED d'estat i mapa GPIO.</div></div>";
  html += "<div class='item'><div class='label'>v1.1-ota-sensor-network</div><div class='value'>OTA, sonda robusta i xarxa avançada</div><div class='small'>Actualització firmware per web, IP fixa/DHCP i validació de lectures DS18B20.</div></div>";
  html += "<div class='item'><div class='label'>v1.0-ha-controls</div><div class='value'>Controls des de Home Assistant</div><div class='small'>Numbers, switch i botons MQTT Discovery per governar paràmetres bàsics.</div></div>";
  html += "<div class='item'><div class='label'>v0.9-ha-discovery</div><div class='value'>Home Assistant Discovery</div><div class='small'>Dispositiu i entitats MQTT descobertes automàticament.</div></div>";
  html += "<div class='item'><div class='label'>v0.8.x</div><div class='value'>MQTT configurable</div><div class='small'>Broker, usuari, password, topic base i contrasenyes visibles/ocultables amb ull.</div></div>";
  html += "<div class='item'><div class='label'>v0.7</div><div class='value'>Wi-Fi configurable + AP rescat</div><div class='small'>Credencials guardades i mode BOIA-PISCINA-SETUP si falla la connexió.</div></div>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<h2>Rescat després d'un reset total</h2>";
  html += "<p class='hint'>Si fas un reset total, si el Wi-Fi guardat és incorrecte o si la boia no pot connectar a la xarxa principal, entrarà en mode AP de rescat. No és una avaria: és el mode de recuperació.</p>";
  html += "<div class='grid'>";
  html += "<div class='item'><div class='label'>Wi-Fi a buscar</div><div class='value'>";
  html += htmlEscape(String(WIFI_AP_SSID));
  html += "</div><div class='small'>La contrasenya és única per placa: boia- seguida dels 6 últims caràcters de la MAC. També es mostra pel monitor sèrie quan s'activa l'AP.</div></div>";
  html += "<div class='item'><div class='label'>IP per defecte</div><div class='value'>http://192.168.4.1</div><div class='small'>Un cop connectat a l'AP, obre aquesta adreça al navegador per tornar a configurar Wi-Fi, MQTT i identitat.</div></div>";
  html += "<div class='item'><div class='label'>Quan passa?</div><div class='value'>Wi-Fi fallit / AP forçat / reset total</div><div class='small'>També el pots forçar amb el botó físic: 10 s per AP setup, 20 s per reset total + AP setup.</div></div>";
  html += "<div class='item'><div class='label'>Després de recuperar</div><div class='value'>Guardar i reiniciar</div><div class='small'>Configura el Wi-Fi correcte, guarda, reinicia i comprova que torna a sortir a la xarxa normal.</div></div>";
  html += "</div></div>";

  html += "<div class='card'>";
  html += "<h2>Bluetooth i Zigbee</h2>";
  html += "<p class='hint'>El xip ESP32-C6 té Bluetooth LE i ràdio IEEE 802.15.4, que és la base de Zigbee/Thread. Ara mateix no ho activem perquè el firmware ja fa Wi-Fi, web, OTA i MQTT de forma estable. Posar Zigbee no és només encendre un botó: cal una pila Zigbee/Thread, definir rols i integrar-ho bé amb Home Assistant. Ho deixem documentat i reservat, no ho barregem encara amb la boia funcional.</p>";
  html += "<span class='tag'>Bluetooth LE: disponible al xip, no usat</span>";
  html += "<span class='tag'>IEEE 802.15.4: disponible al xip, no usat</span>";
  html += "<span class='tag'>Zigbee/Thread: futur, no activat</span>";
  html += "</div>";
}

static String buildFirmwarePage() {
  String html = "";
  appendPageStart(html, "help", false);
  appendFirmwareSection(html);
  appendPageEnd(html);
  return html;
}


static void appendHardwareSection(String& html) {
  html += "<div class='card'>";
  html += "<h2>Hardware i GPIO</h2>";
  html += "<p class='hint'>Mapa pràctic del cablejat de la boia. Això és documentació viva: si canvies un pin a <b>AppConfig.h</b>, aquesta pàgina canvia amb el mateix valor.</p>";
  html += "<div class='grid'>";

  html += "<div class='item'><div class='label'>DS18B20 DATA</div><div class='value'>GPIO";
  html += String(ONE_WIRE_PIN);
  html += "</div><div class='small'>VCC a 3V3, GND a GND, DATA a GPIO";
  html += String(ONE_WIRE_PIN);
  html += ". Resistència de 4.7 kΩ entre DATA i 3V3. No alimentis la sonda a 5V si DATA entra directe a l'ESP32.</div></div>";

  html += "<div class='item'><div class='label'>Botó físic de rescat</div><div class='value'>";
  html += htmlEscape(hardwareResetButtonPinText());
  html += "</div><div class='small'>Botó entre GPIO";
  html += String(RESET_BUTTON_PIN);
  html += " i GND. Usa INPUT_PULLUP intern. No mantenir premut en alimentar si la placa fa servir aquest pin com BOOT.</div></div>";

  html += "<div class='item'><div class='label'>LED d'estat extern</div><div class='value'>";
  html += htmlEscape(hardwareStatusLedPinText());
  html += "</div><div class='small'>GPIO";
  html += String(STATUS_LED_PIN);
  html += " → resistència 220/330 Ω → LED → GND. Si el LED va invertit, cal ajustar STATUS_LED_ACTIVE_LOW.</div></div>";

  html += "<div class='item'><div class='label'>LED intern placa</div><div class='value'>GPIO";
  html += String(INTERNAL_BOARD_LED_PIN);
  html += "</div><div class='small'>LED RGB intern preparat per ESP32-C6 DevKitC-1. Es pot desactivar o fer servir com a mirall del LED extern des de Sistema → LEDs de placa.</div></div>";
  html += "<div class='item'><div class='label'>microSD SPI</div><div class='value'>GPIO" + String(SD_SPI_CS_PIN) + " / " + String(SD_SPI_MOSI_PIN) + " / " + String(SD_SPI_CLK_PIN) + " / " + String(SD_SPI_MISO_PIN) + "</div><div class='small'>CS GPIO" + String(SD_SPI_CS_PIN) + ", MOSI GPIO" + String(SD_SPI_MOSI_PIN) + ", CLK GPIO" + String(SD_SPI_CLK_PIN) + ", MISO GPIO" + String(SD_SPI_MISO_PIN) + ". Alimenta el mòdul a 3V3 i GND comú.</div></div>";
  html += "<div class='item'><div class='label'>Alimentació</div><div class='value'>5V estable</div><div class='small'>Alimenta la placa per USB/5V segons la teva placa. La DS18B20 millor a 3V3 per evitar nivells de dades de 5V. El mòdul microSD, a 3V3.</div></div>";
  html += "</div></div>";

  html += "<div class='card'>";
  html += "<h2>Funcionament botó</h2>";
  html += "<div class='grid'>";
  appendChecklistItem(html, "Pulsació curta", htmlEscape("Diagnòstic"), "Actualitza estat físic intern. No fa cap acció destructiva.");
  appendChecklistItem(html, "5 segons", htmlEscape("Reinici"), "Reinicia la boia.");
  appendChecklistItem(html, "10 segons", htmlEscape("AP setup"), "Esborra/força mode configuració Wi-Fi i obre l'AP de rescat.");
  appendChecklistItem(html, "20 segons", htmlEscape("Reset total"), "Restaura configuració i força AP setup. Acció bèstia: només si cal.");
  html += "</div></div>";

  html += "<div class='card'>";
  html += "<h2>Recuperació després de reset total</h2>";
  html += "<div class='grid'>";
  appendChecklistItem(html, "1", htmlEscape("Buscar Wi-Fi AP"), String("Connecta't a la xarxa ") + String(WIFI_AP_SSID) + ".");
  appendChecklistItem(html, "2", htmlEscape("Obrir web local"), "Entra a http://192.168.4.1 des del navegador.");
  appendChecklistItem(html, "3", htmlEscape("Reconfigurar"), "Posa SSID/password Wi-Fi, MQTT i nom del dispositiu si cal.");
  appendChecklistItem(html, "4", htmlEscape("Guardar i reiniciar"), "Quan torni a la xarxa normal, busca-la pel hostname o per la IP del router.");
  html += "</div>";
  html += "<p class='hint'>Aquest és el procediment que cal tenir clar abans de tancar la boia. Si no pots arribar a http://192.168.4.1 en mode AP, no la tanquis.</p>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<h2>Patrons LED</h2>";
  html += "<div class='grid'>";
  appendChecklistItem(html, "Fix", htmlEscape("OK"), "Wi-Fi, MQTT i sonda dins de l'estat esperat.");
  appendChecklistItem(html, "Lent", htmlEscape("Wi-Fi"), "La boia busca o ha perdut Wi-Fi.");
  appendChecklistItem(html, "Molt lent", htmlEscape("MQTT"), "MQTT està activat però no connecta.");
  appendChecklistItem(html, "Ràpid", htmlEscape("AP setup"), "Mode AP de rescat actiu.");
  appendChecklistItem(html, "Curt/intermitent", htmlEscape("Sonda"), "Error de lectura o sonda no vàlida.");
  html += "</div></div>";
}


static String futurePinText(int pin) {
  if (pin < 0) return "Reservat / no assignat";
  return String("GPIO") + String(pin);
}

static void appendFutureExpansionSection(String& html) {
  html += "<div class='card'>";
  html += "<h2>Ampliacions futures preparades</h2>";
  html += "<p class='hint'>El sensor ambiental intern SHT41 ja està actiu. Aquesta secció conserva la preparació per a bateria, placa solar i altres ampliacions futures.</p>";
  html += "<div class='grid'>";
  html += "<div class='item'><div class='label'>Temperatura interna</div><div class='value'>SHT41 actiu</div><div class='small'>Mesura la temperatura dins del tub per separat de l'aigua.</div></div>";
  html += "<div class='item'><div class='label'>Humitat interna</div><div class='value'>SHT41 actiu</div><div class='small'>Permet detectar condensació o entrada d'aigua abans que afecti l'electrònica.</div></div>";
  html += "<div class='item'><div class='label'>Bateria</div><div class='value'>Activa GPIO1</div><div class='small'>Lectura de tensió amb divisor 100k/100k, percentatge estimat i publicació MQTT/HA.</div></div>";
  html += "<div class='item'><div class='label'>Placa solar</div><div class='value'>Preparada</div><div class='small'>Futur control de tensió solar, estat de càrrega i diagnòstic de si realment està carregant.</div></div>";
  html += "</div></div>";

  html += "<div class='card'>";
  html += "<h2>GPIO reservats per ampliacions</h2>";
  html += "<p class='hint'>Els pins del bus ambiental ja estan assignats. La bateria ja queda activa a GPIO1; solar i carregador continuen reservats fins definir el hardware final.</p>";
  html += "<div class='grid'>";
  html += "<div class='item'><div class='label'>I2C SDA intern</div><div class='value'>" + htmlEscape(futurePinText(INTERNAL_ENV_I2C_SDA_PIN)) + "</div><div class='small'>SHT41 actiu, adreça 0x44.</div></div>";
  html += "<div class='item'><div class='label'>I2C SCL intern</div><div class='value'>" + htmlEscape(futurePinText(INTERNAL_ENV_I2C_SCL_PIN)) + "</div><div class='small'>Mateix bus I2C intern.</div></div>";
  html += "<div class='item'><div class='label'>ADC bateria</div><div class='value'>GPIO" + String(BATTERY_VOLTAGE_ADC_PIN) + "</div><div class='small'>BAT+ -> 100 kΩ -> GPIO" + String(BATTERY_VOLTAGE_ADC_PIN) + " -> 100 kΩ -> GND. Mesura estimada: " + batteryVoltageText() + " / " + batteryPercentText() + ". Mai posar BAT+ directe a l'ESP32.</div></div>";
  html += "<div class='item'><div class='label'>ADC solar</div><div class='value'>" + htmlEscape(futurePinText(SOLAR_VOLTAGE_ADC_PIN)) + "</div><div class='small'>Per saber si la placa solar està donant tensió útil.</div></div>";
  html += "<div class='item'><div class='label'>Estat carregador</div><div class='value'>" + htmlEscape(futurePinText(CHARGER_STATUS_PIN)) + "</div><div class='small'>Opcional: pin CHG/STDBY/FAULT si el mòdul carregador ho exposa.</div></div>";
  html += "</div></div>";

  html += "<div class='card'>";
  html += "<h2>Arquitectura futura d'energia</h2>";
  html += "<div class='grid'>";
  appendChecklistItem(html, "Bateria", htmlEscape("Li-Ion/LiPo 1S"), "Per defecte assumeix 3.00 V buit i 4.20 V ple, però ara es pot ajustar des de Sistema → Bateria. Si canvies química o tensió, revisa també el divisor.");
  appendChecklistItem(html, "Càrrega solar", htmlEscape("Carregador dedicat"), "La placa solar no ha d'anar directa a la bateria. Necessita mòdul carregador/protecció adequat.");
  appendChecklistItem(html, "Mesura tensió", htmlEscape("Divisor resistiu"), "L'ADC de l'ESP32 no pot rebre tensions altes. Cal divisor i calibratge.");
  appendChecklistItem(html, "Mode energia", htmlEscape("Futur"), "Quan hi hagi bateria real, tindrà sentit deep sleep, intervals més llargs i telemetria d'energia.");
  html += "</div></div>";
}

static String buildHardwarePage() {
  String html = "";
  appendPageStart(html, "help", false);
  appendHardwareSection(html);
  appendPageEnd(html);
  return html;
}

static String buildMaintenancePage() {
  String html = "";
  appendPageStart(html, "maintenance", false);

  static const char* labels[] = {"Salut", "Diagnòstic", "Accions", "OTA", "Backup", "Rescat", "Destructives"};
  static const char* anchors[] = {"mnt-health", "mnt-diagnostics", "mnt-actions", "mnt-ota", "mnt-backup", "mnt-recovery", "mnt-danger"};
  appendSubTabs(html, "Manteniment", labels, anchors, 7);

  html += "<div id='mnt-health' class='card'>";
  html += "<h2>Salut i manteniment ràpid</h2>";
  html += "<div class='grid'>";
  html += "<div class='item'><div class='label'>Salut general</div><div class='value ";
  html += healthClass();
  html += "'>";
  html += healthText();
  html += "</div><div class='small'>";
  html += htmlEscape(healthDetails());
  html += "</div></div>";

  html += "<div class='item'><div class='label'>RSSI Wi-Fi</div><div class='value ";
  html += rssiQualityClass();
  html += "'>";
  html += rssiQualityText();
  html += "</div><div class='small'>";
  html += isWifiConnected() ? String(WiFi.RSSI()) + " dBm" : String("Sense connexió STA");
  html += "</div></div>";

  html += "<div class='item'><div class='label'>Última publicació MQTT</div><div class='value'>";
  html += String(appState.mqttPublishCount);
  html += "</div><div class='small'>Errors MQTT: ";
  html += String(appState.mqttFailCount);
  html += " · Topic base: ";
  html += htmlEscape(configMqttTopicBase);
  html += "</div></div>";
  html += "<div class='item'><div class='label'>Mode dispositiu</div><div class='value'>";
  html += configProductionMode ? "Producció" : "Desenvolupament";
  html += "</div><div class='small'>Producció = menys invents i més confirmacions. Desenvolupament = banc de proves.</div></div>";
  html += "</div></div>";

  html += "<div id='mnt-diagnostics'>";
  appendDiagnosticsSection(html);
  html += "</div>";

  html += "<div id='mnt-actions' class='card'>";
  html += "<h2>Accions ràpides</h2>";
  html += "<div class='buttons'>";
  html += "<form method='POST' action='/mqtt-publish-now' data-confirm='Publicar telemetria ara per MQTT?'><button type='submit'>Publicar telemetria ara</button></form>";
  html += "<form method='POST' action='/mqtt-discovery' data-confirm='Republicar Discovery de Home Assistant ara?'><button class='secondary' type='submit'>Republicar Discovery</button></form>";
  html += "<form method='POST' action='/restart' data-confirm='Segur que vols reiniciar la boia?'><button class='danger' type='submit'>Reiniciar boia</button></form>";
  html += "</div>";
  html += "</div>";

  html += "<div id='mnt-ota' class='card'>";
  html += "<h2>Actualització OTA</h2>";
  html += "<p class='hint'>Puja el fitxer <b>firmware.bin</b> generat per PlatformIO. No tanquis la pestanya durant la pujada.</p>";
  html += "<div class='item'><div class='label'>Últim estat OTA</div><div class='value ";
  html += appState.otaSuccess ? "ok" : (appState.otaInProgress ? "warn" : "");
  html += "'>";
  html += htmlEscape(appState.otaLastMessage);
  html += "</div></div>";
  html += "<form id='ota-local-form' method='POST' action='";
  html += htmlEscape(localOtaUploadPath);
  html += "' enctype='multipart/form-data' data-confirm='Pujar firmware nou? Si el fitxer és incorrecte pots deixar la boia malament.'>";
  html += "<input id='ota-local-file' type='file' name='firmware' accept='.bin'>";
  html += "<button type='submit'>Actualitzar firmware</button>";
  html += "</form>";

  html += "<hr>";
  html += "<h3>Actualitzacio des de GitHub</h3>";
  html += "<p class='hint'>Comprova si GitHub Actions ha publicat una build nova i instal·la-la per Wi-Fi. Ideal per actualitzar la boia flotant sense obrir-la.</p>";

  html += "<div class='ota-badges'>";
  html += "<span class='ota-badge "; html += internetClass(); html += "'>🌍 Internet: "; html += htmlEscape(appState.internetCheckDone ? appState.internetCheckMessage : String("pendent")); html += "</span>";
  html += "<span class='ota-badge "; html += githubClass(); html += "'>🐙 GitHub: "; html += htmlEscape(appState.githubUpdateChecked ? (appState.githubUpdateOk ? String("connectat") : String("error")) : String("pendent")); html += "</span>";
  html += "<span class='ota-badge "; html += otaUpdateClass(); html += "'>⬆️ "; html += htmlEscape(appState.githubUpdateMessage); html += "</span>";
  html += "</div>";

  html += "<div class='ota-hero'>";
  html += "<div class='ota-tile info'><div class='ota-title'>Build actual</div><div class='ota-main info'>";
  html += htmlEscape(String(FIRMWARE_VERSION));
  html += "</div><div class='ota-meta'>SHA local: ";
  html += htmlEscape(shortBuildSha(currentFirmwareBuildSha()));
  html += "<br>Compilat: ";
#ifdef FIRMWARE_BUILD_DATE
  html += htmlEscape(String(FIRMWARE_BUILD_DATE));
#else
  html += "local";
#endif
  html += "</div></div>";

  html += "<div id='ota-internet-tile' class='ota-tile "; html += internetClass(); html += "'><div class='ota-title'>Accés a Internet</div><div id='ota-internet-main' class='ota-main "; html += internetClass(); html += "'>";
  html += htmlEscape(appState.internetCheckDone ? appState.internetCheckMessage : String("Comprovant..."));
  html += "</div><div id='ota-internet-meta' class='ota-meta'>";
  html += htmlEscape(appState.internetCheckDetails.length() ? appState.internetCheckDetails : String("Comprovació automàtica en entrar a la pàgina. També pots forçar-la amb el botó."));
  if (appState.internetResolvedIp.length()) { html += "<br>DNS GitHub: "; html += htmlEscape(appState.internetResolvedIp); }
  if (appState.internetCheckDone) { html += "<br>Última prova: "; html += elapsedText(appState.internetLastCheckMillis); }
  html += "</div></div>";

  html += "<div id='ota-github-tile' class='ota-tile "; html += githubClass(); html += "'><div class='ota-title'>GitHub / manifest</div><div id='ota-github-main' class='ota-main "; html += githubClass(); html += "'>";
  html += htmlEscape(appState.githubUpdateChecked ? (appState.githubUpdateOk ? String("Manifest llegit") : String("Manifest fallit")) : String("Comprovant..."));
  html += "</div><div class='ota-meta'>Remota: <span id='ota-github-version'>";
  html += htmlEscape(appState.githubUpdateVersion.length() ? appState.githubUpdateVersion : String("--"));
  html += "</span><br>SHA: <span id='ota-github-sha'>";
  html += htmlEscape(appState.githubUpdateSha.length() ? shortBuildSha(appState.githubUpdateSha) : String("--"));
  html += "</span><br>Build remota: <span id='ota-github-date'>";
  html += htmlEscape(appState.githubUpdateDate.length() ? appState.githubUpdateDate : String("--"));
  html += "</span>";
  if (appState.githubUpdateChecked) { html += "<br>Última prova: "; html += elapsedText(appState.githubLastCheckMillis); }
  html += "</div></div>";

  html += "<div id='ota-update-tile' class='ota-tile "; html += otaUpdateClass(); html += "'><div class='ota-title'>Actualització</div><div id='ota-update-main' class='ota-main "; html += otaUpdateClass(); html += "'>";
  html += htmlEscape(appState.githubUpdateMessage);
  html += "</div><div id='ota-update-details' class='ota-meta'>";
  html += htmlEscape(appState.githubUpdateDetails.length() ? appState.githubUpdateDetails : String("La pàgina comprovarà Internet i GitHub automàticament."));
  if (appState.githubRemoteOlder) html += "<br>No faré downgrade excepte si ho permets explícitament.";
  html += "</div></div>";
  html += "</div>";

  bool showOtaProgress = appState.otaInProgress || (appState.otaProgressSource != "cap" && appState.otaProgressPhase != "espera");
  html += "<div id='ota-progress-card' class='ota-progress-card";
  if (!showOtaProgress) html += " hidden";
  html += "'>";
  html += "<div class='ota-progress-dialog' role='dialog' aria-modal='true' aria-labelledby='ota-progress-title'>";
  html += "<div class='ota-progress-head'><div><div id='ota-progress-title' class='ota-progress-title'>Progrés d'actualització</div><div id='ota-progress-message' class='ota-progress-text'>";
  html += htmlEscape(appState.otaLastMessage);
  html += "</div></div><div class='ota-progress-text'><b id='ota-progress-percent'>";
  html += String(appState.otaProgressPercent);
  html += "%</b><br><span id='ota-progress-bytes'>";
  html += String(appState.otaProgressBytes);
  html += " / ";
  html += String(appState.otaProgressTotal);
  html += " bytes</span></div></div>";
  html += "<div class='progress-track'><div id='ota-progress-fill' class='progress-fill";
  if (appState.otaInProgress && appState.otaProgressPercent == 0) html += " indeterminate";
  html += "' style='width:";
  html += String(appState.otaProgressPercent);
  html += "%'></div></div>";
  html += "<div class='ota-progress-text' style='margin-top:8px'>Fase: <span id='ota-progress-phase'>";
  html += htmlEscape(appState.otaProgressSource + " · " + appState.otaProgressPhase);
  html += "</span></div>";
  html += "<div class='ota-log-head'><span>Log OTA en directe</span><span class='ota-progress-text'>Només durant una actualització</span></div>";
  html += "<pre id='ota-log' class='ota-log'>";
  html += htmlEscape(appState.otaLog);
  html += "</pre>";
  html += "<button class='secondary ota-dismiss' type='button' onclick='dismissOtaProgress()'>Tancar aquest resultat</button>";
  html += "</div></div>";

  html += "<div class='buttons'>";
  html += "<form method='POST' action='/internet-check'><button class='secondary' type='submit'>🌍 Comprovar Internet</button></form>";
  html += "<form method='POST' action='/github-check-update'><button class='secondary' type='submit'>🐙 Comprovar GitHub</button></form>";
  html += "<form id='github-install-form' method='POST' action='/github-update' data-confirm='Descarregar i instal·lar firmware des de GitHub? No tallis alimentació.'><button id='github-install-button' type='submit' ";
  if (!appState.githubUpdateAvailable && !(appState.githubRemoteSameVersion && configGithubAllowSameVersionUpdate)) html += "disabled";
  html += ">⬆️ Instal·lar actualització</button></form>";
  html += "</div>";

  html += "<div class='ota-config'>";
  html += "<h3>Configuracio GitHub OTA</h3>";
  html += "<form method='POST' action='/github-ota-config'>";
  html += "<label><input type='checkbox' name='github_ota_enabled' value='1' ";
  html += configGithubOtaEnabled ? "checked" : "";
  html += "> Activar actualitzacions des de GitHub</label>";
  html += "<label>Manifest GitHub</label>";
  html += "<input type='url' name='github_manifest_url' value='";
  html += htmlEscape(configGithubManifestUrl);
  html += "'>";
  html += "<div class='small'>Ha de ser una URL raw. Exemple: https://raw.githubusercontent.com/pequestick/Pequestick-ESP32-Boia_Piscina/main/firmware/manifest.json</div>";
  html += "<label><input type='checkbox' name='github_allow_same' value='1' ";
  html += configGithubAllowSameVersionUpdate ? "checked" : "";
  html += "> Permetre reinstal·lar la mateixa versió (els downgrades continuen bloquejats)</label>";
  html += "<button type='submit'>Guardar GitHub OTA</button>";
  html += "</form>";
  html += "</div>";

  html += "</div>";

  html += "<div id='mnt-backup' class='card'>";
  html += "<h2>Exportar configuració</h2>";
  html += "<p class='hint'>Per defecte no inclou contrasenyes. Si marques l'opció, el JSON queda sensible: guarda'l bé i no el passis alegrement.</p>";
  html += "<div class='buttons'>";
  html += "<form method='GET' action='/config-export'><button type='submit'>Exportar sense passwords</button></form>";
  html += "<form method='GET' action='/config-export' data-confirm='Exportar amb contrasenyes? Aquest fitxer serà sensible.'><input type='hidden' name='passwords' value='1'><button class='secondary' type='submit'>Exportar amb passwords</button></form>";
  html += "</div>";
  html += "<pre>";
  html += htmlEscape(buildConfigExportJson(false));
  html += "</pre>";
  html += "</div>";

  html += "<div class='card subpage-extra' data-parent='mnt-backup'>";
  html += "<h2>Importar configuració JSON</h2>";
  html += "<p class='hint'>Enganxa un JSON exportat per aquesta boia. L'importador és simple: aplica els camps coneguts i ignora la resta. No és per fer invents estranys.</p>";
  html += "<form method='POST' action='/config-import' data-confirm='Importar aquesta configuració? Es reconfigurarà Wi-Fi/MQTT i pots perdre connexió si has posat dades incorrectes.'>";
  html += "<textarea name='config_json' placeholder='{ ... }'></textarea>";
  html += "<button type='submit'>Importar configuració</button>";
  html += "</form>";
  html += "</div>";

  html += "<div id='mnt-recovery' class='card'>";
  html += "<h2>Rescat si et quedes sense accés</h2>";
  html += "<p class='hint'>Si després d'un canvi de xarxa, reset Wi-Fi o reset total la boia no apareix a la LAN, busca l'AP de rescat i entra per la IP per defecte.</p>";
  html += "<div class='grid'>";
  html += "<div class='item'><div class='label'>Wi-Fi de rescat</div><div class='value'>";
  html += htmlEscape(String(WIFI_AP_SSID));
  html += "</div><div class='small'>La boia crea aquesta xarxa quan no pot connectar o quan forces AP setup.</div></div>";
  html += "<div class='item'><div class='label'>Web de rescat</div><div class='value'>http://192.168.4.1</div><div class='small'>Connecta't primer a l'AP de rescat; si no, aquesta IP no respondrà.</div></div>";
  html += "<div class='item'><div class='label'>Botó físic</div><div class='value'>10 s AP · 20 s reset total</div><div class='small'>10 segons força AP setup. 20 segons restaura configuració i força AP setup.</div></div>";
  html += "</div></div>";

  html += "<div id='mnt-danger' class='card'>";
  html += "<h2>Accions destructives</h2>";
  html += "<div class='buttons'>";
  html += "<form method='POST' action='/wifi-network-reset' data-confirm='Restaurar DHCP? Es reiniciarà la connexió Wi-Fi.'><button class='secondary' type='submit'>Restaurar DHCP</button></form>";
  html += "<form method='POST' action='/wifi-reset' data-confirm='Restaurar Wi-Fi per defecte? Pots perdre connexió.'><button class='secondary' type='submit'>Reset Wi-Fi</button></form>";
  html += "<form method='POST' action='/defaults' data-confirm='Restaurar valors de temperatura i sonda?'><button class='secondary' type='submit'>Reset temperatura</button></form>";
  html += "</div>";
  html += "</div>";

  appendPageEnd(html);
  return html;
}

static String buildSystemPage() {
  String html = "";
  appendPageStart(html, "system", false);

  static const char* labels[] = {"Resum", "Interior boia", "Bateria", "Identitat", "Mode", "LEDs", "Usuaris"};
  static const char* anchors[] = {"sys-summary", "sys-internal-env", "sys-battery", "sys-identity", "sys-mode", "sys-leds", "sys-users"};
  appendSubTabs(html, "Sistema", labels, anchors, 7);
  bool tempAlarm = configInternalEnvAlarmEnabled && !isnan(appState.lastInternalTemperatureC) && appState.lastInternalTemperatureC >= configInternalTempAlarmC;
  bool humidityAlarm = configInternalEnvAlarmEnabled && !isnan(appState.lastInternalHumidityPercent) && appState.lastInternalHumidityPercent >= configInternalHumidityAlarmPercent;

  html += "<div id='sys-summary' class='card'>";
  html += "<h2>Sistema</h2>";
  html += "<div class='grid'>";

  html += "<div class='item'><div class='label'>Firmware</div><div class='value'>";
  html += FIRMWARE_VERSION;
  html += "</div></div>";

  html += "<div class='item'><div class='label'>Device ID</div><div class='value'>";
  html += DEVICE_ID;
  html += "</div></div>";

  html += "<div class='item'><div class='label'>HA Device ID</div><div class='value'>";
  html += htmlEscape(configHaDeviceId);
  html += "</div></div>";

  html += "<div class='item'><div class='label'>HA Device Name</div><div class='value'>";
  html += htmlEscape(configHaDeviceName);
  html += "</div></div>";

  html += "<div class='item'><div class='label'>Uptime</div><div class='value'>";
  html += String(getUptimeSeconds());
  html += " s</div></div>";

  html += "<div class='item'><div class='label'>GPIO sonda</div><div class='value'>GPIO";
  html += String(ONE_WIRE_PIN);
  html += "</div></div>";

  html += "<div class='item'><div class='label'>MQTT publicacions</div><div class='value'>";
  html += String(appState.mqttPublishCount);
  html += "</div></div>";

  html += "<div class='item'><div class='label'>MQTT errors</div><div class='value ";
  html += appState.mqttFailCount == 0 ? "ok" : "bad";
  html += "'>";
  html += String(appState.mqttFailCount);
  html += "</div></div>";

  html += "<div class='item'><div class='label'>Ambient interior de la boia / alarmes</div><div class='value ";
  html += (tempAlarm || humidityAlarm) ? "bad" : "ok";
  html += "'>";
  if (tempAlarm || humidityAlarm) html += "ALARMA ACTIVA";
  else if (!configInternalEnvAlarmEnabled) html += "Alarmes desactivades";
  else html += "Configurat";
  html += "</div><div class='small'>Llindars: " + String(configInternalTempAlarmC, 1) + " °C i " + String(configInternalHumidityAlarmPercent, 1) + " % HR.</div>";
  html += "<div class='buttons' style='margin-top:10px'><a class='action-link' href='/system?section=sys-internal-env'>Veure interior de la boia i configurar alarmes</a></div></div>";

  html += "<div class='item'><div class='label'>Bateria</div><div class='value ";
  html += appState.batteryStatus == "LOW" ? "warn" : (appState.batteryStatus == "ERROR" ? "bad" : "ok");
  html += "'>" + batteryPercentText() + "</div><div class='small'>" + batteryVoltageText() + " · Buit: " + floatText(configBatteryEmptyVoltage, 2) + " V · Ple: " + floatText(configBatteryFullVoltage, 2) + " V.</div>";
  html += "<div class='buttons' style='margin-top:10px'><a class='action-link' href='/system?section=sys-battery'>Configurar bateria</a></div></div>";

  html += "</div>";
  html += "</div>";

  html += "<div id='sys-internal-env' class='card'><h2>Temperatura i humitat interior de la boia · SHT41</h2>";
  html += "<p class='hint'>El SHT41 mesura la temperatura i la humitat de l'aire dins la carcassa de la boia. Serveix per detectar sobreescalfament de l'electrònica i possibles entrades d'aigua o condensació abans que provoquin una avaria.</p>";
  html += "<div class='item'><div class='label'>Alarmes de l'ambient interior de la boia</div><div class='small'>Aquestes alarmes no corresponen a la temperatura de l'aigua. La sobretemperatura s'activa quan l'aire dins la carcassa arriba al llindar configurat; la humitat alta indica condensació o una possible entrada d'aigua. L'estat, els llindars i l'activació es publiquen com a entitats pròpies a Home Assistant.</div></div>";
  html += "<h3>Lectura actual</h3><div class='grid'>";
  html += "<div class='item'><div class='label'>Temperatura de l'aire interior de la boia</div><div class='value ";
  html += tempAlarm ? "bad" : "ok";
  html += "'>" + (isnan(appState.lastInternalTemperatureC) ? String("Sense dades") : formatTemperature(appState.lastInternalTemperatureC, 2) + " °C") + "</div></div>";
  html += "<div class='item'><div class='label'>Humitat de l'aire interior de la boia</div><div class='value ";
  html += humidityAlarm ? "bad" : "ok";
  html += "'>" + (isnan(appState.lastInternalHumidityPercent) ? String("Sense dades") : formatTemperature(appState.lastInternalHumidityPercent, 1) + " %") + "</div></div>";
  html += "<div class='item'><div class='label'>Estat sensor</div><div class='value'>" + htmlEscape(appState.internalEnvStatus) + "</div><div class='small'>" + htmlEscape(appState.internalEnvLastError) + "</div></div>";
  html += "<div class='item'><div class='label'>Bus I2C</div><div class='value'>0x44</div><div class='small'>SDA GPIO6 · SCL GPIO7</div></div></div>";
  html += "<h3>Històric de l'ambient interior</h3>";
  appendStatisticsHistoryPanel(html, "internal-temp-history-chart", "Temperatura interior de la boia", configHaInternalTemperatureEntityId);
  appendStatisticsHistoryPanel(html, "internal-humidity-history-chart", "Humitat interior de la boia", configHaInternalHumidityEntityId);
  html += "<h3>Configuració de valors i alarmes</h3><form method='POST' action='/internal-env-alarm'><div><label><input name='alarm_enabled' type='checkbox' value='1' ";
  html += configInternalEnvAlarmEnabled ? "checked" : "";
  html += ">Activar alarmes de temperatura i humitat interior de la boia</label></div><div class='grid'>";
  html += "<div><div class='label'>Sobretemperatura interior de la boia (°C)</div><input name='temp_alarm_c' type='number' min='-20' max='85' step='0.1' value='" + String(configInternalTempAlarmC, 1) + "' required></div>";
  html += "<div><div class='label'>Humitat interior alta de la boia (%)</div><input name='humidity_alarm_percent' type='number' min='1' max='100' step='0.1' value='" + String(configInternalHumidityAlarmPercent, 1) + "' required></div></div>";
  html += "<div class='buttons'><button type='submit'>Guardar alarmes</button></div></form></div>";

  html += "<div id='sys-battery' class='card'><h2>Bateria · GPIO1</h2>";
  html += "<p class='hint'>La lectura surt del divisor 100k/100k: BAT+ → 100 kΩ → GPIO" + String(BATTERY_VOLTAGE_ADC_PIN) + " → 100 kΩ → GND. El percentatge és lineal entre el voltatge buit i el voltatge ple que configuris aquí.</p>";
  html += "<div class='grid'>";
  html += "<div class='item'><div class='label'>Percentatge actual</div><div class='value ";
  html += appState.batteryStatus == "LOW" ? "warn" : (appState.batteryStatus == "ERROR" ? "bad" : "ok");
  html += "'>" + batteryPercentText() + "</div><div class='small'>Estat: " + htmlEscape(batteryStatusText()) + "</div></div>";
  html += "<div class='item'><div class='label'>Voltatge calculat</div><div class='value'>" + batteryVoltageText() + "</div><div class='small'>ADC: ";
  html += (isnan(appState.lastBatteryAdcMilliVolts) ? String("sense dades") : floatText(appState.lastBatteryAdcMilliVolts, 0) + " mV");
  html += " · divisor x" + floatText(BATTERY_DIVIDER_RATIO, 2) + "</div></div>";
  html += "<div class='item'><div class='label'>Escala actual</div><div class='value'>" + floatText(configBatteryEmptyVoltage, 2) + " V → " + floatText(configBatteryFullVoltage, 2) + " V</div><div class='small'>Avís LOW per sota de " + floatText(configBatteryLowPercent, 0) + " %. Calibratge ADC: x" + floatText(configBatteryCalibrationFactor, 3) + ".</div></div>";
  html += "<div class='item'><div class='label'>Lectures bateria</div><div class='value'>" + String(appState.batteryValidReads) + "/" + String(appState.batteryTotalReads) + "</div><div class='small'>Últim error: " + htmlEscape(appState.batteryLastError) + "</div></div>";
  html += "</div>";
  if (configHaBatteryEntityId.length() > 0) {
    html += "<h3>Històric de bateria a Home Assistant</h3>";
    appendStatisticsHistoryPanel(html, "battery-history-chart", "Bateria de la boia", configHaBatteryEntityId);
  } else {
    html += "<div class='item' style='margin-top:12px'><div class='label'>Històric de bateria</div><div class='small'>Configura l'Entity ID de bateria a MQTT / HA → Home Assistant per veure el gràfic aquí.</div></div>";
  }
  html += "<h3>Configuració volts bateria</h3>";
  html += "<form method='POST' action='/battery-config'><div class='grid'>";
  html += "<div><div class='label'>Voltatge bateria buida (0 %)</div><input name='battery_empty_voltage' type='number' min='" + floatText(MIN_BATTERY_EMPTY_VOLTAGE, 2) + "' max='" + floatText(MAX_BATTERY_EMPTY_VOLTAGE, 2) + "' step='0.01' value='" + floatText(configBatteryEmptyVoltage, 2) + "' required></div>";
  html += "<div><div class='label'>Voltatge bateria plena (100 %)</div><input name='battery_full_voltage' type='number' min='" + floatText(MIN_BATTERY_FULL_VOLTAGE, 2) + "' max='" + floatText(MAX_BATTERY_FULL_VOLTAGE, 2) + "' step='0.01' value='" + floatText(configBatteryFullVoltage, 2) + "' required></div>";
  html += "<div><div class='label'>Avís bateria baixa (%)</div><input name='battery_low_percent' type='number' min='" + floatText(MIN_BATTERY_LOW_PERCENT, 0) + "' max='" + floatText(MAX_BATTERY_LOW_PERCENT, 0) + "' step='1' value='" + floatText(configBatteryLowPercent, 0) + "' required></div>";
  html += "<div><div class='label'>Factor calibratge ADC</div><input name='battery_calibration_factor' type='number' min='" + floatText(MIN_BATTERY_CALIBRATION_FACTOR, 2) + "' max='" + floatText(MAX_BATTERY_CALIBRATION_FACTOR, 2) + "' step='0.001' value='" + floatText(configBatteryCalibrationFactor, 3) + "' required></div>";
  html += "</div><p class='small'>Si el multímetre diu 3.80 V i la web diu 3.60 V, posa calibratge aproximat 3.80 / 3.60 = 1.056. No facis trampes: 3.02 V en una Li-Ion 1S és pràcticament buida, encara que l'ESP32 aguanti uns minuts més.</p>";
  html += "<div class='buttons'><button type='submit'>Guardar bateria</button></div></form>";
  html += "<form method='POST' action='/battery-config-reset' data-confirm='Restaurar valors de bateria per defecte?'><button class='secondary' type='submit'>Restaurar valors bateria</button></form>";
  html += "</div>";

  html += "<div id='sys-identity' class='card'>";
  html += "<h2>Identitat del dispositiu</h2>";
  html += "<p class='hint'>Aquest és el nom humà del dispositiu i el hostname que anunciarà a la xarxa. El hostname ha de ser simple: minúscules, números i guions.</p>";
  html += "<form method='POST' action='/identity'>";
  html += "<div class='grid'>";
  html += "<div><div class='label'>Nom dispositiu</div><input name='device_name' type='text' maxlength='48' value='";
  html += htmlEscape(configDeviceName);
  html += "'></div>";
  html += "<div><div class='label'>Hostname xarxa</div><input name='device_hostname' type='text' maxlength='31' value='";
  html += htmlEscape(configDeviceHostname);
  html += "'></div>";
  html += "</div>";
  html += "<div class='buttons'><button type='submit'>Guardar identitat</button></div>";
  html += "</form>";
  html += "<p class='small'>Per veure el hostname nou a la xarxa, normalment cal reiniciar la connexió Wi-Fi o reiniciar la boia.</p>";
  html += "</div>";

  html += "<div id='sys-mode' class='card'>";
  html += "<h2>Mode del dispositiu</h2>";
  html += "<p class='hint'>Desenvolupament és per banc de proves. Producció és per quan ja la tens muntada i vols menys marge de cagada.</p>";
  html += "<form method='POST' action='/device-mode'>";
  html += "<div><label><input name='production_mode' type='checkbox' value='1' ";
  html += configProductionMode ? "checked" : "";
  html += ">Mode producció</label></div>";
  html += "<div class='buttons'><button type='submit'>Guardar mode</button></div>";
  html += "</form>";
  html += "</div>";

  html += "<div id='sys-leds' class='card'>";
  html += "<h2>LEDs de placa</h2>";
  html += "<p class='hint'>Control del LED intern de la placa ESP32-C6. No substitueix el LED extern recomanat, però va bé per proves i diagnòstic ràpid sense soldar res.</p>";
  html += "<form method='POST' action='/board-leds'>";
  html += "<div class='grid'>";
  html += "<div class='item'><div class='label'>LED intern detectat/preparat</div><div class='value'>GPIO";
  html += String(INTERNAL_BOARD_LED_PIN);
  html += "</div><div class='small'>A molts ESP32-C6 DevKitC-1 és un RGB intern a GPIO8. Si no funciona en la teva placa, deixa'l desactivat.</div></div>";
  html += "<div class='item'><div class='label'>Estat actual</div><div class='value'>";
  html += htmlEscape(hardwareBoardLedStatusText());
  html += "</div><div class='small'>Mirall = copia Wi-Fi/MQTT/AP/error sonda igual que el LED extern. Heartbeat = parpelleig lent només per saber que la placa és viva.</div></div>";
  html += "</div>";
  html += "<div><label><input name='board_led_enabled' type='checkbox' value='1' ";
  html += configBoardLedEnabled ? "checked" : "";
  html += ">Activar LED intern de placa</label></div>";
  html += "<div><label><input name='board_led_mirror' type='checkbox' value='1' ";
  html += configBoardLedMirrorStatus ? "checked" : "";
  html += ">Usar LED intern igual que el LED extern d'estat</label></div>";
  html += "<div class='buttons'><button type='submit'>Guardar LEDs</button></div>";
  html += "</form>";
  html += "</div>";

  html += "<div id='sys-users' class='card'>";
  html += "<h2>Gestió d'usuaris</h2>";
  html += "<p class='hint'>Canvia l'usuari administrador i la contrasenya d'accés a la web. La contrasenya es desa com un hash salat a Preferences, no com a text llegible.</p>";
  html += "<div class='item'><div class='label'>Usuari actual</div><div class='value'>";
  html += htmlEscape(webAuthUsername());
  html += "</div><div class='small'>Només hi ha un administrador local.</div></div>";
  html += "<form method='POST' action='/user-credentials'>";
  html += "<div><div class='label'>Usuari nou</div><input name='new_username' type='text' minlength='3' maxlength='32' value='";
  html += htmlEscape(webAuthUsername());
  html += "' required></div>";
  html += "<div><div class='label'>Contrasenya actual</div><input name='current_password' type='password' maxlength='64' required autocomplete='current-password'></div>";
  html += "<div><div class='label'>Contrasenya nova</div><input name='new_password' type='password' minlength='8' maxlength='64' required autocomplete='new-password'></div>";
  html += "<div><div class='label'>Repeteix la contrasenya nova</div><input name='confirm_password' type='password' minlength='8' maxlength='64' required autocomplete='new-password'></div>";
  html += "<div class='buttons'><button type='submit'>Actualitzar credencials</button></div>";
  html += "</form>";
  html += "</div>";


  appendPageEnd(html);
  return html;
}

static String buildHelpPage() {
  String html = "";
  appendPageStart(html, "help", false);

  static const char* labels[] = {"Firmware", "Hardware / GPIO", "Ampliacions futures", "Rescat"};
  static const char* anchors[] = {"help-firmware", "help-hardware", "help-future", "help-recovery"};
  appendSubTabs(html, "Centre d'ajuda", labels, anchors, 4);

  html += "<div id='help-firmware'>";
  appendFirmwareSection(html);
  html += "</div>";

  html += "<div id='help-hardware'>";
  appendHardwareSection(html);
  html += "</div>";

  html += "<div id='help-future'>";
  appendFutureExpansionSection(html);
  html += "</div>";

  html += "<div id='help-recovery' class='card'>";
  html += "<h2>Recuperació i reset total</h2>";
  html += "<p class='hint'>Si la boia queda fora de la teva xarxa després d'un canvi de Wi-Fi, IP fixa mal posada o reset total, no està morta: entra en mode AP de rescat.</p>";
  html += "<div class='grid'>";
  html += "<div class='item'><div class='label'>Wi-Fi de rescat</div><div class='value'>";
  html += htmlEscape(String(WIFI_AP_SSID));
  html += "</div><div class='small'>Busca aquesta xarxa des del mòbil o portàtil.</div></div>";
  html += "<div class='item'><div class='label'>IP per defecte</div><div class='value'>http://192.168.4.1</div><div class='small'>Només respon quan estàs connectat a l'AP de rescat.</div></div>";
  html += "<div class='item'><div class='label'>Botó físic</div><div class='value'>10 s AP · 20 s reset total</div><div class='small'>10 segons força AP setup. 20 segons restaura configuració i força AP setup.</div></div>";
  html += "</div>";
  html += "<h3>Passos ràpids</h3>";
  html += "<p class='hint'>1) Connecta't al Wi-Fi de rescat. 2) Obre http://192.168.4.1. 3) Ves a Wi-Fi / Credencials. 4) Posa el Wi-Fi correcte i guarda. 5) Espera que torni a aparèixer a la LAN.</p>";
  html += "</div>";

  appendPageEnd(html);
  return html;
}

static String buildSavedPage(const String& title, const String& message, bool restartSoon) {
  String html = "";

  appendHtmlHeader(html, title, false);

  if (restartSoon) {
    html.replace("</head>", "<meta http-equiv='refresh' content='8;url=/'></head>");
  } else {
    html.replace("</head>", "<meta http-equiv='refresh' content='2;url=/'></head>");
  }

  html += "<div class='card'>";
  html += "<h1>";
  html += htmlEscape(title);
  html += "</h1>";
  html += "<p>";
  html += htmlEscape(message);
  html += "</p>";
  html += "<p><a href='/'>Tornar a la boia</a></p>";
  html += "</div>";

  appendPageEnd(html);
  return html;
}

static String buildAuthPage(
  const String& title,
  const String& message,
  bool changeCredentials,
  bool forcedChange
) {
  String html = "";
  appendHtmlHeader(html, title, false);
  html += "<div style='max-width:520px;margin:8vh auto 0'>";
  html += "<div class='card'><h1>" + htmlEscape(title) + "</h1>";
  if (message.length()) {
    html += "<p class='";
    html += message.startsWith("Credencials") ? "bad" : "hint";
    html += "'>" + htmlEscape(message) + "</p>";
  }

  if (changeCredentials) {
    if (forcedChange) {
      html += "<p class='warn'>És el primer accés. Has de substituir admin / 1234 abans de continuar.</p>";
    }
    html += "<form method='POST' action='/change-password'>";
    html += "<div><div class='label'>Usuari nou</div><input name='new_username' type='text' minlength='3' maxlength='32' value='";
    html += htmlEscape(webAuthUsername());
    html += "' required autocomplete='username'></div>";
    html += "<div><div class='label'>Contrasenya actual</div><input name='current_password' type='password' maxlength='64' required autocomplete='current-password'></div>";
    html += "<div><div class='label'>Contrasenya nova</div><input name='new_password' type='password' minlength='8' maxlength='64' required autocomplete='new-password'></div>";
    html += "<div><div class='label'>Repeteix la contrasenya nova</div><input name='confirm_password' type='password' minlength='8' maxlength='64' required autocomplete='new-password'></div>";
    html += "<button type='submit'>Guardar credencials noves</button></form>";
  } else {
    html += "<form method='POST' action='/login'>";
    html += "<div><div class='label'>Usuari</div><input name='username' type='text' maxlength='32' required autofocus autocomplete='username'></div>";
    html += "<div><div class='label'>Contrasenya</div><input name='password' type='password' maxlength='64' required autocomplete='current-password'></div>";
    html += "<button type='submit'>Entrar</button></form>";
  }
  html += "</div>";

  html += "<div class='card'>";
  html += "<h2>Lectures públiques</h2>";
  html += "<p class='small'>Consulta ràpida sense iniciar sessió. La configuració i els controls continuen protegits.</p>";
  html += "<div class='grid'>";
  html += "<div class='item'><div class='label'>Temperatura de l'aigua</div><div class='value'>";
  html += isnan(appState.lastValidTemperatureC) ? "Sense dades" : formatTemperature(appState.lastValidTemperatureC, configTemperatureDecimals) + " °C";
  html += "</div><div class='small'>Última lectura: ";
  html += htmlEscape(elapsedText(appState.lastReadMillis));
  html += "</div></div>";
  html += "<div class='item'><div class='label'>Sonda d'aigua</div><div class='value ";
  html += appState.sensorStatus == "OK" ? "ok" : (appState.sensorStatus == "ERROR" ? "bad" : "warn");
  html += "'>" + htmlEscape(appState.sensorStatus) + "</div><div class='small'>DS18B20</div></div>";
  html += "<div class='item'><div class='label'>Temperatura interior</div><div class='value'>";
  html += isnan(appState.lastInternalTemperatureC) ? "Sense dades" : formatTemperature(appState.lastInternalTemperatureC, 2) + " °C";
  html += "</div><div class='small'>SHT41 · estat " + htmlEscape(appState.internalEnvStatus) + "</div></div>";
  html += "<div class='item'><div class='label'>Humitat interior</div><div class='value'>";
  html += isnan(appState.lastInternalHumidityPercent) ? "Sense dades" : formatTemperature(appState.lastInternalHumidityPercent, 1) + " %";
  html += "</div><div class='small'>SHT41 a 0x44</div></div>";
  html += "<div class='item'><div class='label'>Bateria</div><div class='value'>";
  html += batteryPercentText();
  html += "</div><div class='small'>";
  html += batteryVoltageText();
  html += " · ";
  html += htmlEscape(batteryStatusText());
  html += "</div></div>";
  html += "</div></div>";
  html += "</div></div></body></html>";
  return html;
}

static void redirectTo(const String& location) {
  server.sendHeader("Location", location, true);
  server.send(303, "text/plain", "");
}

static String webSessionCookie(const String& token) {
  return "boia_session_v2=" + token + "; Path=/; Max-Age=604800; HttpOnly; SameSite=Lax";
}

static bool requestHasValidSession() {
  return isWebSessionCookieValid(server.header("Cookie"));
}

static bool requestOriginIsAllowed() {
  String origin = server.header("Origin");
  origin.trim();
  if (origin.length() == 0) return true;

  // Alguns WebView i portals captius mòbils envien un origen opac. La cookie
  // de sessió vàlida continua sent obligatòria abans d'arribar aquí.
  if (origin == "null") return true;

  bool https = origin.startsWith("https://");
  if (!https && !origin.startsWith("http://")) return false;

  String originAuthority = origin.substring(https ? 8 : 7);
  while (originAuthority.endsWith("/")) {
    originAuthority.remove(originAuthority.length() - 1);
  }

  String requestAuthority = server.hostHeader();
  requestAuthority.trim();
  if ((!https && originAuthority.endsWith(":80")) || (https && originAuthority.endsWith(":443"))) {
    originAuthority.remove(originAuthority.lastIndexOf(':'));
  }
  if ((!https && requestAuthority.endsWith(":80")) || (https && requestAuthority.endsWith(":443"))) {
    requestAuthority.remove(requestAuthority.lastIndexOf(':'));
  }

  return originAuthority.equalsIgnoreCase(requestAuthority);
}

static bool authMiddleware(WebServer& currentServer, Middleware::Callback next) {
  String uri = currentServer.uri();
  currentServer.sendHeader("Cache-Control", "no-store");
  currentServer.sendHeader("X-Content-Type-Options", "nosniff");
  currentServer.sendHeader("X-Frame-Options", "DENY");
  currentServer.sendHeader("Referrer-Policy", "no-referrer");

  if (uri == "/login") return next();
  if (uri == "/logout") return next();

  if (!requestHasValidSession()) {
    String cookieHeader = currentServer.header("Cookie");
    Serial.print("Sessio web no valida a ");
    Serial.print(uri);
    Serial.print(" · cookie v2 rebuda: ");
    Serial.println(cookieHeader.indexOf("boia_session_v2=") >= 0 ? "si" : "no");
    redirectTo("/login");
    return false;
  }

  if (webAuthPasswordChangeRequired() && uri != "/change-password") {
    redirectTo("/change-password");
    return false;
  }

  bool credentialChange = uri == "/change-password" || uri == "/user-credentials";
  if (currentServer.method() != HTTP_GET && !credentialChange && !requestOriginIsAllowed()) {
    Serial.print("Peticio rebutjada. Origin: ");
    Serial.print(currentServer.header("Origin"));
    Serial.print(" · Host: ");
    Serial.println(currentServer.hostHeader());
    currentServer.send(403, "text/plain; charset=utf-8", "Origen de petició no permès");
    return false;
  }

  return next();
}

// ==========================
// HANDLERS
// ==========================

static void handleLoginGet() {
  if (requestHasValidSession()) {
    redirectTo(webAuthPasswordChangeRequired() ? "/change-password" : "/");
    return;
  }
  server.send(200, "text/html", buildAuthPage("Accés administratiu", "", false, false));
}

static void handleLoginPost() {
  if (lastFailedLoginMillis != 0 && millis() - lastFailedLoginMillis < 1500UL) {
    server.send(429, "text/html", buildAuthPage("Accés administratiu", "Espera un moment abans de tornar-ho a provar.", false, false));
    return;
  }

  String username = server.arg("username");
  username.trim();
  String password = server.arg("password");
  if (!authenticateWebUser(username, password)) {
    lastFailedLoginMillis = millis();
    delay(250);
    server.send(401, "text/html", buildAuthPage("Accés administratiu", "Credencials incorrectes.", false, false));
    return;
  }

  lastFailedLoginMillis = 0;
  String token = createWebSession();
  server.sendHeader("Set-Cookie", webSessionCookie(token));
  redirectTo(webAuthPasswordChangeRequired() ? "/change-password" : "/");
}

static void handleLogout() {
  clearWebSession();
  server.sendHeader("Set-Cookie", "boia_session_v2=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax");
  server.sendHeader("Set-Cookie", "boia_session=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax");
  redirectTo("/login");
}

static void handleChangePasswordGet() {
  server.send(200, "text/html", buildAuthPage("Canviar credencials", "", true, webAuthPasswordChangeRequired()));
}

static void handleChangePasswordPost() {
  String newPassword = server.arg("new_password");
  if (newPassword != server.arg("confirm_password")) {
    server.send(400, "text/html", buildAuthPage("Canviar credencials", "Les contrasenyes noves no coincideixen.", true, webAuthPasswordChangeRequired()));
    return;
  }

  String message;
  if (!changeWebCredentials(server.arg("current_password"), server.arg("new_username"), newPassword, message)) {
    server.send(400, "text/html", buildAuthPage("Canviar credencials", message, true, webAuthPasswordChangeRequired()));
    return;
  }

  String token = createWebSession();
  server.sendHeader("Set-Cookie", webSessionCookie(token));
  redirectTo("/");
}

static void handleUserCredentialsPost() {
  handleChangePasswordPost();
}

static void handleRoot() {
  server.send(200, "text/html", buildStatusPage());
}

static void handleStorageGet() {
  server.send(200, "text/html", buildStoragePage());
}

static void handleSdInfoGet() {
  refreshSdInfo();
  server.send(200, "application/json", sdInfoJson());
}

static String contentTypeForPath(const String& path) {
  String lower = path;
  lower.toLowerCase();
  if (lower.endsWith(".csv")) return "text/csv";
  if (lower.endsWith(".json")) return "application/json";
  if (lower.endsWith(".jsonl")) return "application/x-ndjson";
  if (lower.endsWith(".log")) return "text/plain";
  if (lower.endsWith(".txt")) return "text/plain";
  return "application/octet-stream";
}

static String filenameForDownload(const String& path, const String& fallback) {
  int slash = path.lastIndexOf('/');
  String name = slash >= 0 ? path.substring(slash + 1) : path;
  if (name.length() == 0) name = fallback;
  return name;
}

static void streamSdFilePath(const String& requestedPath, const String& fallbackName, bool attachment) {
  if (!isSdMounted()) {
    server.send(404, "text/plain", "SD no muntada");
    return;
  }

  String clean;
  if (!normalizeSdPath(requestedPath, clean)) {
    server.send(400, "text/plain", "Ruta no valida");
    return;
  }

  File file = SD.open(clean.c_str(), FILE_READ);
  if (!file || file.isDirectory()) {
    if (file) file.close();
    server.send(404, "text/plain", "Fitxer no trobat");
    return;
  }

  if (attachment) {
    server.sendHeader("Content-Disposition", String("attachment; filename=") + filenameForDownload(clean, fallbackName));
  }
  server.streamFile(file, contentTypeForPath(clean));
  file.close();
}

static void handleSdHistoryCsvGet() {
  if (!ensureSdHistoryFile()) {
    server.send(500, "text/plain", "No puc preparar el fitxer historic diari");
    return;
  }
  streamSdFilePath(sdHistoryPathText(), "boia_history_today.csv", true);
}

static void handleSdDailyStatsCsvGet() {
  streamSdFilePath(sdDailyStatsPathText(), "boia_daily_stats.csv", true);
}

static void handleSdListGet() {
  String path = server.arg("path");
  if (path.length() == 0) path = SD_BASE_DIR;
  server.send(200, "application/json", sdDirectoryListingJson(path));
}

static void handleSdViewGet() {
  String path = server.arg("path");
  server.send(200, "text/html", buildSdFileViewPage(path));
}

static void handleSdDownloadGet() {
  String path = server.arg("path");
  streamSdFilePath(path, "boia_sd_file.dat", true);
}

static void handleSdReadGet() {
  if (!isSdMounted()) {
    server.send(404, "application/json", "{\"error\":\"SD no muntada\"}");
    return;
  }

  String path = server.arg("path");
  String clean;
  if (!normalizeSdPath(path, clean)) {
    server.send(400, "application/json", "{\"error\":\"Ruta no valida\"}");
    return;
  }

  File file = SD.open(clean.c_str(), FILE_READ);
  if (!file) {
    server.send(404, "application/json", "{\"error\":\"Fitxer no trobat\"}");
    return;
  }
  if (file.isDirectory()) {
    file.close();
    server.send(400, "application/json", "{\"error\":\"La ruta es un directori\"}");
    return;
  }
  uint64_t fileSize = file.size();
  file.close();

  bool truncated = false;
  String content = sdReadTextFileLimited(clean, 32768, truncated);
  String json = "{\"path\":\"";
  json += jsonEscape(clean);
  json += "\",\"size\":";
  json += String((unsigned long)fileSize);
  json += ",\"truncated\":";
  json += truncated ? "true" : "false";
  json += ",\"content\":\"";
  json += jsonEscape(content);
  json += "\"}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

static void handleSdPendingMqttGet() {
  streamSdFilePath(sdPendingMqttPathText(), "boia_mqtt_pending.jsonl", true);
}

static void handleSdFormatPost() {
  bool ok = logicalFormatSdCard();
  server.send(
    ok ? 200 : 500,
    "text/html",
    buildSavedPage(
      ok ? "SD netejada" : "Error netejant SD",
      ok ? "S'han esborrat els fitxers de la microSD i s'ha recreat l'estructura /boia amb historic, stats, logs, blackbox i buffer MQTT." : sdLastErrorText(),
      false
    )
  );
}

static void handleConfigGet() {
  server.send(200, "text/html", buildConfigPage());
}

static void handleWifiGet() {
  server.send(200, "text/html", buildWifiPage());
}

static void handleWifiScanGet() {
  int16_t count = WiFi.scanNetworks(false, true);
  if (count < 0) {
    WiFi.scanDelete();
    server.send(500, "application/json", "{\"error\":\"Error intern escanejant xarxes Wi-Fi\"}");
    return;
  }

  String json = "{\"networks\":[";
  bool first = true;
  for (int16_t i = 0; i < count; ++i) {
    String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;

    bool duplicate = false;
    for (int16_t j = 0; j < i; ++j) {
      if (WiFi.SSID(j) == ssid) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) continue;

    if (!first) json += ',';
    first = false;
    json += "{\"ssid\":\"" + jsonEscape(ssid) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ',';
    json += "\"secure\":";
    json += WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true";
    json += '}';
  }
  json += "]}";
  WiFi.scanDelete();
  server.send(200, "application/json", json);
}

static void handleMqttGet() {
  server.send(200, "text/html", buildMqttPage());
}

static void handleSystemGet() {
  server.send(200, "text/html", buildSystemPage());
}

static void handleHelpGet() {
  server.send(200, "text/html", buildHelpPage());
}

static void handleMaintenanceGet() {
  server.send(200, "text/html", buildMaintenancePage());
}

static void handleHardwareGet() {
  server.send(200, "text/html", buildHardwarePage());
}

static void handleFirmwareGet() {
  server.send(200, "text/html", buildFirmwarePage());
}

static void handleDiagnosticsGet() {
  server.send(200, "text/html", buildDiagnosticsPage());
}

static void handleConfigPost() {
  if (server.hasArg("read_interval")) {
    uint16_t value = server.arg("read_interval").toInt();
    configReadIntervalSeconds = clampUint16(
      value,
      MIN_READ_INTERVAL_SECONDS,
      MAX_READ_INTERVAL_SECONDS
    );
  }

  if (server.hasArg("decimals")) {
    uint8_t value = server.arg("decimals").toInt();
    configTemperatureDecimals = clampUint8(
      value,
      MIN_TEMPERATURE_DECIMALS,
      MAX_TEMPERATURE_DECIMALS
    );
  }

  float offsetC = server.hasArg("temperature_offset") ? server.arg("temperature_offset").toFloat() : configTemperatureOffsetC;
  float minValidC = server.hasArg("min_valid_temp") ? server.arg("min_valid_temp").toFloat() : configMinValidTempC;
  float maxValidC = server.hasArg("max_valid_temp") ? server.arg("max_valid_temp").toFloat() : configMaxValidTempC;

  if (minValidC >= maxValidC) {
    server.send(
      400,
      "text/html",
      buildSavedPage("Rang invalid", "La temperatura minima valida ha de ser inferior a la maxima.", false)
    );
    return;
  }

  saveConfig();
  saveSensorConfig(offsetC, minValidC, maxValidC);
  appState.mqttConfigStatePublishRequested = true;

  Serial.println();
  Serial.println("Configuracio de temperatura i sonda guardada des de la web:");
  Serial.print("Interval lectura: ");
  Serial.println(configReadIntervalSeconds);
  Serial.print("Decimals: ");
  Serial.println(configTemperatureDecimals);
  Serial.print("Offset: ");
  Serial.println(configTemperatureOffsetC);
  Serial.print("Rang valid: ");
  Serial.print(configMinValidTempC);
  Serial.print(" - ");
  Serial.println(configMaxValidTempC);

  server.send(
    200,
    "text/html",
    buildSavedPage("Temperatura guardada", "Els nous valors ja estan actius i han quedat guardats a la memoria.", false)
  );
}


static void handleIdentityPost() {
  String deviceName = server.hasArg("device_name") ? server.arg("device_name") : DEVICE_NAME;
  String hostname = server.hasArg("device_hostname") ? server.arg("device_hostname") : DEFAULT_DEVICE_HOSTNAME;

  saveDeviceIdentity(deviceName, hostname);
  appState.mqttDiscoveryPublished = false;
  appState.mqttConfigStatePublishRequested = true;

  server.send(
    200,
    "text/html",
    buildSavedPage("Identitat guardada", "S'ha guardat el nom i el hostname. Reinicio la connexio Wi-Fi perquè el hostname nou es vegi a la xarxa.", false)
  );

  delay(800);
  restartWifiWithCurrentConfig();
  reconfigureMqtt();
}

static void handleDeviceModePost() {
  bool productionMode = server.hasArg("production_mode");
  saveDeviceMode(productionMode);

  server.send(
    200,
    "text/html",
    buildSavedPage("Mode guardat", productionMode ? "Mode produccio activat." : "Mode desenvolupament activat.", false)
  );
}

static void handleBoardLedsPost() {
  bool enabled = server.hasArg("board_led_enabled");
  bool mirror = server.hasArg("board_led_mirror");
  saveBoardLedConfig(enabled, mirror);

  server.send(
    200,
    "text/html",
    buildSavedPage("LEDs guardats", enabled ? (mirror ? "LED intern activat com a mirall del LED d'estat." : "LED intern activat en mode heartbeat.") : "LED intern desactivat.", false)
  );
}

static void handleInternalEnvAlarmPost() {
  bool enabled = server.hasArg("alarm_enabled");
  float temperatureC = server.hasArg("temp_alarm_c") ? server.arg("temp_alarm_c").toFloat() : DEFAULT_INTERNAL_TEMP_ALARM_C;
  float humidityPercent = server.hasArg("humidity_alarm_percent") ? server.arg("humidity_alarm_percent").toFloat() : DEFAULT_INTERNAL_HUMIDITY_ALARM_PERCENT;
  saveInternalEnvAlarmConfig(enabled, temperatureC, humidityPercent);
  appState.lastMqttPublishMillis = 0;

  server.send(
    200,
    "text/html",
    buildSavedPage("Alarmes de l'interior de la boia guardades", enabled ? "Els llindars de temperatura i humitat interior de la boia ja estan actius i es publicaran a Home Assistant." : "Les alarmes de l'ambient interior de la boia queden desactivades.", false)
  );
}

static void handleBatteryConfigPost() {
  float emptyVoltage = server.hasArg("battery_empty_voltage") ? server.arg("battery_empty_voltage").toFloat() : DEFAULT_BATTERY_EMPTY_VOLTAGE;
  float fullVoltage = server.hasArg("battery_full_voltage") ? server.arg("battery_full_voltage").toFloat() : DEFAULT_BATTERY_FULL_VOLTAGE;
  float lowPercent = server.hasArg("battery_low_percent") ? server.arg("battery_low_percent").toFloat() : DEFAULT_BATTERY_LOW_PERCENT;
  float calibrationFactor = server.hasArg("battery_calibration_factor") ? server.arg("battery_calibration_factor").toFloat() : DEFAULT_BATTERY_CALIBRATION_FACTOR;

  saveBatteryConfig(emptyVoltage, fullVoltage, lowPercent, calibrationFactor);
  performBatteryRead();
  appState.lastMqttPublishMillis = 0;

  server.send(
    200,
    "text/html",
    buildSavedPage("Configuracio de bateria guardada", "La boia ja recalcula el percentatge amb els nous volts de referencia i el nou factor ADC.", false)
  );
}

static void handleBatteryConfigResetPost() {
  resetBatteryConfigToDefaults();
  performBatteryRead();
  appState.lastMqttPublishMillis = 0;

  server.send(
    200,
    "text/html",
    buildSavedPage("Bateria restaurada", "S'han restaurat els valors per defecte de bateria: 3.00 V buit, 4.20 V ple, LOW al 15 % i calibratge x1.000.", false)
  );
}

static void handleMqttPublishNowPost() {
  if (!configMqttEnabled || !isMqttConnected()) {
    server.send(400, "text/html", buildSavedPage("MQTT no connectat", "No puc publicar telemetria perquè MQTT no esta connectat.", false));
    return;
  }

  publishMqttTelemetry();
  server.send(200, "text/html", buildSavedPage("Telemetria publicada", "S'ha publicat la telemetria MQTT manualment.", false));
}

static void handleConfigExport() {
  bool includePasswords = server.hasArg("passwords") && server.arg("passwords") == "1";
  server.sendHeader("Content-Disposition", includePasswords ? "attachment; filename=boia-config-sensitive.json" : "attachment; filename=boia-config.json");
  server.send(200, "application/json", buildConfigExportJson(includePasswords));
}

static void handleConfigImportPost() {
  String json = server.hasArg("config_json") ? server.arg("config_json") : "";
  json.trim();
  if (json.length() < 5) {
    server.send(400, "text/html", buildSavedPage("JSON buit", "No has enganxat cap configuracio valida.", false));
    return;
  }

  String sv;
  bool bv;
  uint16_t uv;
  float fv;

  String deviceName = configDeviceName;
  String hostname = configDeviceHostname;
  if (extractJsonStringValue(json, "device_name", sv)) deviceName = sv;
  if (extractJsonStringValue(json, "device_hostname", sv)) hostname = sv;
  saveDeviceIdentity(deviceName, hostname);

  if (extractJsonBoolValue(json, "production_mode", bv)) saveDeviceMode(bv);

  bool boardLedEnabled = configBoardLedEnabled;
  bool boardLedMirror = configBoardLedMirrorStatus;
  if (extractJsonBoolValue(json, "board_led_enabled", bv)) boardLedEnabled = bv;
  if (extractJsonBoolValue(json, "board_led_mirror_status", bv)) boardLedMirror = bv;
  saveBoardLedConfig(boardLedEnabled, boardLedMirror);

  String wifiSsid = configWifiSsid;
  String wifiPassword = configWifiPassword;
  if (extractJsonStringValue(json, "wifi_ssid", sv)) wifiSsid = sv;
  if (extractJsonStringValue(json, "wifi_password", sv)) wifiPassword = sv;

  bool useStatic = configWifiUseStaticIp;
  String staticIp = configWifiStaticIp;
  String gateway = configWifiGateway;
  String subnet = configWifiSubnet;
  String dns1 = configWifiDns1;
  String dns2 = configWifiDns2;
  if (extractJsonBoolValue(json, "wifi_use_static_ip", bv)) useStatic = bv;
  if (extractJsonStringValue(json, "wifi_static_ip", sv)) staticIp = sv;
  if (extractJsonStringValue(json, "wifi_gateway", sv)) gateway = sv;
  if (extractJsonStringValue(json, "wifi_subnet", sv)) subnet = sv;
  if (extractJsonStringValue(json, "wifi_dns1", sv)) dns1 = sv;
  if (extractJsonStringValue(json, "wifi_dns2", sv)) dns2 = sv;
  if (wifiSsid.length() > 0) saveWifiConfig(wifiSsid, wifiPassword);
  saveNetworkConfig(useStatic, staticIp, gateway, subnet, dns1, dns2);

  bool mqttEnabled = configMqttEnabled;
  String mqttHost = configMqttHost;
  uint16_t mqttPort = configMqttPort;
  String mqttUser = configMqttUser;
  String mqttPassword = configMqttPassword;
  String mqttBase = configMqttTopicBase;
  uint16_t mqttInterval = configMqttPublishIntervalSeconds;
  if (extractJsonBoolValue(json, "mqtt_enabled", bv)) mqttEnabled = bv;
  if (extractJsonStringValue(json, "mqtt_host", sv)) mqttHost = sv;
  if (extractJsonUInt16Value(json, "mqtt_port", uv)) mqttPort = uv;
  if (extractJsonStringValue(json, "mqtt_user", sv)) mqttUser = sv;
  if (extractJsonStringValue(json, "mqtt_password", sv)) mqttPassword = sv;
  if (extractJsonStringValue(json, "mqtt_topic_base", sv)) mqttBase = sv;
  if (extractJsonUInt16Value(json, "mqtt_publish_interval_seconds", uv)) mqttInterval = uv;
  saveMqttConfig(mqttEnabled, mqttHost, mqttPort, mqttUser, mqttPassword, mqttBase, mqttInterval);

  bool haEnabled = configHaDiscoveryEnabled;
  String haPrefix = configHaDiscoveryPrefix;
  String haDeviceId = configHaDeviceId;
  String haDeviceName = configHaDeviceName;
  if (extractJsonBoolValue(json, "ha_discovery_enabled", bv)) haEnabled = bv;
  if (extractJsonStringValue(json, "ha_discovery_prefix", sv)) haPrefix = sv;
  if (extractJsonStringValue(json, "ha_device_id", sv)) haDeviceId = sv;
  if (extractJsonStringValue(json, "ha_device_name", sv)) haDeviceName = sv;
  saveHomeAssistantConfig(haEnabled, haPrefix, haDeviceId, haDeviceName);

  bool haApiEnabled = configHaApiEnabled;
  String haApiUrl = configHaApiUrl;
  String haApiToken = configHaApiToken;
  String haHistoryEntity = configHaHistoryEntityId;
  if (extractJsonBoolValue(json, "ha_api_enabled", bv)) haApiEnabled = bv;
  if (extractJsonStringValue(json, "ha_api_url", sv)) haApiUrl = sv;
  if (extractJsonStringValue(json, "ha_api_token", sv)) haApiToken = sv;
  if (extractJsonStringValue(json, "ha_history_entity_id", sv)) haHistoryEntity = sv;
  String haInternalTemperatureEntity = configHaInternalTemperatureEntityId;
  String haInternalHumidityEntity = configHaInternalHumidityEntityId;
  String haBatteryEntity = configHaBatteryEntityId;
  if (extractJsonStringValue(json, "ha_internal_temperature_entity_id", sv)) haInternalTemperatureEntity = sv;
  if (extractJsonStringValue(json, "ha_internal_humidity_entity_id", sv)) haInternalHumidityEntity = sv;
  if (extractJsonStringValue(json, "ha_battery_entity_id", sv)) haBatteryEntity = sv;
  uint16_t haHistoryHours = configHaHistoryHours;
  if (extractJsonUInt16Value(json, "ha_history_hours", uv)) haHistoryHours = uv;
  saveHomeAssistantApiConfig(haApiEnabled, haApiUrl, haApiToken, haHistoryEntity, haHistoryHours);
  saveHomeAssistantStatisticsEntities(haInternalTemperatureEntity, haInternalHumidityEntity, haBatteryEntity);

  bool githubEnabled = configGithubOtaEnabled;
  String githubManifest = configGithubManifestUrl;
  bool githubSame = configGithubAllowSameVersionUpdate;
  if (extractJsonBoolValue(json, "github_ota_enabled", bv)) githubEnabled = bv;
  if (extractJsonStringValue(json, "github_manifest_url", sv)) githubManifest = sv;
  if (extractJsonBoolValue(json, "github_allow_same_version_update", bv)) githubSame = bv;
  saveGithubOtaConfig(githubEnabled, githubManifest, githubSame);

  if (extractJsonUInt16Value(json, "read_interval_seconds", uv)) configReadIntervalSeconds = clampUint16(uv, MIN_READ_INTERVAL_SECONDS, MAX_READ_INTERVAL_SECONDS);
  if (extractJsonUInt16Value(json, "temperature_decimals", uv)) configTemperatureDecimals = clampUint8((uint8_t)uv, MIN_TEMPERATURE_DECIMALS, MAX_TEMPERATURE_DECIMALS);
  saveConfig();

  float offset = configTemperatureOffsetC;
  float minT = configMinValidTempC;
  float maxT = configMaxValidTempC;
  if (extractJsonFloatValue(json, "temperature_offset_c", fv)) offset = fv;
  if (extractJsonFloatValue(json, "min_valid_temp_c", fv)) minT = fv;
  if (extractJsonFloatValue(json, "max_valid_temp_c", fv)) maxT = fv;
  saveSensorConfig(offset, minT, maxT);

  float batteryEmpty = configBatteryEmptyVoltage;
  float batteryFull = configBatteryFullVoltage;
  float batteryLow = configBatteryLowPercent;
  float batteryCal = configBatteryCalibrationFactor;
  if (extractJsonFloatValue(json, "battery_empty_voltage", fv)) batteryEmpty = fv;
  if (extractJsonFloatValue(json, "battery_full_voltage", fv)) batteryFull = fv;
  if (extractJsonFloatValue(json, "battery_low_percent", fv)) batteryLow = fv;
  if (extractJsonFloatValue(json, "battery_calibration_factor", fv)) batteryCal = fv;
  saveBatteryConfig(batteryEmpty, batteryFull, batteryLow, batteryCal);

  appState.mqttDiscoveryPublished = false;
  appState.mqttConfigStatePublishRequested = true;
  reconfigureMqtt();

  server.send(200, "text/html", buildSavedPage("Configuracio importada", "S'han aplicat els camps reconeguts. Reinicio la connexio Wi-Fi perquè hostname/xarxa quedin aplicats.", false));
  delay(800);
  restartWifiWithCurrentConfig();
}

static void handleWifiPost() {
  String ssid = server.hasArg("ssid") ? server.arg("ssid") : "";
  String password = server.hasArg("password") ? server.arg("password") : "";
  if (password.length() == 0) password = configWifiPassword;
  ssid.trim();

  if (ssid.length() == 0) {
    server.send(
      400,
      "text/html",
      buildSavedPage("SSID invalid", "No pots guardar un SSID buit. Aixo deixaria la boia sense xarxa configurada.", false)
    );
    return;
  }

  const bool wifiNetworkChanged = ssid != configWifiSsid;

  saveWifiConfig(ssid, password);
  if (wifiNetworkChanged) {
    saveNetworkConfig(false, configWifiStaticIp, configWifiGateway, configWifiSubnet, configWifiDns1, configWifiDns2);
  }

  Serial.println();
  Serial.println("Configuracio Wi-Fi guardada des de la web:");
  Serial.print("SSID: ");
  Serial.println(configWifiSsid);
  Serial.print("Tipus IP: ");
  Serial.println(configWifiUseStaticIp ? "Fixa" : "DHCP");

  String savedMessage = "La boia reiniciara la connexio Wi-Fi. Si no connecta, obrira l'AP de rescat.";
  if (wifiNetworkChanged) {
    savedMessage = "La xarxa Wi-Fi ha canviat i s'ha activat DHCP per seguretat. Si no connecta, la boia obrira l'AP de rescat.";
  }

  server.send(
    200,
    "text/html",
    buildSavedPage("Wi-Fi guardat", savedMessage, false)
  );

  delay(800);
  restartWifiWithCurrentConfig();
}

static void handleWifiNetworkPost() {
  bool useStaticIp = server.hasArg("use_static_ip");
  String staticIp = server.hasArg("static_ip") ? server.arg("static_ip") : DEFAULT_WIFI_STATIC_IP;
  String gateway = server.hasArg("gateway") ? server.arg("gateway") : DEFAULT_WIFI_GATEWAY;
  String subnet = server.hasArg("subnet") ? server.arg("subnet") : DEFAULT_WIFI_SUBNET;
  String dns1 = server.hasArg("dns1") ? server.arg("dns1") : DEFAULT_WIFI_DNS1;
  String dns2 = server.hasArg("dns2") ? server.arg("dns2") : DEFAULT_WIFI_DNS2;

  if (useStaticIp && (!isValidIpString(staticIp) || !isValidIpString(gateway) || !isValidIpString(subnet))) {
    server.send(
      400,
      "text/html",
      buildSavedPage("IP fixa invalida", "Per activar IP fixa cal informar IP, gateway i subnet valids. No ho guardo per no deixar la boia penjada.", false)
    );
    return;
  }

  saveNetworkConfig(useStaticIp, staticIp, gateway, subnet, dns1, dns2);
  server.send(
    200,
    "text/html",
    buildSavedPage("Xarxa guardada", useStaticIp ? "S'ha guardat la configuracio IP fixa i es reiniciara la connexio." : "S'ha activat DHCP i es reiniciara la connexio.", false)
  );

  delay(800);
  restartWifiWithCurrentConfig();
}

static void handleWifiResetPost() {
  resetWifiConfigToDefaults();

  Serial.println();
  Serial.println("Wi-Fi restaurat a valors per defecte.");

  server.send(
    200,
    "text/html",
    buildSavedPage("Wi-Fi restaurat", "S'ha restaurat el Wi-Fi per defecte i es reiniciara la connexio.", false)
  );

  delay(800);
  restartWifiWithCurrentConfig();
}

static void handleWifiNetworkResetPost() {
  resetNetworkConfigToDhcp();

  Serial.println();
  Serial.println("Xarxa restaurada a DHCP.");

  server.send(
    200,
    "text/html",
    buildSavedPage("DHCP restaurat", "La xarxa torna a DHCP i es reiniciara la connexio Wi-Fi.", false)
  );

  delay(800);
  restartWifiWithCurrentConfig();
}


static String urlEncode(const String& value) {
  const char* hex = "0123456789ABCDEF";
  String out = "";
  for (uint16_t i = 0; i < value.length(); i++) {
    char c = value.charAt(i);
    bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
    if (safe) {
      out += c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0x0F];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

static String haHistoryEndpointUrl(const String& startIso, const String& endIso) {
  String base = normalizedHaApiUrl(configHaApiUrl);
  String url = base + "/api/history/period/" + urlEncode(startIso);
  url += "?filter_entity_id=" + urlEncode(configHaHistoryEntityId);
  if (endIso.length() > 0) url += "&end_time=" + urlEncode(endIso);
  url += "&minimal_response&no_attributes&significant_changes_only";
  return url;
}


static bool looksNumericState(const String& value) {
  if (value.length() == 0) return false;
  char c = value.charAt(0);
  return (c >= '0' && c <= '9') || c == '-' || c == '+';
}

static void appendHistoryPoint(String& out, bool& first, const String& iso, float value) {
  if (iso.length() == 0 || isnan(value)) return;
  if (!first) out += ",";
  first = false;
  out += "{\"t\":\"";
  out += jsonEscape(iso);
  out += "\",\"v\":";
  out += String(value, 3);
  out += "}";
}

static String buildCompactHourlyHistoryJson(const String& payload, uint16_t hours) {
  String out = "{\"hours\":";
  out += String(hours);
  out += ",\"sample\":\"hourly\",\"points\":[";

  const String stateMarker = "\"state\":\"";
  const String changedMarker = "\"last_changed\":\"";
  String pendingHour = "";
  String pendingIso = "";
  float pendingValue = NAN;
  bool hasPending = false;
  bool first = true;
  int pos = 0;

  while (true) {
    int st = payload.indexOf(stateMarker, pos);
    if (st < 0) break;
    int svStart = st + stateMarker.length();
    int svEnd = payload.indexOf('"', svStart);
    if (svEnd < 0) break;

    String state = payload.substring(svStart, svEnd);
    int objEnd = payload.indexOf('}', svEnd);
    int lc = payload.indexOf(changedMarker, svEnd);
    if (lc < 0 || (objEnd >= 0 && lc > objEnd)) {
      pos = svEnd + 1;
      continue;
    }

    int tsStart = lc + changedMarker.length();
    int tsEnd = payload.indexOf('"', tsStart);
    if (tsEnd < 0) break;

    if (looksNumericState(state)) {
      float value = state.toFloat();
      String iso = payload.substring(tsStart, tsEnd);
      String hourKey = iso.length() >= 13 ? iso.substring(0, 13) : iso;

      if (!hasPending) {
        pendingHour = hourKey;
        pendingIso = iso;
        pendingValue = value;
        hasPending = true;
      } else if (hourKey == pendingHour) {
        pendingIso = iso;
        pendingValue = value;
      } else {
        appendHistoryPoint(out, first, pendingIso, pendingValue);
        pendingHour = hourKey;
        pendingIso = iso;
        pendingValue = value;
      }
    }

    pos = tsEnd + 1;
  }

  if (hasPending) appendHistoryPoint(out, first, pendingIso, pendingValue);
  out += "]}";
  return out;
}

static String buildCompactHourlyHistoryJsonFromStream(HTTPClient& http, uint16_t hours, bool& receivedBody) {
  String out = "{\"hours\":" + String(hours) + ",\"sample\":\"hourly\",\"points\":[";
  String object;
  object.reserve(768);
  String pendingHour;
  String pendingIso;
  float pendingValue = NAN;
  bool hasPending = false;
  bool first = true;
  bool inString = false;
  bool escaped = false;
  int depth = 0;
  unsigned long lastDataMillis = millis();
  NetworkClient* stream = http.getStreamPtr();
  receivedBody = false;

  if (!stream) return out + "]}";

  while (http.connected() || stream->available() > 0) {
    int available = stream->available();
    if (available <= 0) {
      if (millis() - lastDataMillis > 20000UL) break;
      delay(1);
      continue;
    }

    while (available-- > 0) {
      char c = (char)stream->read();
      receivedBody = true;
      lastDataMillis = millis();

      if (depth > 0 && object.length() < 4096) object += c;

      if (inString) {
        if (escaped) escaped = false;
        else if (c == '\\') escaped = true;
        else if (c == '"') inString = false;
        continue;
      }

      if (c == '"') {
        inString = true;
      } else if (c == '{') {
        if (depth == 0) {
          object = "{";
        }
        depth++;
      } else if (c == '}' && depth > 0) {
        depth--;
        if (depth == 0) {
          const String stateMarker = "\"state\":\"";
          const String changedMarker = "\"last_changed\":\"";
          int statePos = object.indexOf(stateMarker);
          int changedPos = object.indexOf(changedMarker);
          if (statePos >= 0 && changedPos >= 0) {
            int stateStart = statePos + stateMarker.length();
            int stateEnd = object.indexOf('"', stateStart);
            int changedStart = changedPos + changedMarker.length();
            int changedEnd = object.indexOf('"', changedStart);
            if (stateEnd > stateStart && changedEnd > changedStart) {
              String state = object.substring(stateStart, stateEnd);
              if (looksNumericState(state)) {
                String iso = object.substring(changedStart, changedEnd);
                String hourKey = iso.length() >= 13 ? iso.substring(0, 13) : iso;
                float value = state.toFloat();
                if (!hasPending) {
                  pendingHour = hourKey;
                  pendingIso = iso;
                  pendingValue = value;
                  hasPending = true;
                } else if (hourKey == pendingHour) {
                  pendingIso = iso;
                  pendingValue = value;
                } else {
                  appendHistoryPoint(out, first, pendingIso, pendingValue);
                  pendingHour = hourKey;
                  pendingIso = iso;
                  pendingValue = value;
                }
              }
            }
          }
          object = "";
        }
      }
    }
    yield();
  }

  if (hasPending) appendHistoryPoint(out, first, pendingIso, pendingValue);
  out += "]}";
  return out;
}

static void handleHaHistorySettingsPost() {
  uint16_t hours = server.hasArg("ha_history_hours") ? (uint16_t)server.arg("ha_history_hours").toInt() : configHaHistoryHours;
  saveHomeAssistantApiConfig(configHaApiEnabled, configHaApiUrl, configHaApiToken, configHaHistoryEntityId, hours);
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "");
}

static void handleHaApiPost() {
  bool enabled = server.hasArg("ha_api_enabled");
  String apiUrl = server.hasArg("ha_api_url") ? server.arg("ha_api_url") : DEFAULT_HA_API_URL;
  String apiToken = server.hasArg("ha_api_token") ? server.arg("ha_api_token") : "";
  if (apiToken.length() == 0) apiToken = configHaApiToken;
  String entityId = server.hasArg("ha_history_entity") ? server.arg("ha_history_entity") : DEFAULT_HA_HISTORY_ENTITY_ID;
  uint16_t hours = configHaHistoryHours;
  String internalTemperatureEntityId = server.hasArg("ha_internal_temperature_entity") ? server.arg("ha_internal_temperature_entity") : DEFAULT_HA_INTERNAL_TEMPERATURE_ENTITY_ID;
  String internalHumidityEntityId = server.hasArg("ha_internal_humidity_entity") ? server.arg("ha_internal_humidity_entity") : DEFAULT_HA_INTERNAL_HUMIDITY_ENTITY_ID;
  String batteryEntityId = server.hasArg("ha_battery_entity") ? server.arg("ha_battery_entity") : "";

  saveHomeAssistantApiConfig(enabled, apiUrl, apiToken, entityId, hours);
  saveHomeAssistantStatisticsEntities(internalTemperatureEntityId, internalHumidityEntityId, batteryEntityId);

  server.send(
    200,
    "text/html",
    buildSavedPage("API Home Assistant guardada", enabled ? "La boia ja pot consultar l'històric de Home Assistant per dibuixar el gràfic de temperatura." : "La lectura d'històric de Home Assistant queda desactivada.", false)
  );
}

static void handleHaHistoryGet() {
  if (!configHaApiEnabled) {
    server.send(200, "application/json", "{\"error\":\"Històric HA desactivat\"}");
    return;
  }

  if (!isWifiConnected()) {
    server.send(200, "application/json", "{\"error\":\"Wi-Fi no connectat\"}");
    return;
  }

  if (configHaApiToken.length() == 0) {
    server.send(200, "application/json", "{\"error\":\"Token HA no configurat\"}");
    return;
  }

  uint16_t hours = server.hasArg("hours") ? (uint16_t)server.arg("hours").toInt() : configHaHistoryHours;
  hours = constrain(hours, MIN_HA_HISTORY_HOURS, MAX_HA_HISTORY_HOURS);

  String startIso = server.hasArg("start") ? server.arg("start") : "";
  startIso.trim();
  if (startIso.length() == 0) {
    server.send(400, "application/json", "{\"error\":\"Falta paràmetre start\"}");
    return;
  }

  String endIso = server.hasArg("end") ? server.arg("end") : "";
  endIso.trim();
  String url = haHistoryEndpointUrl(startIso, endIso);
  HTTPClient http;
  int code = -1;
  String payload = "";
  String compact = "";
  bool receivedBody = false;

  if (url.startsWith("https://")) {
    WiFiClientSecure client;
    client.setInsecure();
    if (!http.begin(client, url)) {
      server.send(200, "application/json", "{\"error\":\"No puc obrir connexió HTTPS amb HA\"}");
      return;
    }
    http.useHTTP10(true);
    http.setTimeout(20000);
    http.addHeader("Authorization", "Bearer " + configHaApiToken);
    http.addHeader("Accept", "application/json");
    code = http.GET();
    if (code == 200) compact = buildCompactHourlyHistoryJsonFromStream(http, hours, receivedBody);
    else if (code > 0) payload = http.getString();
    http.end();
  } else {
    WiFiClient client;
    if (!http.begin(client, url)) {
      server.send(200, "application/json", "{\"error\":\"No puc obrir connexió HTTP amb HA\"}");
      return;
    }
    http.useHTTP10(true);
    http.setTimeout(20000);
    http.addHeader("Authorization", "Bearer " + configHaApiToken);
    http.addHeader("Accept", "application/json");
    code = http.GET();
    if (code == 200) compact = buildCompactHourlyHistoryJsonFromStream(http, hours, receivedBody);
    else if (code > 0) payload = http.getString();
    http.end();
  }

  if (code != 200 || !receivedBody) {
    String err = "{\"error\":\"HA HTTP ";
    err += String(code);
    if (code == 401) {
      err += ": token no autoritzat. Revisa que sigui un token de llarga durada i que no estigui caducat o copiat malament";
    } else if (code == 404) {
      err += ": URL o entity_id no trobats";
    } else if (code == 200) {
      err += ": resposta buida; Home Assistant no ha retornat historial per aquesta entitat o finestra";
    }
    err += "\"}";
    server.send(200, "application/json", err);
    return;
  }

  server.send(200, "application/json", compact);
}

static bool isConfiguredHaStatisticsEntity(const String& entityId) {
  return entityId == configHaHistoryEntityId ||
         entityId == configHaInternalTemperatureEntityId ||
         entityId == configHaInternalHumidityEntityId ||
         (configHaBatteryEntityId.length() > 0 && entityId == configHaBatteryEntityId);
}

static void handleHaStatisticsGet() {
  if (!configHaApiEnabled || !isWifiConnected() || configHaApiToken.length() == 0) {
    server.send(200, "application/json", "{\"error\":\"Estadístiques HA no disponibles: revisa API, Wi-Fi i token\"}");
    return;
  }

  String entityId = server.hasArg("entity") ? server.arg("entity") : "";
  String startIso = server.hasArg("start") ? server.arg("start") : "";
  String endIso = server.hasArg("end") ? server.arg("end") : "";
  String period = server.hasArg("period") ? server.arg("period") : "hour";
  entityId.trim();
  startIso.trim();
  endIso.trim();
  period.trim();

  if (!isConfiguredHaStatisticsEntity(entityId) || startIso.length() == 0 || endIso.length() == 0) {
    server.send(400, "application/json", "{\"error\":\"Entitat o interval estadístic no vàlid\"}");
    return;
  }
  if (period != "hour" && period != "day" && period != "week" && period != "month") {
    server.send(400, "application/json", "{\"error\":\"Resolució estadística no vàlida\"}");
    return;
  }

  String url = normalizedHaApiUrl(configHaApiUrl) + "/api/services/recorder/get_statistics?return_response";
  String request = "{\"statistic_ids\":[\"" + jsonEscape(entityId) + "\"],";
  request += "\"start_time\":\"" + jsonEscape(startIso) + "\",";
  request += "\"end_time\":\"" + jsonEscape(endIso) + "\",";
  request += "\"period\":\"" + period + "\",";
  request += "\"types\":[\"mean\",\"min\",\"max\"]}";

  HTTPClient http;
  int code = -1;
  String payload;
  if (url.startsWith("https://")) {
    WiFiClientSecure client;
    client.setInsecure();
    if (!http.begin(client, url)) {
      server.send(200, "application/json", "{\"error\":\"No puc obrir connexió HTTPS amb HA\"}");
      return;
    }
    http.useHTTP10(true);
    http.setTimeout(25000);
    http.addHeader("Authorization", "Bearer " + configHaApiToken);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    code = http.POST(request);
    if (code > 0) payload = http.getString();
    http.end();
  } else {
    WiFiClient client;
    if (!http.begin(client, url)) {
      server.send(200, "application/json", "{\"error\":\"No puc obrir connexió HTTP amb HA\"}");
      return;
    }
    http.useHTTP10(true);
    http.setTimeout(25000);
    http.addHeader("Authorization", "Bearer " + configHaApiToken);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    code = http.POST(request);
    if (code > 0) payload = http.getString();
    http.end();
  }

  if (code != 200 || payload.length() == 0) {
    String error = "{\"error\":\"HA statistics HTTP " + String(code) + "";
    if (code == 400 || code == 404) error += ": recorder.get_statistics no disponible o entitat sense estadístiques";
    else if (code == 401) error += ": token no autoritzat";
    error += "\"}";
    server.send(200, "application/json", error);
    return;
  }

  int statisticsPos = payload.indexOf("\"statistics\"");
  int entityPos = statisticsPos >= 0 ? payload.indexOf("\"" + entityId + "\"", statisticsPos) : -1;
  int arrayStart = entityPos >= 0 ? payload.indexOf('[', entityPos) : -1;
  int arrayEnd = arrayStart >= 0 ? payload.indexOf(']', arrayStart) : -1;
  if (arrayStart >= 0 && arrayEnd >= arrayStart) {
    payload.remove(arrayEnd + 1);
    payload.remove(0, arrayStart);
  }

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", payload);
}

static void handleMqttPost() {
  bool enabled = server.hasArg("enabled");
  String host = server.hasArg("host") ? server.arg("host") : "";
  uint16_t port = server.hasArg("port") ? server.arg("port").toInt() : DEFAULT_MQTT_PORT;
  String user = server.hasArg("user") ? server.arg("user") : "";
  String password = server.hasArg("password") ? server.arg("password") : "";
  if (password.length() == 0) password = configMqttPassword;
  String topicBase = server.hasArg("topic_base") ? server.arg("topic_base") : DEFAULT_MQTT_TOPIC_BASE;
  uint16_t publishInterval = server.hasArg("publish_interval") ? server.arg("publish_interval").toInt() : DEFAULT_MQTT_PUBLISH_INTERVAL_SECONDS;

  bool haDiscoveryEnabled = server.hasArg("ha_discovery");
  String haPrefix = server.hasArg("ha_prefix") ? server.arg("ha_prefix") : DEFAULT_HA_DISCOVERY_PREFIX;
  String haDeviceId = server.hasArg("ha_device_id") ? server.arg("ha_device_id") : DEFAULT_HA_DEVICE_ID;
  String haDeviceName = server.hasArg("ha_device_name") ? server.arg("ha_device_name") : DEFAULT_HA_DEVICE_NAME;

  host.trim();
  topicBase.trim();
  haPrefix.trim();
  haDeviceId.trim();
  haDeviceName.trim();

  if (host.length() == 0 && enabled) {
    server.send(
      400,
      "text/html",
      buildSavedPage("Broker invalid", "No pots activar MQTT amb el broker buit.", false)
    );
    return;
  }

  saveMqttConfig(
    enabled,
    host,
    port,
    user,
    password,
    topicBase,
    publishInterval
  );

  saveHomeAssistantConfig(
    haDiscoveryEnabled,
    haPrefix,
    haDeviceId,
    haDeviceName
  );

  Serial.println();
  Serial.println("Configuracio MQTT guardada des de la web:");
  Serial.print("Activat: ");
  Serial.println(configMqttEnabled ? "si" : "no");
  Serial.print("Broker: ");
  Serial.print(configMqttHost);
  Serial.print(":");
  Serial.println(configMqttPort);
  Serial.print("Topic base: ");
  Serial.println(configMqttTopicBase);
  Serial.print("Interval publicacio: ");
  Serial.println(configMqttPublishIntervalSeconds);
  Serial.print("HA Discovery: ");
  Serial.println(configHaDiscoveryEnabled ? "si" : "no");
  Serial.print("HA device ID: ");
  Serial.println(configHaDeviceId);

  reconfigureMqtt();

  server.send(
    200,
    "text/html",
    buildSavedPage("MQTT guardat", "La configuracio MQTT s'ha guardat i la connexio MQTT s'ha reiniciat.", false)
  );
}

static void handleMqttResetPost() {
  resetMqttConfigToDefaults();
  resetHomeAssistantConfigToDefaults();

  Serial.println();
  Serial.println("MQTT restaurat a valors per defecte.");

  reconfigureMqtt();

  server.send(
    200,
    "text/html",
    buildSavedPage("MQTT restaurat", "S'han restaurat els valors MQTT per defecte.", false)
  );
}

static void handleMqttDiscoveryPost() {
  if (!configMqttEnabled || !isMqttConnected()) {
    server.send(
      400,
      "text/html",
      buildSavedPage("MQTT no connectat", "No puc publicar Discovery perquè MQTT no esta connectat.", false)
    );
    return;
  }

  appState.mqttDiscoveryPublished = false;
  publishHomeAssistantDiscovery();

  server.send(
    200,
    "text/html",
    buildSavedPage("Discovery publicat", "S'han republicat les entitats MQTT Discovery de Home Assistant.", false)
  );
}

static void handleGithubOtaConfigPost() {
  bool enabled = server.hasArg("github_ota_enabled");
  String manifestUrl = server.arg("github_manifest_url");
  bool allowSame = server.hasArg("github_allow_same");
  saveGithubOtaConfig(enabled, manifestUrl, allowSame);

  appState.githubUpdateChecked = false;
  appState.githubUpdateOk = false;
  appState.githubUpdateAvailable = false;
  appState.githubRemoteOlder = false;
  appState.githubRemoteSameVersion = false;
  appState.githubUpdateMessage = "Configuracio guardada. Torna a comprovar GitHub.";
  appState.githubUpdateDetails = "S'ha guardat la URL del manifest i les opcions OTA.";
  redirectToMaintenanceOta();
}


static void applyInternetCheckInfo(const InternetCheckInfo& info) {
  appState.internetCheckDone = true;
  appState.internetCheckOk = info.ok;
  appState.internetCheckMessage = info.message;
  appState.internetCheckDetails = info.details;
  appState.internetHttpCode = info.httpCode;
  appState.internetResolvedIp = info.resolvedIp;
  appState.internetLastCheckMillis = millis();
}

static void applyGitHubUpdateInfo(const GitHubUpdateInfo& info) {
  appState.githubUpdateChecked = true;
  appState.githubUpdateOk = info.ok;
  appState.githubUpdateAvailable = info.updateAvailable;
  appState.githubRemoteOlder = info.remoteOlder;
  appState.githubRemoteSameVersion = info.sameVersion;
  appState.githubLastHttpCode = info.httpCode;
  appState.githubLastCheckMillis = millis();
  appState.githubUpdateMessage = info.message;
  appState.githubUpdateVersion = info.version;
  appState.githubUpdateSha = info.buildSha;
  appState.githubUpdateDate = info.buildDate;
  appState.githubFirmwareUrl = info.firmwareUrl;
  appState.githubFirmwareSha256 = info.firmwareSha256;
  appState.githubFirmwareSize = info.sizeBytes;
  appState.githubUpdateDetails = info.details;
}

static void handleInternetCheckPost() {
  InternetCheckInfo info = checkInternetConnectivityNow();
  applyInternetCheckInfo(info);
  redirectToMaintenanceOta();
}

static void handleInternetCheckRun() {
  InternetCheckInfo info = checkInternetConnectivityNow();
  applyInternetCheckInfo(info);
  server.send(200, "application/json", buildStatusJsonPayload());
}

static void handleGithubCheckUpdatePost() {
  GitHubUpdateInfo info = checkGitHubUpdateNow(false);
  applyGitHubUpdateInfo(info);
  redirectToMaintenanceOta();
}

static void handleGithubCheckUpdateRun() {
  GitHubUpdateInfo info = checkGitHubUpdateNow(false);
  applyGitHubUpdateInfo(info);
  server.send(200, "application/json", buildStatusJsonPayload());
}

static void handleGithubUpdatePost() {
  String message;
  bool ok = performGitHubOtaUpdate(message, notifyOtaProgressNow);

  appState.otaLastMessage = message;
  if (!ok) {
    server.send(500, "text/plain; charset=utf-8", message);
    return;
  }

  server.send(
    200,
    "text/html",
    buildSavedPage("Actualitzant des de GitHub", message, true)
  );

  if (ok) scheduleRestart();
}

static void handleDefaultsPost() {
  resetConfigToDefaults();
  resetSensorConfigToDefaults();
  appState.mqttConfigStatePublishRequested = true;

  Serial.println();
  Serial.println("Configuracio de temperatura restaurada a valors per defecte.");

  server.send(
    200,
    "text/html",
    buildSavedPage("Valors restaurats", "S'han restaurat els valors per defecte de temperatura i sonda.", false)
  );
}

static void handleRestartPost() {
  server.send(
    200,
    "text/html",
    buildSavedPage("Reiniciant boia", "La boia es reiniciara en uns segons.", true)
  );

  scheduleRestart();
}

static void handleUpdateFinished() {
  if (localOtaChunkRejected) {
    server.send(409, "text/plain", "Offset OTA no valid");
    return;
  }

  if (appState.otaInProgress && localOtaExpectedSize > 0 && localOtaReceivedSize < localOtaExpectedSize) {
    server.send(200, "application/json", "{\"received\":" + String(localOtaReceivedSize) + "}");
    return;
  }

  bool ok = appState.otaSuccess && !Update.hasError();

  String message = ok
    ? "Firmware rebut correctament. La boia es reiniciara ara."
    : "L'actualitzacio ha fallat o no s'ha rebut cap firmware valid. Mira el monitor serie per veure el detall.";

  appState.otaInProgress = false;
  appState.otaSuccess = ok;
  appState.otaLastMessage = message;

  String response = "{\"ok\":";
  response += ok ? "true" : "false";
  response += ",\"message\":\"" + jsonEscape(message) + "\"}";
  server.send(ok ? 200 : 500, "application/json", response);

  if (ok) scheduleRestart();
}

static void handleUpdateUpload() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    uint32_t requestedOffset = (uint32_t)server.header("X-Firmware-Offset").toInt();
    uint32_t requestedSize = (uint32_t)server.header("X-Firmware-Size").toInt();
    String requestedSha256 = server.header("X-Firmware-SHA256");
    requestedSha256.toLowerCase();
    localOtaChunkRejected = false;

    if (requestedOffset > 0) {
      bool validContinuation = appState.otaInProgress &&
                               requestedOffset == localOtaReceivedSize &&
                               requestedSize == localOtaExpectedSize &&
                               requestedSha256 == localOtaExpectedSha256;
      if (!validContinuation) {
        localOtaChunkRejected = true;
        Serial.print("Bloc OTA rebutjat. Offset demanat: ");
        Serial.print(requestedOffset);
        Serial.print(" · esperat: ");
        Serial.println(localOtaReceivedSize);
      }
      return;
    }

    if (appState.otaInProgress) {
      Update.abort();
      finishLocalOtaHash();
    }

    Serial.println();
    Serial.print("OTA iniciada: ");
    Serial.println(upload.filename);

    appState.otaInProgress = true;
    appState.otaSuccess = false;
    appState.otaProgressSource = "Local";
    appState.otaProgressPhase = "pujant";
    appState.otaProgressBytes = 0;
    localOtaExpectedSha256 = requestedSha256;
    localOtaExpectedSize = requestedSize;
    localOtaReceivedSize = 0;
    finishLocalOtaHash();
    if (validSha256Header(localOtaExpectedSha256)) {
      mbedtls_sha256_init(&localOtaShaContext);
      mbedtls_sha256_starts(&localOtaShaContext, 0);
      localOtaShaActive = true;
      Serial.println("OTA amb verificacio SHA-256 activada.");
    } else {
      localOtaExpectedSha256 = "";
    }

    appState.otaProgressTotal = localOtaExpectedSize;
    appState.otaProgressPercent = 0;
    appState.otaProgressMillis = millis();
    appState.otaLastMessage = "OTA local en curs";
    notifyOtaProgressNow();

    size_t expectedUpdateSize = localOtaExpectedSize > 0 ? (size_t)localOtaExpectedSize : UPDATE_SIZE_UNKNOWN;
    if (!Update.begin(expectedUpdateSize)) {
      appState.otaLastMessage = "ERROR iniciant OTA";
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (localOtaChunkRejected) return;
    localOtaReceivedSize += upload.currentSize;
    if (localOtaShaActive) {
      mbedtls_sha256_update(&localOtaShaContext, upload.buf, upload.currentSize);
    }
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      appState.otaProgressPhase = "error";
      appState.otaLastMessage = "ERROR escrivint firmware OTA";
      Update.printError(Serial);
      notifyOtaProgressNow();
      return;
    }
    appState.otaProgressBytes = localOtaReceivedSize;
    appState.otaProgressTotal = localOtaExpectedSize;
    appState.otaProgressPercent = localOtaExpectedSize > 0
      ? (uint8_t)((localOtaReceivedSize * 100UL) / localOtaExpectedSize)
      : 0;
    appState.otaProgressPhase = "escrivint";
    appState.otaProgressMillis = millis();
    appState.otaLastMessage = "OTA local rebuda: " + String(upload.totalSize) + " bytes";
  } else if (upload.status == UPLOAD_FILE_END) {
    if (localOtaChunkRejected) return;
    if (localOtaExpectedSize > 0 && localOtaReceivedSize < localOtaExpectedSize) {
      appState.otaProgressPhase = "bloc rebut";
      appState.otaLastMessage = "OTA preparada per continuar des del byte " + String(localOtaReceivedSize);
      return;
    }

    bool integrityOk = true;
    if (localOtaExpectedSize > 0 && localOtaReceivedSize != localOtaExpectedSize) {
      integrityOk = false;
      appState.otaLastMessage = "ERROR mida firmware incorrecta";
      Serial.println(appState.otaLastMessage);
    }
    if (localOtaShaActive) {
      uint8_t digest[32];
      mbedtls_sha256_finish(&localOtaShaContext, digest);
      finishLocalOtaHash();
      String actualSha256 = sha256Hex(digest);
      if (actualSha256 != localOtaExpectedSha256) {
        integrityOk = false;
        appState.otaLastMessage = "ERROR SHA-256 firmware incorrecte";
        Serial.println(appState.otaLastMessage);
      } else {
        Serial.println("SHA-256 OTA verificat correctament.");
      }
    }

    if (!integrityOk) {
      Update.abort();
      appState.otaProgressPhase = "error";
      appState.otaSuccess = false;
      notifyOtaProgressNow();
    } else if (Update.end(true)) {
      Serial.print("OTA completada. Bytes: ");
      Serial.println(upload.totalSize);
      appState.otaProgressPhase = "completada";
      appState.otaProgressPercent = 100;
      appState.otaProgressTotal = localOtaReceivedSize;
      appState.otaProgressBytes = localOtaReceivedSize;
      appState.otaProgressMillis = millis();
      appState.otaLastMessage = "OTA completada";
      appState.otaSuccess = true;
      notifyOtaProgressNow();
    } else {
      appState.otaProgressPhase = "error";
      appState.otaLastMessage = "ERROR finalitzant OTA";
      Update.printError(Serial);
      notifyOtaProgressNow();
    }

    appState.otaInProgress = false;
    notifyOtaProgressNow();
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (localOtaExpectedSize > 0 && localOtaReceivedSize < localOtaExpectedSize) {
      appState.otaInProgress = true;
      appState.otaSuccess = false;
      appState.otaProgressPhase = "reprenent";
      appState.otaLastMessage = "Bloc interromput; es pot reprendre des del byte " + String(localOtaReceivedSize);
      Serial.println(appState.otaLastMessage);
    } else {
      finishLocalOtaHash();
      Update.abort();
      appState.otaInProgress = false;
      appState.otaSuccess = false;
      appState.otaProgressPhase = "error";
      appState.otaLastMessage = "OTA avortada";
      Serial.println("OTA avortada.");
    }
  }
}

static String buildStatusJsonPayload() {
  String json = "{";

  json += "\"version\":\"";
  json += FIRMWARE_VERSION;
  json += "\",";

  json += "\"device_name\":\"";
  json += jsonEscape(configDeviceName);
  json += "\",";

  json += "\"device_hostname\":\"";
  json += jsonEscape(configDeviceHostname);
  json += "\",";

  json += "\"temperature_c\":";
  json += formatTemperatureForJson(appState.lastValidTemperatureC, configTemperatureDecimals);
  json += ",";

  json += "\"internal_temperature_c\":";
  json += formatTemperatureForJson(appState.lastInternalTemperatureC, 2);
  json += ",";

  json += "\"internal_humidity_percent\":";
  json += formatTemperatureForJson(appState.lastInternalHumidityPercent, 1);
  json += ",";

  json += "\"battery_voltage\":";
  json += isnan(appState.lastBatteryVoltage) ? String("null") : String(appState.lastBatteryVoltage, 3);
  json += ",";

  json += "\"battery_percent\":";
  json += isnan(appState.lastBatteryPercent) ? String("null") : String(appState.lastBatteryPercent, 0);
  json += ",";

  json += "\"battery_status\":\"";
  json += jsonEscape(appState.batteryStatus);
  json += "\",";

  json += "\"battery_adc_mv\":";
  json += isnan(appState.lastBatteryAdcMilliVolts) ? String("null") : String(appState.lastBatteryAdcMilliVolts, 0);
  json += ",";

  json += "\"internal_env_status\":\"";
  json += jsonEscape(appState.internalEnvStatus);
  json += "\",";

  json += "\"internal_env_last_error\":\"";
  json += jsonEscape(appState.internalEnvLastError);
  json += "\",";

  bool internalTempAlarm = configInternalEnvAlarmEnabled && !isnan(appState.lastInternalTemperatureC) && appState.lastInternalTemperatureC >= configInternalTempAlarmC;
  bool internalHumidityAlarm = configInternalEnvAlarmEnabled && !isnan(appState.lastInternalHumidityPercent) && appState.lastInternalHumidityPercent >= configInternalHumidityAlarmPercent;
  json += "\"internal_env_alarms_enabled\":";
  json += configInternalEnvAlarmEnabled ? "true" : "false";
  json += ",";
  json += "\"internal_temperature_alarm\":";
  json += internalTempAlarm ? "true" : "false";
  json += ",";
  json += "\"internal_humidity_alarm\":";
  json += internalHumidityAlarm ? "true" : "false";
  json += ",";
  json += "\"internal_temperature_alarm_threshold_c\":";
  json += String(configInternalTempAlarmC, 1);
  json += ",";
  json += "\"internal_humidity_alarm_threshold_percent\":";
  json += String(configInternalHumidityAlarmPercent, 1);
  json += ",";

  json += "\"total_reads\":";
  json += String(appState.totalReads);
  json += ",";

  json += "\"valid_reads\":";
  json += String(appState.validReads);
  json += ",";

  json += "\"failed_reads\":";
  json += String(appState.failedReads);
  json += ",";

  json += "\"last_error\":\"";
  json += jsonEscape(appState.lastErrorMessage);
  json += "\",";

  json += "\"sensor_status\":\"";
  json += jsonEscape(appState.sensorStatus);
  json += "\",";

  json += "\"consecutive_sensor_errors\":";
  json += String(appState.consecutiveSensorErrors);
  json += ",";

  json += "\"wifi_connected\":";
  json += isWifiConnected() ? "true" : "false";
  json += ",";

  json += "\"wifi_ap_active\":";
  json += isWifiApActive() ? "true" : "false";
  json += ",";

  json += "\"wifi_mode\":\"";
  json += jsonEscape(wifiModeText());
  json += "\",";

  json += "\"configured_ssid\":\"";
  json += jsonEscape(configWifiSsid);
  json += "\",";

  json += "\"connected_ssid\":\"";
  json += jsonEscape(WiFi.SSID());
  json += "\",";

  json += "\"ip\":\"";
  json += jsonEscape(wifiStaIpText());
  json += "\",";

  json += "\"ap_ip\":\"";
  json += jsonEscape(wifiApIpText());
  json += "\",";

  json += "\"rssi_dbm\":";
  json += isWifiConnected() ? String(WiFi.RSSI()) : "null";
  json += ",";

  json += "\"mqtt_enabled\":";
  json += configMqttEnabled ? "true" : "false";
  json += ",";

  json += "\"mqtt_connected\":";
  json += isMqttConnected() ? "true" : "false";
  json += ",";

  json += "\"mqtt_host\":\"";
  json += jsonEscape(configMqttHost);
  json += "\",";

  json += "\"mqtt_port\":";
  json += String(configMqttPort);
  json += ",";

  json += "\"mqtt_topic_base\":\"";
  json += jsonEscape(configMqttTopicBase);
  json += "\",";

  json += "\"mqtt_availability_topic\":\"";
  json += jsonEscape(mqttAvailabilityTopic());
  json += "\",";

  json += "\"mqtt_command_restart_topic\":\"";
  json += jsonEscape(mqttCommandRestartTopic());
  json += "\",";

  json += "\"mqtt_command_publish_discovery_topic\":\"";
  json += jsonEscape(mqttCommandPublishDiscoveryTopic());
  json += "\",";

  json += "\"ha_controls_enabled\":true,";

  json += "\"internet_check_done\":";
  json += appState.internetCheckDone ? "true" : "false";
  json += ",";
  json += "\"internet_check_ok\":";
  json += appState.internetCheckOk ? "true" : "false";
  json += ",";
  json += "\"internet_check_message\":\"";
  json += jsonEscape(appState.internetCheckMessage);
  json += "\",";
  json += "\"internet_check_details\":\"";
  json += jsonEscape(appState.internetCheckDetails);
  json += "\",";
  json += "\"internet_resolved_ip\":\"";
  json += jsonEscape(appState.internetResolvedIp);
  json += "\",";
  json += "\"github_update_checked\":";
  json += appState.githubUpdateChecked ? "true" : "false";
  json += ",";
  json += "\"github_update_ok\":";
  json += appState.githubUpdateOk ? "true" : "false";
  json += ",";
  json += "\"github_update_available\":";
  json += appState.githubUpdateAvailable ? "true" : "false";
  json += ",";
  json += "\"github_remote_older\":";
  json += appState.githubRemoteOlder ? "true" : "false";
  json += ",";
  json += "\"github_remote_same_version\":";
  json += appState.githubRemoteSameVersion ? "true" : "false";
  json += ",";
  json += "\"github_update_message\":\"";
  json += jsonEscape(appState.githubUpdateMessage);
  json += "\",";
  json += "\"github_update_details\":\"";
  json += jsonEscape(appState.githubUpdateDetails);
  json += "\",";
  json += "\"github_update_version\":\"";
  json += jsonEscape(appState.githubUpdateVersion);
  json += "\",";
  json += "\"github_update_sha\":\"";
  json += jsonEscape(appState.githubUpdateSha);
  json += "\",";
  json += "\"github_update_sha_short\":\"";
  json += jsonEscape(shortBuildSha(appState.githubUpdateSha));
  json += "\",";
  json += "\"github_update_date\":\"";
  json += jsonEscape(appState.githubUpdateDate);
  json += "\",";
  json += "\"github_firmware_url\":\"";
  json += jsonEscape(appState.githubFirmwareUrl);
  json += "\",";
  json += "\"github_firmware_sha256\":\"";
  json += jsonEscape(appState.githubFirmwareSha256);
  json += "\",";
  json += "\"github_firmware_size\":";
  json += String(appState.githubFirmwareSize);
  json += ",";
  json += "\"github_allow_same_version_update\":";
  json += configGithubAllowSameVersionUpdate ? "true" : "false";
  json += ",";

  json += "\"ota_in_progress\":";
  json += appState.otaInProgress ? "true" : "false";
  json += ",";

  json += "\"ota_success\":";
  json += appState.otaSuccess ? "true" : "false";
  json += ",";

  json += "\"ota_last_message\":\"";
  json += jsonEscape(appState.otaLastMessage);
  json += "\",";

  json += "\"ota_log\":\"";
  json += jsonEscape(appState.otaLog);
  json += "\",";

  json += "\"ota_log_seq\":";
  json += String(appState.otaLogSeq);
  json += ",";

  json += "\"ota_progress_source\":\"";
  json += jsonEscape(appState.otaProgressSource);
  json += "\",";

  json += "\"ota_progress_phase\":\"";
  json += jsonEscape(appState.otaProgressPhase);
  json += "\",";

  json += "\"ota_progress_bytes\":";
  json += String(appState.otaProgressBytes);
  json += ",";

  json += "\"ota_progress_total\":";
  json += String(appState.otaProgressTotal);
  json += ",";

  json += "\"ota_progress_percent\":";
  json += String(appState.otaProgressPercent);
  json += ",";

  json += "\"sd_enabled\":";
  json += isSdEnabled() ? "true" : "false";
  json += ",";
  json += "\"sd_mounted\":";
  json += isSdMounted() ? "true" : "false";
  json += ",";
  json += "\"sd_status\":\"";
  json += jsonEscape(sdStatusText());
  json += "\",";
  json += "\"sd_card_type\":\"";
  json += jsonEscape(sdCardTypeText());
  json += "\",";
  json += "\"sd_history_file\":\"";
  json += jsonEscape(sdHistoryPathText());
  json += "\",";
  json += "\"sd_history_writes\":";
  json += String(appState.sdHistoryWriteCount);
  json += ",";
  json += "\"sd_history_write_fails\":";
  json += String(appState.sdHistoryWriteFailCount);
  json += ",";
  json += "\"sd_daily_stats_file\":\"";
  json += jsonEscape(sdDailyStatsPathText());
  json += "\",";
  json += "\"sd_system_log_file\":\"";
  json += jsonEscape(sdSystemLogPathText());
  json += "\",";
  json += "\"sd_pending_mqtt_file\":\"";
  json += jsonEscape(sdPendingMqttPathText());
  json += "\",";
  json += "\"sd_mqtt_pending_count\":";
  json += String(appState.sdMqttPendingCount);
  json += ",";
  json += "\"sd_mqtt_flush_count\":";
  json += String(appState.sdMqttFlushCount);
  json += ",";
  json += "\"sd_daily_records\":";
  json += String(appState.sdDailyRecordCount);
  json += ",";
  json += "\"sd_daily_errors\":";
  json += String(appState.sdDailyErrorCount);
  json += ",";
  json += "\"sd_last_error\":\"";
  json += jsonEscape(sdLastErrorText());
  json += "\",";

  json += "\"hardware_ready\":";
  json += appState.hardwareReady ? "true" : "false";
  json += ",";

  json += "\"reset_button_pin\":";
  json += String(RESET_BUTTON_PIN);
  json += ",";

  json += "\"status_led_pin\":";
  json += String(STATUS_LED_PIN);
  json += ",";

  json += "\"internal_board_led_pin\":";
  json += String(INTERNAL_BOARD_LED_PIN);
  json += ",";

  json += "\"board_led_enabled\":";
  json += configBoardLedEnabled ? "true" : "false";
  json += ",";

  json += "\"board_led_mirror_status\":";
  json += configBoardLedMirrorStatus ? "true" : "false";
  json += ",";

  json += "\"one_wire_pin\":";
  json += String(ONE_WIRE_PIN);
  json += ",";

  json += "\"button_pressed\":";
  json += appState.buttonPressed ? "true" : "false";
  json += ",";

  json += "\"button_press_duration_ms\":";
  json += String(appState.buttonPressDurationMs);
  json += ",";

  json += "\"last_hardware_action\":\"";
  json += jsonEscape(appState.lastHardwareAction);
  json += "\",";

  json += "\"ha_discovery_enabled\":";
  json += configHaDiscoveryEnabled ? "true" : "false";
  json += ",";

  json += "\"ha_discovery_published\":";
  json += appState.mqttDiscoveryPublished ? "true" : "false";
  json += ",";

  json += "\"ha_discovery_prefix\":\"";
  json += jsonEscape(configHaDiscoveryPrefix);
  json += "\",";

  json += "\"ha_device_id\":\"";
  json += jsonEscape(configHaDeviceId);
  json += "\",";

  json += "\"ha_device_name\":\"";
  json += jsonEscape(configHaDeviceName);
  json += "\",";

  json += "\"ha_api_enabled\":";
  json += configHaApiEnabled ? "true" : "false";
  json += ",";

  json += "\"ha_api_url\":\"";
  json += jsonEscape(configHaApiUrl);
  json += "\",";

  json += "\"ha_history_entity_id\":\"";
  json += jsonEscape(configHaHistoryEntityId);
  json += "\",";

  json += "\"ha_internal_temperature_entity_id\":\"";
  json += jsonEscape(configHaInternalTemperatureEntityId);
  json += "\",";

  json += "\"ha_internal_humidity_entity_id\":\"";
  json += jsonEscape(configHaInternalHumidityEntityId);
  json += "\",";

  json += "\"ha_battery_entity_id\":\"";
  json += jsonEscape(configHaBatteryEntityId);
  json += "\",";

  json += "\"ha_history_hours\":";
  json += String(configHaHistoryHours);
  json += ",";

  json += "\"mqtt_publish_count\":";
  json += String(appState.mqttPublishCount);
  json += ",";

  json += "\"mqtt_fail_count\":";
  json += String(appState.mqttFailCount);
  json += ",";

  json += "\"uptime_seconds\":";
  json += String(getUptimeSeconds());
  json += ",";

  json += "\"health\":\"";
  json += jsonEscape(healthText());
  json += "\",";

  json += "\"health_details\":\"";
  json += jsonEscape(healthDetails());
  json += "\",";

  json += "\"rssi_quality\":\"";
  json += jsonEscape(rssiQualityText());
  json += "\",";

  json += "\"production_mode\":";
  json += configProductionMode ? "true" : "false";
  json += ",";

  json += "\"config\":{";
  json += "\"read_interval_seconds\":";
  json += String(configReadIntervalSeconds);
  json += ",";
  json += "\"mqtt_publish_interval_seconds\":";
  json += String(configMqttPublishIntervalSeconds);
  json += ",";
  json += "\"temperature_decimals\":";
  json += String(configTemperatureDecimals);
  json += "}";

  json += "}";

  return json;

}

static void handleStatusJson() {
  server.send(200, "application/json", buildStatusJsonPayload());
}
static void handleNotFound() {
  server.send(404, "text/plain", "404 - No trobat");
}


static void handleWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  (void)payload;
  (void)length;

  if (type == WStype_CONNECTED) {
    IPAddress ip = webSocket.remoteIP(num);
    Serial.print("WebSocket connectat: ");
    Serial.println(ip);
    String payload = buildStatusJsonPayload();
    webSocket.sendTXT(num, payload);
  }
}

static void broadcastWebSocketStatus(bool force) {
  unsigned long now = millis();
  if (!force && now - lastWebSocketBroadcastMillis < WEBSOCKET_BROADCAST_INTERVAL_MS) {
    return;
  }

  lastWebSocketBroadcastMillis = now;
  String payload = buildStatusJsonPayload();
  webSocket.broadcastTXT(payload);
}

static void notifyOtaProgressNow() {
  lastWebSocketBroadcastMillis = millis();
  String payload = buildStatusJsonPayload();
  webSocket.broadcastTXT(payload);
  webSocket.loop();
}

// ==========================
// PUBLIC
// ==========================

void setupWebServer() {
  initAuthManager();
  localOtaUploadPath = "/update/" + createWebUploadToken();
  static const char* collectedHeaders[] = {"Cookie", "Origin", "X-Firmware-SHA256", "X-Firmware-Size", "X-Firmware-Offset"};
  server.collectHeaders(collectedHeaders, 5);
  server.addMiddleware(authMiddleware);

  server.on("/login", HTTP_GET, handleLoginGet);
  server.on("/login", HTTP_POST, handleLoginPost);
  server.on("/logout", HTTP_GET, handleLogout);
  server.on("/change-password", HTTP_GET, handleChangePasswordGet);
  server.on("/change-password", HTTP_POST, handleChangePasswordPost);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/storage", HTTP_GET, handleStorageGet);
  server.on("/sd-info", HTTP_GET, handleSdInfoGet);
  server.on("/sd-history.csv", HTTP_GET, handleSdHistoryCsvGet);
  server.on("/sd-daily-stats.csv", HTTP_GET, handleSdDailyStatsCsvGet);
  server.on("/sd-pending-mqtt.jsonl", HTTP_GET, handleSdPendingMqttGet);
  server.on("/sd-list", HTTP_GET, handleSdListGet);
  server.on("/sd-view", HTTP_GET, handleSdViewGet);
  server.on("/sd-download", HTTP_GET, handleSdDownloadGet);
  server.on("/sd-read", HTTP_GET, handleSdReadGet);
  server.on("/sd-format", HTTP_POST, handleSdFormatPost);
  server.on("/config", HTTP_GET, handleConfigGet);
  server.on("/config", HTTP_POST, handleConfigPost);
  server.on("/wifi", HTTP_GET, handleWifiGet);
  server.on("/wifi", HTTP_POST, handleWifiPost);
  server.on("/wifi-network", HTTP_POST, handleWifiNetworkPost);
  server.on("/wifi-scan", HTTP_GET, handleWifiScanGet);
  server.on("/wifi-reset", HTTP_POST, handleWifiResetPost);
  server.on("/wifi-network-reset", HTTP_POST, handleWifiNetworkResetPost);
  server.on("/mqtt", HTTP_GET, handleMqttGet);
  server.on("/mqtt", HTTP_POST, handleMqttPost);
  server.on("/mqtt-reset", HTTP_POST, handleMqttResetPost);
  server.on("/mqtt-discovery", HTTP_POST, handleMqttDiscoveryPost);
  server.on("/ha-api", HTTP_POST, handleHaApiPost);
  server.on("/ha-history", HTTP_GET, handleHaHistoryGet);
  server.on("/ha-statistics", HTTP_GET, handleHaStatisticsGet);
  server.on("/ha-history-settings", HTTP_POST, handleHaHistorySettingsPost);
  server.on("/system", HTTP_GET, handleSystemGet);
  server.on("/maintenance", HTTP_GET, handleMaintenanceGet);
  server.on("/help", HTTP_GET, handleHelpGet);
  server.on("/hardware", HTTP_GET, handleHardwareGet);
  server.on("/identity", HTTP_POST, handleIdentityPost);
  server.on("/device-mode", HTTP_POST, handleDeviceModePost);
  server.on("/board-leds", HTTP_POST, handleBoardLedsPost);
  server.on("/internal-env-alarm", HTTP_POST, handleInternalEnvAlarmPost);
  server.on("/battery-config", HTTP_POST, handleBatteryConfigPost);
  server.on("/battery-config-reset", HTTP_POST, handleBatteryConfigResetPost);
  server.on("/user-credentials", HTTP_POST, handleUserCredentialsPost);
  server.on("/mqtt-publish-now", HTTP_POST, handleMqttPublishNowPost);
  server.on("/config-export", HTTP_GET, handleConfigExport);
  server.on("/config-import", HTTP_POST, handleConfigImportPost);
  server.on("/diagnostics", HTTP_GET, handleDiagnosticsGet);
  server.on("/firmware", HTTP_GET, handleFirmwareGet);
  server.on("/defaults", HTTP_POST, handleDefaultsPost);
  server.on("/restart", HTTP_POST, handleRestartPost);
  // WebServer chooses upload handlers before parsing request headers. The
  // per-boot capability path protects the streaming callback at that stage.
  server.on(localOtaUploadPath.c_str(), HTTP_POST, handleUpdateFinished, handleUpdateUpload);
  server.on("/github-ota-config", HTTP_POST, handleGithubOtaConfigPost);
  server.on("/internet-check", HTTP_POST, handleInternetCheckPost);
  server.on("/internet-check-run", HTTP_GET, handleInternetCheckRun);
  server.on("/github-check-update", HTTP_POST, handleGithubCheckUpdatePost);
  server.on("/github-check-update-run", HTTP_GET, handleGithubCheckUpdateRun);
  server.on("/github-update", HTTP_POST, handleGithubUpdatePost);
  server.on("/status", HTTP_GET, handleStatusJson);
  server.onNotFound(handleNotFound);

  server.begin();
  static const char* webSocketHeaders[] = {"Cookie"};
  webSocket.onValidateHttpHeader(
    [](String headerName, String headerValue) {
      return !headerName.equalsIgnoreCase("Cookie") ||
             (isWebSessionCookieValid(headerValue) && !webAuthPasswordChangeRequired());
    },
    webSocketHeaders,
    1
  );
  webSocket.begin();
  webSocket.onEvent(handleWebSocketEvent);

  Serial.println();
  Serial.println("Servidor web iniciat.");

  if (isWifiConnected()) {
    Serial.print("Obre al navegador: http://");
    Serial.println(WiFi.localIP());
  }

  if (isWifiApActive()) {
    Serial.print("Obre en mode AP: http://");
    Serial.println(WiFi.softAPIP());
  }

  Serial.println("JSON estat: /status");
  Serial.println("WebSocket live: port 81");
}

void handleWebServer() {
  server.handleClient();
  webSocket.loop();
  broadcastWebSocketStatus(false);

  if (restartPending && (long)(millis() - restartAtMillis) >= 0) {
    restartPending = false;
    Serial.println("Reiniciant ara.");
    publishOfflineAndDisconnect();
    delay(80);
    ESP.restart();
  }
}
