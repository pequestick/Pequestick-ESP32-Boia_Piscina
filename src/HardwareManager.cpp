#include "HardwareManager.h"
#include "AppConfig.h"

#include <WiFi.h>
#if INTERNAL_BOARD_LED_IS_RGB
#include "esp32-hal-rgb-led.h"
#endif

#include "AppState.h"
#include "WifiManagerBoia.h"
#include "MqttManager.h"

static bool buttonWasPressed = false;
static unsigned long buttonPressedAt = 0;
static unsigned long lastButtonSampleAt = 0;
static unsigned long lastLedChangeAt = 0;
static bool ledLevel = false;
static bool boardLedLevel = false;
static unsigned long lastBoardHeartbeatAt = 0;
static bool boardHeartbeatLevel = false;
static String lastHardwareAction = "Cap accio fisica encara";

static bool buttonPressedRaw() {
  if (!RESET_BUTTON_ENABLED) {
    return false;
  }

  int value = digitalRead(RESET_BUTTON_PIN);
  return RESET_BUTTON_ACTIVE_LOW ? (value == LOW) : (value == HIGH);
}

static void writeBoardLed(bool on) {
#if INTERNAL_BOARD_LED_PIN >= 0
  boardLedLevel = on;

  if (!configBoardLedEnabled) {
#if INTERNAL_BOARD_LED_IS_RGB
    rgbLedWrite(INTERNAL_BOARD_LED_PIN, 0, 0, 0);
#else
    digitalWrite(INTERNAL_BOARD_LED_PIN, LOW);
#endif
    return;
  }

#if INTERNAL_BOARD_LED_IS_RGB
  // RGB intern tipus NeoPixel. Verd suau quan està actiu.
  rgbLedWrite(INTERNAL_BOARD_LED_PIN, on ? 0 : 0, on ? 24 : 0, on ? 0 : 0);
#else
  digitalWrite(INTERNAL_BOARD_LED_PIN, on ? HIGH : LOW);
#endif
#else
  (void)on;
#endif
}

static bool statusLedOutputAvailable() {
  return STATUS_LED_ENABLED || (configBoardLedEnabled && configBoardLedMirrorStatus);
}

static void writeLed(bool on) {
  ledLevel = on;

  if (STATUS_LED_ENABLED) {
    bool physicalLevel = STATUS_LED_ACTIVE_LOW ? !on : on;
    digitalWrite(STATUS_LED_PIN, physicalLevel ? HIGH : LOW);
  }

  if (configBoardLedEnabled && configBoardLedMirrorStatus) {
    writeBoardLed(on);
  }
}

static void updateBoardLedHeartbeat(unsigned long now) {
  if (!configBoardLedEnabled) {
    writeBoardLed(false);
    return;
  }

  if (configBoardLedMirrorStatus) {
    return;
  }

  if (now - lastBoardHeartbeatAt >= 1800) {
    lastBoardHeartbeatAt = now;
    boardHeartbeatLevel = !boardHeartbeatLevel;
    writeBoardLed(boardHeartbeatLevel);
  }
}

static void blinkLed(unsigned long now, unsigned long intervalMs) {
  if (!statusLedOutputAvailable()) {
    return;
  }

  if (now - lastLedChangeAt >= intervalMs) {
    lastLedChangeAt = now;
    writeLed(!ledLevel);
  }
}

static void updateStatusLed() {
  unsigned long now = millis();
  updateBoardLedHeartbeat(now);

  if (!statusLedOutputAvailable()) {
    return;
  }


  if (isWifiApActive()) {
    blinkLed(now, 150);       // AP setup actiu: parpelleig rapid
    return;
  }

  if (!isWifiConnected()) {
    blinkLed(now, 650);       // Wi-Fi connectant/fallant: parpelleig lent
    return;
  }

  if (appState.sensorStatus == "ERROR") {
    blinkLed(now, 250);       // Sonda fotuda: parpelleig curt
    return;
  }

  if (configMqttEnabled && !isMqttConnected()) {
    blinkLed(now, 1000);      // MQTT no connectat: avís lent
    return;
  }

  writeLed(true);             // Tot OK: LED fix
}

static void doWifiRescueReset() {
  lastHardwareAction = "Reset Wi-Fi fisic: AP de configuracio forcat";
  Serial.println();
  Serial.println("BOTO FISIC: reset Wi-Fi i AP de configuracio.");
  forceWifiSetupMode();
  delay(400);
  ESP.restart();
}

static void doFactoryReset() {
  lastHardwareAction = "Reset total fisic: configuracio netejada i AP forcat";
  Serial.println();
  Serial.println("BOTO FISIC: reset total de configuracio.");
  factoryResetConfigAndSetupMode();
  delay(400);
  ESP.restart();
}

static void doRestart() {
  lastHardwareAction = "Reinici fisic";
  Serial.println();
  Serial.println("BOTO FISIC: reinici.");
  publishOfflineAndDisconnect();
  delay(400);
  ESP.restart();
}

