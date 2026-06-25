#include "WifiManagerBoia.h"

#include <WiFi.h>

#include "AppConfig.h"

static bool setupApActive = false;


static bool parseIp(const String& value, IPAddress& ip) {
  return ip.fromString(value);
}

static String setupAccessPointPassword() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toLowerCase();
  String suffix = mac.length() >= 6 ? mac.substring(mac.length() - 6) : String("setup1");
  return "boia-" + suffix;
}

static void applyWifiIpConfiguration() {
  if (!configWifiUseStaticIp) {
    IPAddress none(0, 0, 0, 0);
    WiFi.config(none, none, none, none, none);
    Serial.println("Wi-Fi IP: DHCP");
    return;
  }

  IPAddress localIp;
  IPAddress gateway;
  IPAddress subnet;
  IPAddress dns1;
  IPAddress dns2;

  bool ok = parseIp(configWifiStaticIp, localIp)
         && parseIp(configWifiGateway, gateway)
         && parseIp(configWifiSubnet, subnet)
         && parseIp(configWifiDns1, dns1)
         && parseIp(configWifiDns2, dns2);

  if (!ok) {
    IPAddress none(0, 0, 0, 0);
    WiFi.config(none, none, none, none, none);
    Serial.println("AVIS: configuracio IP fixa invalida. Torno a DHCP per seguretat.");
    return;
  }

  if (WiFi.config(localIp, gateway, subnet, dns1, dns2)) {
    Serial.print("Wi-Fi IP fixa configurada: ");
    Serial.println(configWifiStaticIp);
  } else {
    Serial.println("ERROR: WiFi.config ha fallat. El dispositiu intentara DHCP.");
  }
}


bool isWifiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

bool isWifiApActive() {
  return setupApActive;
}

String wifiStatusText() {
  if (isWifiConnected()) {
    return "Connectat";
  }

  if (setupApActive) {
    return "Mode configuracio AP";
  }

  return "Desconnectat";
}

String wifiModeText() {
  if (isWifiConnected() && setupApActive) {
    return "STA + AP";
  }

  if (isWifiConnected()) {
    return "STA";
  }

  if (setupApActive) {
    return "AP configuracio";
  }

  return "Sense Wi-Fi";
}

String wifiStaIpText() {
  if (!isWifiConnected()) {
    return "Sense IP";
  }

  return WiFi.localIP().toString();
}

String wifiApIpText() {
  if (!setupApActive) {
    return "AP inactiu";
  }

  return WiFi.softAPIP().toString();
}

String wifiConfiguredSsidText() {
  return configWifiSsid;
}

String wifiPowerModeText() {
  return configWifiPowerSaveEnabled ? "Estalvi bateria Wi-Fi" : "Màxim rendiment Wi-Fi";
}

bool wifiPowerSaveEnabled() {
  return configWifiPowerSaveEnabled;
}

void applyWifiPowerMode() {
  WiFi.setSleep(configWifiPowerSaveEnabled);

  Serial.print("Mode energia Wi-Fi: ");
  Serial.println(wifiPowerModeText());
}

