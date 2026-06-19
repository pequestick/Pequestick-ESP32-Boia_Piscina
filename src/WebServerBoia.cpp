#include "WebServerBoia.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <WebSocketsServer.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

#include "AppConfig.h"
#include "AppState.h"
#include "Utils.h"
#include "WifiManagerBoia.h"
#include "MqttManager.h"
#include "HardwareManager.h"
#include "GitHubOta.h"
#include "AuthManager.h"

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
  html += "button{background:linear-gradient(180deg,#2563eb,#1d4ed8);color:white;border:0;border-radius:12px;padding:12px 16px;font-size:15px;font-weight:850;cursor:pointer;}button.secondary{background:#475569;}button.danger{background:#b91c1c;}.buttons{display:flex;gap:10px;flex-wrap:wrap;}";
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
  html += ".ota-progress-card{margin:14px 0;padding:14px;border:1px solid rgba(56,189,248,.25);background:linear-gradient(180deg,rgba(14,165,233,.10),rgba(15,23,42,.40));border-radius:16px}.ota-progress-card.hidden{display:none}.ota-progress-head{display:flex;justify-content:space-between;gap:12px;align-items:flex-start;margin-bottom:10px}.ota-progress-title{font-weight:950;color:#e0f2fe}.ota-progress-text{font-size:12px;color:#94a3b8;line-height:1.45}.progress-track{height:15px;background:#020617;border:1px solid #263449;border-radius:999px;overflow:hidden;position:relative}.progress-fill{height:100%;width:0%;background:linear-gradient(90deg,#2563eb,#38bdf8,#22c55e);border-radius:999px;transition:width .25s ease}.progress-fill.indeterminate{width:38%;position:absolute;animation:indeterminate 1.2s infinite ease-in-out}.ota-progress-card.done .progress-fill{background:linear-gradient(90deg,#16a34a,#86efac)}.ota-progress-card.error .progress-fill{background:linear-gradient(90deg,#dc2626,#fca5a5)}@keyframes indeterminate{0%{left:-40%}100%{left:105%}}";
  html += ".ota-log{margin-top:12px;background:#020617;border:1px solid #263449;border-radius:14px;padding:12px;color:#c7d2fe;font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:12px;line-height:1.45;max-height:230px;overflow:auto;white-space:pre-wrap}.ota-log-head{display:flex;justify-content:space-between;align-items:center;margin-top:12px;color:#bfdbfe;font-size:12px;font-weight:900;text-transform:uppercase;letter-spacing:.06em}.ota-log small{color:#64748b}";
  html += "button:disabled{opacity:.45;cursor:not-allowed;background:#334155;}";
  html += "pre{white-space:pre-wrap;word-break:break-word;background:#050b14;border:1px solid #334155;border-radius:14px;padding:14px;color:#dbeafe;}";
  html += ".temp-card{position:relative;overflow:hidden;min-height:188px;}";
  html += ".temp-card canvas{position:absolute;inset:0;width:100%;height:100%;opacity:.26;pointer-events:none;}";
  html += ".temp-card .temp-content{position:relative;z-index:1;}";
  html += ".chart-note{position:absolute;right:18px;bottom:12px;color:#94a3b8;font-size:11px;z-index:1;}";
  html += ".history-controls{position:relative;z-index:2;display:flex;justify-content:flex-end;margin-bottom:10px}.history-controls form{display:flex;gap:8px;align-items:center;background:rgba(15,23,42,.78);border:1px solid #263449;border-radius:999px;padding:7px 9px;backdrop-filter:blur(8px)}.history-controls input{width:78px;padding:6px 8px;border-radius:999px;font-size:13px}.history-controls button{padding:7px 10px;border-radius:999px;font-size:12px}.history-controls .label-inline{color:#bfdbfe;font-size:12px;font-weight:800;white-space:nowrap}";
  html += "@media(max-width:920px){.ota-hero{grid-template-columns:1fr}.app-shell{grid-template-columns:1fr}.sidebar{position:relative;top:0;margin-top:0}.tabs{grid-template-columns:1fr}.container{padding:12px}.grid,.grid3{grid-template-columns:1fr}.temp{font-size:44px}.brandrow{display:block}.servicebar{justify-content:flex-start;margin-top:12px}.tab{padding:10px 10px;font-size:14px}.subnav{margin-left:12px}}";
  html += "</style>";

  html += "<script>";
  html += "function togglePassword(id,btn){var input=document.getElementById(id);if(!input)return;if(input.type==='password'){input.type='text';btn.textContent='🙈';btn.setAttribute('aria-label','Ocultar password');}else{input.type='password';btn.textContent='👁️';btn.setAttribute('aria-label','Mostrar password');}}";
  html += "function txt(id,v){var e=document.getElementById(id);if(e)e.textContent=(v===null||v===undefined)?'--':v;}";
  html += "function cls(id,c){var e=document.getElementById(id);if(e){e.classList.remove('ok','warn','bad');e.classList.add(c);}}";
  html += "function service(id,on,bad,text){var e=document.getElementById(id);if(!e)return;e.classList.remove('ok','warn','bad');e.classList.add(on?'ok':(bad?'bad':'warn'));if(text!==undefined){var sp=e.querySelector('span:last-child');if(sp)sp.textContent=text;}}";
  html += "function bytesHuman(n){n=parseInt(n||0,10);if(!n)return '--';if(n<1024)return n+' B';if(n<1048576)return (n/1024).toFixed(1)+' KB';return (n/1048576).toFixed(2)+' MB';}";
  html += "function updateGithubOtaStatus(d){var internetCls=d.internet_check_done?(d.internet_check_ok?'ok':'bad'):'info';var ghCls=d.github_update_checked?(d.github_update_ok?'ok':'bad'):'info';var updCls='info';if(d.github_update_checked){if(d.github_update_available)updCls='warn';else if(d.github_remote_older)updCls='bad';else if(d.github_update_ok)updCls='ok';}txt('ota-internet-main',d.internet_check_done?d.internet_check_message:'Comprovant...');txt('ota-internet-meta',(d.internet_check_details||'')+(d.internet_resolved_ip?' · DNS '+d.internet_resolved_ip:'')+(d.internet_check_done?' · última prova ara':''));txt('ota-github-main',d.github_update_checked?(d.github_update_ok?'Manifest llegit':'Manifest fallit'):'Comprovant...');txt('ota-github-version',d.github_update_version||'--');txt('ota-github-sha',d.github_update_sha_short||d.github_update_sha||'--');txt('ota-github-date',d.github_update_date||'--');txt('ota-update-main',d.github_update_message||'Encara no comprovat');txt('ota-update-details',d.github_update_details||'');cls('ota-internet-tile',internetCls);cls('ota-github-tile',ghCls);cls('ota-update-tile',updCls);cls('ota-internet-main',internetCls);cls('ota-github-main',ghCls);cls('ota-update-main',updCls);var b=document.getElementById('github-install-button');if(b){b.disabled=!(d.github_update_available||(d.github_remote_same_version&&d.github_allow_same_version_update));}}";
  html += "function updateOtaProgress(d){var card=document.getElementById('ota-progress-card');if(!card)return;var pct=parseInt(d.ota_progress_percent||0,10);var inProg=!!d.ota_in_progress;var phase=d.ota_progress_phase||'espera';var source=d.ota_progress_source||'cap';var active=(source!=='cap'&&phase!=='espera')||inProg;if(active)card.classList.remove('hidden');else card.classList.add('hidden');var fill=document.getElementById('ota-progress-fill');var pctEl=document.getElementById('ota-progress-percent');var phaseEl=document.getElementById('ota-progress-phase');var msgEl=document.getElementById('ota-progress-message');var bytesEl=document.getElementById('ota-progress-bytes');card.classList.remove('done','error');if(phase==='error')card.classList.add('error');if(phase==='completada')card.classList.add('done');if(fill){fill.classList.remove('indeterminate');if(inProg&&(!pct||pct<1)){fill.classList.add('indeterminate');fill.style.width='38%';}else{fill.style.width=Math.max(0,Math.min(100,pct))+'%';}}if(pctEl)pctEl.textContent=(pct?pct:0)+'%';if(phaseEl)phaseEl.textContent=(source||'OTA')+' · '+phase;if(msgEl)msgEl.textContent=d.ota_last_message||'Esperant accio OTA';if(bytesEl)bytesEl.textContent=bytesHuman(d.ota_progress_bytes)+' / '+bytesHuman(d.ota_progress_total);var log=document.getElementById('ota-log');if(log&&d.ota_log!==undefined){var atBottom=(log.scrollTop+log.clientHeight+24)>=log.scrollHeight;log.textContent=d.ota_log||'Sense log OTA';if(atBottom)log.scrollTop=log.scrollHeight;}}";
  html += "function runOtaAutoChecks(){if(location.pathname!=='/maintenance'||location.search.indexOf('section=mnt-ota')<0)return;txt('ota-internet-main','Comprovant...');txt('ota-github-main','Comprovant...');fetch('/internet-check-run',{cache:'no-store'}).then(function(r){return r.json();}).then(function(d){applyStatus(d);return fetch('/github-check-update-run',{cache:'no-store'});}).then(function(r){return r.json();}).then(function(d){applyStatus(d);}).catch(function(){txt('ota-update-main','No puc actualitzar estat OTA');txt('ota-update-details','La comprovació automàtica ha fallat. Prova els botons manuals.');});}";
  html += "function applyStatus(d){txt('live-temp',d.temperature_c===null?'Sense dades':d.temperature_c);txt('live-wifi',d.wifi_connected?'Connectat':(d.wifi_ap_active?'AP setup':'Desconnectat'));txt('live-ip',d.ip);txt('live-rssi',d.rssi_dbm===null?'Sense senyal':d.rssi_dbm+' dBm');txt('live-mqtt',d.mqtt_enabled?(d.mqtt_connected?'Connectat':'Desconnectat'):'Desactivat');txt('live-uptime',d.uptime_seconds+' s');txt('live-sensor',d.sensor_status||'UNKNOWN');txt('live-reads',d.valid_reads+'/'+d.total_reads);txt('live-hostname',d.device_hostname);txt('live-device-name',d.device_name);service('svc-wifi',d.wifi_connected,d.wifi_ap_active?false:true,d.wifi_connected?'Connectat':(d.wifi_ap_active?'AP setup':'Error'));service('svc-ap',d.wifi_ap_active,false,d.wifi_ap_active?'Actiu':'Inactiu');service('svc-mqtt',d.mqtt_enabled&&d.mqtt_connected,d.mqtt_enabled&&!d.mqtt_connected,d.mqtt_enabled?(d.mqtt_connected?'Connectat':'Error'):'Off');service('svc-ha',d.ha_discovery_enabled&&d.ha_discovery_published,d.ha_discovery_enabled&&!d.ha_discovery_published,d.ha_discovery_enabled?(d.ha_discovery_published?'OK':'Pendent'):'Off');service('svc-sensor',d.sensor_status==='OK',d.sensor_status==='ERROR',d.sensor_status||'UNKNOWN');service('svc-ota',!d.ota_in_progress,d.ota_in_progress,d.ota_in_progress?'En curs':'Disponible');updateGithubOtaStatus(d);updateOtaProgress(d);}";
  html += "function startWS(){try{var ws=new WebSocket('ws://'+location.hostname+':81/');ws.onmessage=function(ev){try{applyStatus(JSON.parse(ev.data));}catch(e){}};ws.onclose=function(){setTimeout(startWS,3000);};ws.onerror=function(){try{ws.close();}catch(e){}};}catch(e){}}";
  html += "function bindConfirms(){document.querySelectorAll('form[data-confirm]').forEach(function(f){if(f.id==='ota-local-form'||f.id==='github-install-form')return;f.addEventListener('submit',function(e){if(!confirm(f.getAttribute('data-confirm'))){e.preventDefault();}});});}";
  html += "function bindAccordion(){document.querySelectorAll('.menu-toggle').forEach(function(btn){btn.addEventListener('click',function(){var g=btn.closest('.menu-group');if(!g)return;var isOpen=g.classList.contains('open');document.querySelectorAll('.menu-group.has-sub').forEach(function(x){x.classList.remove('open');});if(!isOpen)g.classList.add('open');});});}";
  html += "function applySubpage(){var links=[].slice.call(document.querySelectorAll('.subtab'));if(!links.length)return;var params=new URLSearchParams(location.search);var selected=params.get('section');var ids=[];links.forEach(function(a){var u=new URL(a.href,location.href);var id=u.searchParams.get('section');if(id){ids.push(id);}});if(!selected||ids.indexOf(selected)<0)selected=ids[0];ids.forEach(function(id){var el=document.getElementById(id);if(el)el.style.display=(id===selected)?'':'none';});document.querySelectorAll('.subpage-extra').forEach(function(el){el.style.display=(el.getAttribute('data-parent')===selected)?'':'none';});links.forEach(function(a){var u=new URL(a.href,location.href);a.classList.toggle('active',u.searchParams.get('section')===selected);});}";
  html += "function extractHaPoints(raw){var out=[];try{if(raw&&Array.isArray(raw.points)){raw.points.forEach(function(x){var v=parseFloat(x.v);var tm=x.t;if(isFinite(v)&&tm){out.push({t:new Date(tm).getTime(),v:v});}});return out;}var arr=raw;if(Array.isArray(arr)&&Array.isArray(arr[0]))arr=arr[0];if(!Array.isArray(arr))return out;arr.forEach(function(x){var st=(x.state!==undefined)?x.state:x.s;var tm=x.last_changed||x.last_updated||x.lc||x.lu;var v=parseFloat(st);if(isFinite(v)&&tm){out.push({t:new Date(tm).getTime(),v:v});}});}catch(e){}return out;}";
  html += "function drawTempHistory(points,hours){var c=document.getElementById('temp-history-chart');if(!c)return;var ctx=c.getContext('2d');var r=c.getBoundingClientRect();var w=Math.max(1,Math.floor(r.width*devicePixelRatio));var h=Math.max(1,Math.floor(r.height*devicePixelRatio));if(c.width!==w)c.width=w;if(c.height!==h)c.height=h;ctx.clearRect(0,0,w,h);if(!points||points.length<2){txt('temp-history-note','Històric HA no disponible');return;}var vals=points.map(function(p){return p.v;});var mn=Math.min.apply(null,vals),mx=Math.max.apply(null,vals);if(mx-mn<0.2){mx+=0.1;mn-=0.1;}var t0=points[0].t,t1=points[points.length-1].t;if(t1<=t0)t1=t0+1;ctx.globalAlpha=1;ctx.lineWidth=Math.max(2,2*devicePixelRatio);ctx.strokeStyle='rgba(125,211,252,.95)';ctx.fillStyle='rgba(37,99,235,.22)';ctx.beginPath();points.forEach(function(p,i){var x=(p.t-t0)/(t1-t0)*w;var y=h-((p.v-mn)/(mx-mn)*h*.72+h*.14);if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);});ctx.stroke();ctx.lineTo(w,h);ctx.lineTo(0,h);ctx.closePath();ctx.fill();txt('temp-history-note','Últimes '+hours+' h · mostres horàries · '+points.length+' punts · '+mn.toFixed(1)+'-'+mx.toFixed(1)+' °C');}";
  html += "function loadTempHistory(){var c=document.getElementById('temp-history-chart');if(!c)return;var inp=document.getElementById('ha-history-hours-inline');var hours=inp?parseInt(inp.value||'24',10):24;if(!isFinite(hours)||hours<1)hours=24;if(hours>168)hours=168;var start=new Date(Date.now()-hours*60*60*1000).toISOString();fetch('/ha-history?hours='+encodeURIComponent(hours)+'&start='+encodeURIComponent(start)).then(function(r){return r.json();}).then(function(j){if(j.error){txt('temp-history-note',j.error);return;}drawTempHistory(extractHaPoints(j),hours);}).catch(function(){txt('temp-history-note','No puc llegir historial HA. Prova menys hores o revisa token/URL.');});}";
  html += "function showOtaProgress(source,msg){var card=document.getElementById('ota-progress-card');if(!card)return;card.classList.remove('hidden','done','error');txt('ota-progress-phase',source+' · iniciant');txt('ota-progress-message',msg||'Preparant actualitzacio');txt('ota-progress-percent','0%');txt('ota-progress-bytes','-- / --');var fill=document.getElementById('ota-progress-fill');if(fill){fill.classList.remove('indeterminate');fill.style.width='0%';}}";
  html += "function bindOtaForms(){var local=document.getElementById('ota-local-form');if(local){local.addEventListener('submit',function(e){e.preventDefault();if(local.getAttribute('data-confirm')&&!confirm(local.getAttribute('data-confirm')))return;var file=document.getElementById('ota-local-file');if(!file||!file.files||!file.files.length){alert('Tria primer un firmware.bin');return;}showOtaProgress('OTA local','Pujant firmware local');var xhr=new XMLHttpRequest();xhr.open('POST','/update');xhr.upload.onprogress=function(ev){if(ev.lengthComputable){var pct=Math.round(ev.loaded*100/ev.total);var fill=document.getElementById('ota-progress-fill');if(fill)fill.style.width=pct+'%';txt('ota-progress-percent',pct+'%');txt('ota-progress-phase','OTA local · pujant');txt('ota-progress-bytes',bytesHuman(ev.loaded)+' / '+bytesHuman(ev.total));}};xhr.onload=function(){txt('ota-progress-message',xhr.status>=200&&xhr.status<300?'Firmware rebut. La boia es reiniciara si tot ha anat be.':'OTA local fallida. HTTP '+xhr.status);if(xhr.status>=200&&xhr.status<300){var card=document.getElementById('ota-progress-card');if(card)card.classList.add('done');setTimeout(function(){location.href='/maintenance?section=mnt-ota';},8000);}};xhr.onerror=function(){txt('ota-progress-message','Error de xarxa pujant firmware');var card=document.getElementById('ota-progress-card');if(card)card.classList.add('error');};xhr.send(new FormData(local));});}var gh=document.getElementById('github-install-form');if(gh){gh.addEventListener('submit',function(e){e.preventDefault();if(gh.getAttribute('data-confirm')&&!confirm(gh.getAttribute('data-confirm')))return;showOtaProgress('GitHub OTA','Demanant descarrega del firmware a la boia');var fill=document.getElementById('ota-progress-fill');if(fill)fill.classList.add('indeterminate');fetch('/github-update',{method:'POST'}).then(function(r){return r.text().then(function(t){return {ok:r.ok,txt:t,status:r.status};});}).then(function(res){txt('ota-progress-message',res.ok?'Actualitzacio enviada. Si ha anat be, la boia es reiniciara.':'GitHub OTA fallida. HTTP '+res.status);if(!res.ok){var card=document.getElementById('ota-progress-card');if(card)card.classList.add('error');}}).catch(function(){txt('ota-progress-message','Connexio tallada. Si la boia reinicia, pot ser normal durant OTA.');});});}}";
  html += "window.addEventListener('load',function(){bindConfirms();bindAccordion();applySubpage();startWS();loadTempHistory();bindOtaForms();setTimeout(runOtaAutoChecks,600);});";
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
  html += "<div class='servicebar'>";
  appendServicePill(html, "svc-wifi", "📶", "Wi-Fi", isWifiConnected() ? "ok" : "bad", wifiStatusText());
  appendServicePill(html, "svc-ap", "🛟", "AP", isWifiApActive() ? "warn" : "ok", isWifiApActive() ? "Actiu" : "Inactiu");
  appendServicePill(html, "svc-mqtt", "📡", "MQTT", configMqttEnabled ? statusClass(isMqttConnected()) : "warn", mqttStatusText());
  appendServicePill(html, "svc-ha", "🏠", "HA", (configHaDiscoveryEnabled && appState.mqttDiscoveryPublished) ? "ok" : "warn", configHaDiscoveryEnabled ? (appState.mqttDiscoveryPublished ? "OK" : "Pendent") : "Off");
  appendServicePill(html, "svc-sensor", "🌡️", "Sonda", appState.sensorStatus == "OK" ? "ok" : (appState.sensorStatus == "ERROR" ? "bad" : "warn"), appState.sensorStatus);
  appendServicePill(html, "svc-ota", "⬆️", "OTA", appState.otaInProgress ? "warn" : "ok", appState.otaInProgress ? "En curs" : "Disponible");
  html += "</div></div>";
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

