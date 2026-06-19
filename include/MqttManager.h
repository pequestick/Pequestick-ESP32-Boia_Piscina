#pragma once

#include <Arduino.h>

void initMqtt();
void mqttLoop();
void connectMqtt();
void checkMqttConnection();
void reconfigureMqtt();

void publishMqttTelemetry();
void publishMqttConfigState();
void publishHomeAssistantDiscovery();

bool isMqttConnected();
String mqttStatusText();
String mqttTopic(const String& suffix);
String mqttAvailabilityTopic();
String mqttCommandRestartTopic();
String mqttCommandPublishDiscoveryTopic();
String haDiscoveryBaseTopic();

void publishOfflineAndDisconnect();