void initHardwareManager() {
  if (RESET_BUTTON_ENABLED) {
    pinMode(RESET_BUTTON_PIN, RESET_BUTTON_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
  }

  if (STATUS_LED_ENABLED) {
    pinMode(STATUS_LED_PIN, OUTPUT);
  }

#if INTERNAL_BOARD_LED_PIN >= 0
#if !INTERNAL_BOARD_LED_IS_RGB
  pinMode(INTERNAL_BOARD_LED_PIN, OUTPUT);
#endif
#endif

  writeLed(false);
  writeBoardLed(false);

  appState.hardwareReady = true;
  appState.buttonPressDurationMs = 0;
  appState.buttonPressed = false;
  appState.lastHardwareAction = lastHardwareAction;

  Serial.println();
  Serial.println("Hardware fisic inicialitzat:");
  Serial.print("- Boto reset: ");
  Serial.println(RESET_BUTTON_ENABLED ? ("GPIO" + String(RESET_BUTTON_PIN)) : "desactivat");
  Serial.print("- LED estat extern: ");
  Serial.println(STATUS_LED_ENABLED ? ("GPIO" + String(STATUS_LED_PIN)) : "desactivat");
  Serial.print("- LED intern placa: ");
  Serial.print(configBoardLedEnabled ? ("GPIO" + String(INTERNAL_BOARD_LED_PIN)) : "desactivat");
  Serial.print(" · mode: ");
  Serial.println(configBoardLedMirrorStatus ? "mirall estat" : "heartbeat");
}

void handleHardwareManager() {
  updateStatusLed();

  if (!RESET_BUTTON_ENABLED) {
    return;
  }

  unsigned long now = millis();
  if (now - lastButtonSampleAt < BUTTON_DEBOUNCE_MS) {
    return;
  }
  lastButtonSampleAt = now;

  bool pressed = buttonPressedRaw();
  appState.buttonPressed = pressed;

  if (pressed && !buttonWasPressed) {
    buttonWasPressed = true;
    buttonPressedAt = now;
    appState.buttonPressDurationMs = 0;
    lastHardwareAction = "Boto premut";
  }

  if (pressed && buttonWasPressed) {
    appState.buttonPressDurationMs = now - buttonPressedAt;
  }

  if (!pressed && buttonWasPressed) {
    buttonWasPressed = false;
    unsigned long duration = now - buttonPressedAt;
    appState.buttonPressDurationMs = 0;

    if (duration >= BUTTON_FACTORY_RESET_MS) {
      doFactoryReset();
    } else if (duration >= BUTTON_WIFI_RESET_MS) {
      doWifiRescueReset();
    } else if (duration >= BUTTON_RESTART_MS) {
      doRestart();
    } else {
      lastHardwareAction = "Pulsacio curta: estat LED actualitzat";
      Serial.println("BOTO FISIC: pulsacio curta.");
    }
  }

  appState.lastHardwareAction = lastHardwareAction;
}

String hardwareButtonStatusText() {
  if (!RESET_BUTTON_ENABLED) {
    return "Desactivat";
  }

  if (appState.buttonPressed) {
    return "Premut " + String(appState.buttonPressDurationMs / 1000.0f, 1) + " s";
  }

  return "En repos";
}

String hardwareLedStatusText() {
  if (!STATUS_LED_ENABLED) {
    return "Desactivat";
  }

  if (isWifiApActive()) {
    return "Parpelleig rapid: AP setup actiu";
  }

  if (!isWifiConnected()) {
    return "Parpelleig lent: Wi-Fi no connectat";
  }

  if (appState.sensorStatus == "ERROR") {
    return "Parpelleig curt: error sonda";
  }

  if (configMqttEnabled && !isMqttConnected()) {
    return "Parpelleig molt lent: MQTT no connectat";
  }

  return "Fix: tot OK";
}

String hardwareBoardLedStatusText() {
#if INTERNAL_BOARD_LED_PIN < 0
  return "No definit al firmware";
#else
  if (!configBoardLedEnabled) {
    return "Desactivat";
  }
  return configBoardLedMirrorStatus
    ? ("Actiu · mirall del LED d'estat · GPIO" + String(INTERNAL_BOARD_LED_PIN))
    : ("Actiu · heartbeat intern · GPIO" + String(INTERNAL_BOARD_LED_PIN));
#endif
}

String hardwareLastActionText() {
  return lastHardwareAction;
}

String hardwareResetButtonPinText() {
  return RESET_BUTTON_ENABLED ? ("GPIO" + String(RESET_BUTTON_PIN)) : "Desactivat";
}

String hardwareStatusLedPinText() {
  return STATUS_LED_ENABLED ? ("GPIO" + String(STATUS_LED_PIN)) : "Desactivat";
}

String hardwareReadyStateText() {
  return appState.hardwareReady ? "Inicialitzat" : "No inicialitzat";
}