// ==========================
// PAGINES
// ==========================

static String buildStatusPage() {
  String tempText = formatTemperature(appState.lastValidTemperatureC, configTemperatureDecimals);
  String html = "";

  appendPageStart(html, "status", true);

  html += "<div class='card temp-card'>";
  html += "<canvas id='temp-history-chart' aria-hidden='true'></canvas>";
  html += "<div class='temp-content'>";
  html += "<div class='history-controls'><form method='POST' action='/ha-history-settings'><span class='label-inline'>Històric</span><input id='ha-history-hours-inline' name='ha_history_hours' type='number' min='1' max='168' value='";
  html += String(configHaHistoryHours);
  html += "'><span class='label-inline'>hores</span><button type='submit'>Aplicar</button></form></div>";
  html += "<h2>Temperatura actual</h2>";
  html += "<div class='temp'><span id='live-temp'>";
  html += tempText;

  html += "</span>";

  if (!isnan(appState.lastValidTemperatureC)) {
    html += " <span class='unit'>&deg;C</span>";
  }

  html += "</div>";

  if (appState.failedReads == 0 && appState.validReads > 0) {
    html += "<div class='ok'>Estat: lectures correctes</div>";
  } else if (appState.validReads == 0) {
    html += "<div class='warn'>Estat: esperant lectura valida</div>";
  } else {
    html += "<div class='warn'>Estat: hi ha hagut alguna lectura fallida</div>";
  }

  html += "</div>";
  html += "<div id='temp-history-note' class='chart-note'>Carregant històric HA...</div>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<h2>Lectures</h2>";
  html += "<div class='grid'>";

  html += "<div class='item'><div class='label'>Totals</div><div class='value'>";
  html += String(appState.totalReads);
  html += "</div></div>";

  html += "<div class='item'><div class='label'>Valides</div><div class='value ok'>";
  html += String(appState.validReads);
  html += "</div></div>";

  html += "<div class='item'><div class='label'>Fallides</div><div class='value ";
  html += appState.failedReads == 0 ? "ok" : "bad";
  html += "'>";
  html += String(appState.failedReads);
  html += "</div></div>";

  html += "<div class='item'><div class='label'>Ultim error</div><div class='value' style='font-size:15px;'>";
  html += htmlEscape(appState.lastErrorMessage);
  html += "</div></div>";

  html += "</div>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<h2>Comunicacions</h2>";
  html += "<div class='grid'>";

  html += "<div class='item'><div class='label'>Wi-Fi</div><div class='value ";
  html += isWifiConnected() ? "ok" : "warn";
  html += "'>";
  html += "<span id='live-wifi'>"; html += wifiStatusText(); html += "</span>";
  html += "</div></div>";

  html += "<div class='item'><div class='label'>IP</div><div class='value'>";
  html += htmlEscape(wifiStaIpText());
  html += "</div></div>";

  html += "<div class='item'><div class='label'>MQTT</div><div class='value ";
  html += configMqttEnabled ? statusClass(isMqttConnected()) : "warn";
  html += "'>";
  html += "<span id='live-mqtt'>"; html += mqttStatusText(); html += "</span>";
  html += "</div></div>";

  html += "<div class='item'><div class='label'>Broker MQTT</div><div class='value'>";
  html += htmlEscape(configMqttHost);
  html += ":";
  html += String(configMqttPort);
  html += "</div></div>";

  html += "<div class='item'><div class='label'>Topic base</div><div class='value'>";
  html += htmlEscape(configMqttTopicBase);
  html += "</div></div>";

  html += "<div class='item'><div class='label'>Interval MQTT</div><div class='value'>";
  html += String(configMqttPublishIntervalSeconds);
  html += " s</div></div>";

  html += "<div class='item'><div class='label'>Home Assistant Discovery</div><div class='value ";
  html += configHaDiscoveryEnabled ? "ok" : "warn";
  html += "'>";
  html += enabledText(configHaDiscoveryEnabled);
  html += "</div></div>";

  html += "<div class='item'><div class='label'>Discovery publicat</div><div class='value ";
  html += appState.mqttDiscoveryPublished ? "ok" : "warn";
  html += "'>";
  html += appState.mqttDiscoveryPublished ? "Si" : "No";
  html += "</div></div>";

  html += "</div>";
  html += "</div>";

  html += "<div class='card small'>";
  html += "Versio firmware: ";
  html += FIRMWARE_VERSION;
  html += "<br>Uptime: ";
  html += String(getUptimeSeconds());
  html += " segons<br>La pagina rep estat en directe per WebSocket, sense refrescos periòdics del navegador.";
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

  html += "</div>";

  html += "<div id='temp-calibration' class='card'>";
  html += "<h2>Calibratge i proteccio de la sonda</h2>";
  html += "<p class='hint'>L'offset s'aplica a la lectura abans de publicar-la. Els valors fora del rang logic es descarten. Les lectures tipiques dolentes de DS18B20, -127 C i 85 C, tambe es descarten.</p>";

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
  html += "<button type='submit'>Guardar temperatura i sonda</button>";
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

  html += "<form method='POST' action='/wifi'>";

  html += "<div>";
  html += "<div class='label'>SSID</div>";
  html += "<input name='ssid' type='text' maxlength='64' value='";
  html += htmlEscape(configWifiSsid);
  html += "'>";
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

  html += "</div>";

  html += "<div id='wifi-network' class='card'>";
  html += "<h2>Xarxa avançada</h2>";
  html += "<p class='hint'>DHCP és l'opcio segura. IP fixa només si ho tens clar. Si la IP fixa queda malament, l'AP de rescat ha de salvar-te.</p>";

  html += "<div>";
  html += "<label><input name='use_static_ip' type='checkbox' value='1' ";
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
  html += "<button type='submit'>Guardar Wi-Fi i reiniciar connexio</button>";
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
  html += "<h2>API local de Home Assistant per historial</h2>";
  html += "<p class='hint'>Aquesta configuració permet que la web de la boia llegeixi l'històric de Home Assistant i dibuixi el gràfic de fons amb les últimes hores configurades sobre la fitxa de temperatura. Necessita un token de llarga durada de Home Assistant.</p>";
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
  html += "<div><div class='label'>Hores d'històric a mostrar</div><input name='ha_history_hours' type='number' min='1' max='168' value='";
  html += String(configHaHistoryHours);
  html += "'><div class='small'>Màxim recomanat: 168 hores. La boia redueix les dades a una mostra per hora per no carregar massa l'ESP32.</div></div>";
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
  bool discoveryOk = !configHaDiscoveryEnabled || appState.mqttDiscoveryPublished;
  bool otaOk = !appState.otaInProgress;
  bool overallReady = wifiOk && mqttOk && sensorOk && tempOk && discoveryOk && otaOk;

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
  html += "<div class='item'><div class='label'>Alimentació</div><div class='value'>5V estable</div><div class='small'>Alimenta la placa per USB/5V segons la teva placa. La DS18B20 millor a 3V3 per evitar nivells de dades de 5V.</div></div>";
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
  html += "<p class='hint'>Aquesta boia ja queda preparada a nivell de documentació i estructura per afegir sensors interns i energia autònoma. No ho activo encara al firmware perquè posar codi sense tenir el mòdul físic concret seria una font d'errors. Primer es reserva i es documenta; després, quan tinguem components exactes, s'implementa.</p>";
  html += "<div class='grid'>";
  html += "<div class='item'><div class='label'>Temperatura interna</div><div class='value'>Preparada</div><div class='small'>Pensada per controlar la temperatura dins del tub, no la temperatura de l'aigua. Sensor recomanat: SHT31 o BME280 per I2C.</div></div>";
  html += "<div class='item'><div class='label'>Humitat interna</div><div class='value'>Preparada</div><div class='small'>Això és important de veritat: si puja la humitat dins la boia, vol dir condensació o entrada d'aigua abans que mori l'electrònica.</div></div>";
  html += "<div class='item'><div class='label'>Bateria</div><div class='value'>Preparada</div><div class='small'>Futur monitoratge de tensió, percentatge estimat, estat de càrrega i alarma de bateria baixa.</div></div>";
  html += "<div class='item'><div class='label'>Placa solar</div><div class='value'>Preparada</div><div class='small'>Futur control de tensió solar, estat de càrrega i diagnòstic de si realment està carregant.</div></div>";
  html += "</div></div>";

  html += "<div class='card'>";
  html += "<h2>GPIO reservats per ampliacions</h2>";
  html += "<p class='hint'>De moment no assignem pins reals perquè depèn del mòdul que compris. És millor no mentir al firmware. Quan triem sensor i carregador, fixem pins i els mostrem aquí com a actius.</p>";
  html += "<div class='grid'>";
  html += "<div class='item'><div class='label'>I2C SDA intern</div><div class='value'>" + htmlEscape(futurePinText(INTERNAL_ENV_I2C_SDA_PIN)) + "</div><div class='small'>Per SHT31/BME280 o sensor intern equivalent.</div></div>";
  html += "<div class='item'><div class='label'>I2C SCL intern</div><div class='value'>" + htmlEscape(futurePinText(INTERNAL_ENV_I2C_SCL_PIN)) + "</div><div class='small'>Mateix bus I2C intern.</div></div>";
  html += "<div class='item'><div class='label'>ADC bateria</div><div class='value'>" + htmlEscape(futurePinText(BATTERY_VOLTAGE_ADC_PIN)) + "</div><div class='small'>Lectura de bateria sempre amb divisor resistiu. Mai posar tensió de bateria directa a l'ESP32.</div></div>";
  html += "<div class='item'><div class='label'>ADC solar</div><div class='value'>" + htmlEscape(futurePinText(SOLAR_VOLTAGE_ADC_PIN)) + "</div><div class='small'>Per saber si la placa solar està donant tensió útil.</div></div>";
  html += "<div class='item'><div class='label'>Estat carregador</div><div class='value'>" + htmlEscape(futurePinText(CHARGER_STATUS_PIN)) + "</div><div class='small'>Opcional: pin CHG/STDBY/FAULT si el mòdul carregador ho exposa.</div></div>";
  html += "</div></div>";

  html += "<div class='card'>";
  html += "<h2>Arquitectura futura d'energia</h2>";
  html += "<div class='grid'>";
  appendChecklistItem(html, "Bateria", htmlEscape("Li-Ion o LiFePO4"), "Cal triar química i carregador abans d'escriure codi. LiFePO4 és més segura; Li-Ion té més densitat. No barregem carregadors.");
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
  html += "<form id='ota-local-form' method='POST' action='/update' enctype='multipart/form-data' data-confirm='Pujar firmware nou? Si el fitxer és incorrecte pots deixar la boia malament.'>";
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
  html += "<div class='ota-progress-head'><div><div class='ota-progress-title'>Progrés d'actualització</div><div id='ota-progress-message' class='ota-progress-text'>";
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
  html += "</div>";

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

  static const char* labels[] = {"Resum", "Identitat", "Mode", "LEDs", "Usuaris"};
  static const char* anchors[] = {"sys-summary", "sys-identity", "sys-mode", "sys-leds", "sys-users"};
  appendSubTabs(html, "Sistema", labels, anchors, 5);

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

  html += "</div>";
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
  html += "</div></div></div></body></html>";
  return html;
}

