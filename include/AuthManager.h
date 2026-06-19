#pragma once

#include <Arduino.h>

void initAuthManager();

bool authenticateWebUser(const String& username, const String& password);
bool changeWebCredentials(
  const String& currentPassword,
  const String& newUsername,
  const String& newPassword,
  String& errorMessage
);

String webAuthUsername();
bool webAuthPasswordChangeRequired();

String createWebSession();
bool isWebSessionCookieValid(const String& cookieHeader);
void clearWebSession();