static void startSetupAccessPoint() {
  if (setupApActive) {
    return;
  }

  Serial.println();
  Serial.println("Iniciant AP de configuracio...");

  WiFi.mode(WIFI_AP_STA);
  applyWifiPowerMode();

  String apPassword = setupAccessPointPassword();
  bool ok = WiFi.softAP(WIFI_AP_SSID, apPassword.c_str());

  if (ok) {
    setupApActive = true;
    Serial.print("AP actiu: ");
    Serial.println(WIFI_AP_SSID);
    Serial.print("Password AP: ");
    Serial.println(apPassword);
    Serial.print("IP AP: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("ERROR: No s'ha pogut iniciar l'AP de configuracio.");
  }
}

static void stopSetupAccessPoint() {
  if (!setupApActive) {
    return;
  }

  WiFi.softAPdisconnect(true);
  setupApActive = false;
  WiFi.mode(WIFI_STA);
  applyWifiPowerMode();

  Serial.println("AP de configuracio aturat.");
}

void printWifiStatus() {
  Serial.println();
  Serial.println("--- Estat Wi-Fi ---");

  Serial.print("Mode: ");
  Serial.println(wifiModeText());

  Serial.print("SSID configurat: ");
  Serial.println(configWifiSsid);

  if (isWifiConnected()) {
    Serial.println("Wi-Fi STA connectat");

    Serial.print("SSID connectat: ");
    Serial.println(WiFi.SSID());

    Serial.print("IP local: ");
    Serial.println(WiFi.localIP());

    Serial.print("RSSI senyal: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");

    Serial.print("MAC STA: ");
    Serial.println(WiFi.macAddress());

    Serial.print("Hostname: ");
    Serial.println(configDeviceHostname);

    Serial.print("Tipus IP: ");
    Serial.println(configWifiUseStaticIp ? "Fixa" : "DHCP");

    Serial.print("Energia Wi-Fi: ");
    Serial.println(wifiPowerModeText());

    Serial.print("Gateway: ");
    Serial.println(WiFi.gatewayIP());

    Serial.print("Subnet: ");
    Serial.println(WiFi.subnetMask());

    Serial.print("DNS: ");
    Serial.println(WiFi.dnsIP());
  } else {
    Serial.println("Wi-Fi STA NO connectat");

    Serial.print("Codi estat Wi-Fi: ");
    Serial.println(WiFi.status());
  }

  if (setupApActive) {
    Serial.print("AP SSID: ");
    Serial.println(WIFI_AP_SSID);

    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
  }

  Serial.println("-------------------");
}

void connectWifi() {
  if (isWifiConnected()) {
    return;
  }

  if (!hasWifiConfig()) {
    Serial.println("No hi ha SSID configurat. Entro directament en mode AP.");
    startSetupAccessPoint();
    return;
  }

  Serial.println();
  Serial.print("Connectant al Wi-Fi: ");
  Serial.println(configWifiSsid);

  if (setupApActive) {
    WiFi.mode(WIFI_AP_STA);
  } else {
    WiFi.mode(WIFI_STA);
  }

  applyWifiIpConfiguration();
  applyWifiPowerMode();

  WiFi.setHostname(configDeviceHostname.c_str());
  Serial.print("Hostname Wi-Fi: ");
  Serial.println(configDeviceHostname);

  WiFi.begin(configWifiSsid.c_str(), configWifiPassword.c_str());

  unsigned long startAttempt = millis();

  while (!isWifiConnected() && millis() - startAttempt < WIFI_CONNECT_TIMEOUT_MS) {
    Serial.print(".");
    delay(500);
  }

  Serial.println();

  if (isWifiConnected()) {
    Serial.println("Wi-Fi connectat correctament.");

    if (setupApActive) {
      stopSetupAccessPoint();
    }

    printWifiStatus();

    Serial.print("Web disponible a: http://");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("ERROR: No s'ha pogut connectar al Wi-Fi configurat.");
    startSetupAccessPoint();
    printWifiStatus();

    Serial.println("Connecta't a l'AP de configuracio i obre: http://192.168.4.1");
  }
}

void checkWifiConnection() {
  if (isWifiConnected()) {
    if (setupApActive) {
      stopSetupAccessPoint();
    }
    return;
  }

  Serial.println();
  Serial.println("AVIS: Wi-Fi STA desconnectat. Reintentant...");
  connectWifi();
}

void restartWifiWithCurrentConfig() {
  Serial.println();
  Serial.println("Reiniciant connexio Wi-Fi amb la configuracio actual...");

  WiFi.disconnect(true, true);
  delay(500);
  setupApActive = false;

  connectWifi();
}

void resetWifiAndRestart() {
  resetWifiConfigToDefaults();
  restartWifiWithCurrentConfig();
}