static void redirectTo(const String& location) {
  server.sendHeader("Location", location, true);
  server.send(303, "text/plain", "");
}

static bool requestHasValidSession() {
  return isWebSessionCookieValid(server.header("Cookie"));
}

static bool requestOriginIsAllowed() {
  String origin = server.header("Origin");
  if (origin.length() == 0) return true;
  return origin == "http://" + server.hostHeader() || origin == "https://" + server.hostHeader();
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
    redirectTo("/login");
    return false;
  }

  if (webAuthPasswordChangeRequired() && uri != "/change-password") {
    redirectTo("/change-password");
    return false;
  }

  if (currentServer.method() != HTTP_GET && !requestOriginIsAllowed()) {
    currentServer.send(403, "text/plain", "Origen de petició no permès");
    return false;
  }

  return next();
}

static bool protectedUploadRequest(WebServer& currentServer) {
  return isWebSessionCookieValid(currentServer.header("Cookie")) &&
         !webAuthPasswordChangeRequired() &&
         requestOriginIsAllowed();
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
  String password = server.arg("password");
  if (!authenticateWebUser(username, password)) {
    lastFailedLoginMillis = millis();
    delay(250);
    server.send(401, "text/html", buildAuthPage("Accés administratiu", "Credencials incorrectes.", false, false));
    return;
  }

  lastFailedLoginMillis = 0;
  String token = createWebSession();
  server.sendHeader("Set-Cookie", "boia_session=" + token + "; Path=/; HttpOnly; SameSite=Strict");
  redirectTo(webAuthPasswordChangeRequired() ? "/change-password" : "/");
}

static void handleLogout() {
  clearWebSession();
  server.sendHeader("Set-Cookie", "boia_session=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
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

  server.sendHeader("Set-Cookie", "boia_session=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
  server.send(200, "text/html", buildAuthPage("Credencials actualitzades", "Ja pots iniciar sessió amb el nou usuari i la nova contrasenya.", false, false));
}

static void handleUserCredentialsPost() {
  handleChangePasswordPost();
}

static void handleRoot() {
  server.send(200, "text/html", buildStatusPage());
}

static void handleConfigGet() {
  server.send(200, "text/html", buildConfigPage());
}

static void handleWifiGet() {
  server.send(200, "text/html", buildWifiPage());
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
  uint16_t haHistoryHours = configHaHistoryHours;
  if (extractJsonUInt16Value(json, "ha_history_hours", uv)) haHistoryHours = uv;
  saveHomeAssistantApiConfig(haApiEnabled, haApiUrl, haApiToken, haHistoryEntity, haHistoryHours);

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

  saveWifiConfig(ssid, password);
  saveNetworkConfig(useStaticIp, staticIp, gateway, subnet, dns1, dns2);

  Serial.println();
  Serial.println("Configuracio Wi-Fi guardada des de la web:");
  Serial.print("SSID: ");
  Serial.println(configWifiSsid);
  Serial.print("Tipus IP: ");
  Serial.println(configWifiUseStaticIp ? "Fixa" : "DHCP");

  server.send(
    200,
    "text/html",
    buildSavedPage("Wi-Fi guardat", "La boia reiniciara la connexio Wi-Fi. Si no connecta, obrira l'AP de rescat.", false)
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

static String haHistoryEndpointUrl(const String& startIso) {
  String base = normalizedHaApiUrl(configHaApiUrl);
  String url = base + "/api/history/period/" + urlEncode(startIso);
  url += "?filter_entity_id=" + urlEncode(configHaHistoryEntityId);
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
  uint16_t hours = server.hasArg("ha_history_hours") ? (uint16_t)server.arg("ha_history_hours").toInt() : DEFAULT_HA_HISTORY_HOURS;

  saveHomeAssistantApiConfig(enabled, apiUrl, apiToken, entityId, hours);

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

  String url = haHistoryEndpointUrl(startIso);
  HTTPClient http;
  int code = -1;
  String payload = "";

  if (url.startsWith("https://")) {
    WiFiClientSecure client;
    client.setInsecure();
    if (!http.begin(client, url)) {
      server.send(200, "application/json", "{\"error\":\"No puc obrir connexió HTTPS amb HA\"}");
      return;
    }
    http.addHeader("Authorization", "Bearer " + configHaApiToken);
    http.addHeader("Accept", "application/json");
    code = http.GET();
    if (code > 0) payload = http.getString();
    http.end();
  } else {
    WiFiClient client;
    if (!http.begin(client, url)) {
      server.send(200, "application/json", "{\"error\":\"No puc obrir connexió HTTP amb HA\"}");
      return;
    }
    http.addHeader("Authorization", "Bearer " + configHaApiToken);
    http.addHeader("Accept", "application/json");
    code = http.GET();
    if (code > 0) payload = http.getString();
    http.end();
  }

  if (code != 200 || payload.length() == 0) {
    String err = "{\"error\":\"HA HTTP ";
    err += String(code);
    if (code == 401) {
      err += ": token no autoritzat. Revisa que sigui un token de llarga durada i que no estigui caducat o copiat malament";
    } else if (code == 404) {
      err += ": URL o entity_id no trobats";
    }
    err += "\"}";
    server.send(200, "application/json", err);
    return;
  }

  String compact = buildCompactHourlyHistoryJson(payload, hours);
  server.send(200, "application/json", compact);
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
    redirectToMaintenanceOta();
    return;
  }

  server.send(
    200,
    "text/html",
    buildSavedPage("Actualitzant des de GitHub", message, true)
  );

  if (ok) {
    delay(1000);
    publishOfflineAndDisconnect();
    delay(500);
    ESP.restart();
  }
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

  delay(1000);

  publishOfflineAndDisconnect();

  delay(500);
  ESP.restart();
}

static void handleUpdateFinished() {
  bool ok = appState.otaSuccess && !Update.hasError();

  String title = ok ? "OTA completada" : "OTA fallida";
  String message = ok
    ? "Firmware rebut correctament. La boia es reiniciara ara."
    : "L'actualitzacio ha fallat o no s'ha rebut cap firmware valid. Mira el monitor serie per veure el detall.";

  appState.otaInProgress = false;
  appState.otaSuccess = ok;
  appState.otaLastMessage = message;

  server.send(
    ok ? 200 : 500,
    "text/html",
    buildSavedPage(title, message, ok)
  );

  if (ok) {
    delay(1000);
    publishOfflineAndDisconnect();
    delay(500);
    ESP.restart();
  }
}

static void handleUpdateUpload() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    Serial.println();
    Serial.print("OTA iniciada: ");
    Serial.println(upload.filename);

    appState.otaInProgress = true;
    appState.otaSuccess = false;
    appState.otaProgressSource = "Local";
    appState.otaProgressPhase = "pujant";
    appState.otaProgressBytes = 0;
    appState.otaProgressTotal = 0;
    appState.otaProgressPercent = 0;
    appState.otaProgressMillis = millis();
    appState.otaLastMessage = "OTA local en curs";
    notifyOtaProgressNow();

    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      appState.otaLastMessage = "ERROR iniciant OTA";
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    appState.otaProgressBytes = upload.totalSize;
    appState.otaProgressTotal = 0;
    appState.otaProgressPercent = 0;
    appState.otaProgressPhase = "escrivint";
    appState.otaProgressMillis = millis();
    appState.otaLastMessage = "OTA local rebuda: " + String(upload.totalSize) + " bytes";
    notifyOtaProgressNow();

    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      appState.otaProgressPhase = "error";
      appState.otaLastMessage = "ERROR escrivint firmware OTA";
      Update.printError(Serial);
      notifyOtaProgressNow();
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.print("OTA completada. Bytes: ");
      Serial.println(upload.totalSize);
      appState.otaProgressPhase = "completada";
      appState.otaProgressPercent = 100;
      appState.otaProgressTotal = upload.totalSize;
      appState.otaProgressBytes = upload.totalSize;
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
    Update.end();
    appState.otaInProgress = false;
    appState.otaSuccess = false;
    appState.otaProgressPhase = "error";
    appState.otaLastMessage = "OTA avortada";
    Serial.println("OTA avortada.");
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
  static const char* collectedHeaders[] = {"Cookie", "Origin"};
  server.collectHeaders(collectedHeaders, 2);
  server.addMiddleware(authMiddleware);

  server.on("/login", HTTP_GET, handleLoginGet);
  server.on("/login", HTTP_POST, handleLoginPost);
  server.on("/logout", HTTP_GET, handleLogout);
  server.on("/change-password", HTTP_GET, handleChangePasswordGet);
  server.on("/change-password", HTTP_POST, handleChangePasswordPost);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/config", HTTP_GET, handleConfigGet);
  server.on("/config", HTTP_POST, handleConfigPost);
  server.on("/wifi", HTTP_GET, handleWifiGet);
  server.on("/wifi", HTTP_POST, handleWifiPost);
  server.on("/wifi-reset", HTTP_POST, handleWifiResetPost);
  server.on("/wifi-network-reset", HTTP_POST, handleWifiNetworkResetPost);
  server.on("/mqtt", HTTP_GET, handleMqttGet);
  server.on("/mqtt", HTTP_POST, handleMqttPost);
  server.on("/mqtt-reset", HTTP_POST, handleMqttResetPost);
  server.on("/mqtt-discovery", HTTP_POST, handleMqttDiscoveryPost);
  server.on("/ha-api", HTTP_POST, handleHaApiPost);
  server.on("/ha-history", HTTP_GET, handleHaHistoryGet);
  server.on("/ha-history-settings", HTTP_POST, handleHaHistorySettingsPost);
  server.on("/system", HTTP_GET, handleSystemGet);
  server.on("/maintenance", HTTP_GET, handleMaintenanceGet);
  server.on("/help", HTTP_GET, handleHelpGet);
  server.on("/hardware", HTTP_GET, handleHardwareGet);
  server.on("/identity", HTTP_POST, handleIdentityPost);
  server.on("/device-mode", HTTP_POST, handleDeviceModePost);
  server.on("/board-leds", HTTP_POST, handleBoardLedsPost);
  server.on("/user-credentials", HTTP_POST, handleUserCredentialsPost);
  server.on("/mqtt-publish-now", HTTP_POST, handleMqttPublishNowPost);
  server.on("/config-export", HTTP_GET, handleConfigExport);
  server.on("/config-import", HTTP_POST, handleConfigImportPost);
  server.on("/diagnostics", HTTP_GET, handleDiagnosticsGet);
  server.on("/firmware", HTTP_GET, handleFirmwareGet);
  server.on("/defaults", HTTP_POST, handleDefaultsPost);
  server.on("/restart", HTTP_POST, handleRestartPost);
  server.on("/update", HTTP_POST, handleUpdateFinished, handleUpdateUpload).setFilter(protectedUploadRequest);
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
}
