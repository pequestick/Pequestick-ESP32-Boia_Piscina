#include "AuthManager.h"

#include <Preferences.h>
#include <esp_system.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>

namespace {
const char* AUTH_NAMESPACE = "boia_auth";
const char* INITIAL_USERNAME = "admin";
const char* INITIAL_PASSWORD = "1234";
const uint8_t AUTH_SCHEMA_VERSION = 2;

Preferences authPreferences;
String authUsername;
String authSalt;
String authPasswordHash;
bool passwordChangeRequired = true;
String persistentSessionToken;

String bytesToHex(const uint8_t* bytes, size_t length) {
  static const char* HEX_CHARS = "0123456789abcdef";
  String output;
  output.reserve(length * 2);
  for (size_t i = 0; i < length; i++) {
    output += HEX_CHARS[(bytes[i] >> 4) & 0x0f];
    output += HEX_CHARS[bytes[i] & 0x0f];
  }
  return output;
}

String randomHex(size_t byteCount) {
  String output;
  output.reserve(byteCount * 2);
  while (byteCount > 0) {
    uint32_t value = esp_random();
    size_t chunk = byteCount > sizeof(value) ? sizeof(value) : byteCount;
    output += bytesToHex(reinterpret_cast<const uint8_t*>(&value), chunk);
    byteCount -= chunk;
  }
  return output;
}

String passwordHash(const String& salt, const String& password) {
  uint8_t digest[32];
  int result = mbedtls_pkcs5_pbkdf2_hmac_ext(
    MBEDTLS_MD_SHA256,
    reinterpret_cast<const unsigned char*>(password.c_str()),
    password.length(),
    reinterpret_cast<const unsigned char*>(salt.c_str()),
    salt.length(),
    10000,
    sizeof(digest),
    digest
  );
  if (result != 0) return "";
  return bytesToHex(digest, sizeof(digest));
}

bool constantTimeEquals(const String& left, const String& right) {
  if (left.length() != right.length()) return false;
  uint8_t difference = 0;
  for (size_t i = 0; i < left.length(); i++) {
    difference |= static_cast<uint8_t>(left.charAt(i) ^ right.charAt(i));
  }
  return difference == 0;
}

String cookieValue(const String& cookieHeader, const String& name) {
  String prefix = name + "=";
  int start = cookieHeader.indexOf(prefix);
  while (start >= 0 && start > 0 && cookieHeader.charAt(start - 1) != ' ' && cookieHeader.charAt(start - 1) != ';') {
    start = cookieHeader.indexOf(prefix, start + 1);
  }
  if (start < 0) return "";
  start += prefix.length();
  int end = cookieHeader.indexOf(';', start);
  if (end < 0) end = cookieHeader.length();
  String value = cookieHeader.substring(start, end);
  value.trim();
  return value;
}
}

void initAuthManager() {
  authPreferences.begin(AUTH_NAMESPACE, false);
  uint8_t storedSchema = authPreferences.getUChar("schema", 0);
  authUsername = authPreferences.getString("username", "");
  authSalt = authPreferences.getString("salt", "");
  authPasswordHash = authPreferences.getString("pass_hash", "");

  if (storedSchema < AUTH_SCHEMA_VERSION || authUsername.length() == 0 || authSalt.length() == 0 || authPasswordHash.length() == 0) {
    authPreferences.clear();
    authUsername = INITIAL_USERNAME;
    authSalt = randomHex(16);
    authPasswordHash = passwordHash(authSalt, INITIAL_PASSWORD);
    passwordChangeRequired = true;
    authPreferences.putString("username", authUsername);
    authPreferences.putString("salt", authSalt);
    authPreferences.putString("pass_hash", authPasswordHash);
    authPreferences.putBool("must_change", true);
    authPreferences.putUChar("schema", AUTH_SCHEMA_VERSION);
  } else {
    passwordChangeRequired = authPreferences.getBool("must_change", true);
  }

  persistentSessionToken = authPreferences.getString("session_v2", "");
  if (persistentSessionToken.length() != 64) {
    persistentSessionToken = randomHex(32);
    authPreferences.putString("session_v2", persistentSessionToken);
  }
  authPreferences.end();
}

bool authenticateWebUser(const String& username, const String& password) {
  return constantTimeEquals(username, authUsername) &&
         constantTimeEquals(passwordHash(authSalt, password), authPasswordHash);
}

bool changeWebCredentials(
  const String& currentPassword,
  const String& newUsernameValue,
  const String& newPassword,
  String& errorMessage
) {
  String newUsername = newUsernameValue;
  newUsername.trim();

  if (!authenticateWebUser(authUsername, currentPassword)) {
    errorMessage = "La contrasenya actual no és correcta.";
    return false;
  }
  if (newUsername.length() < 3 || newUsername.length() > 32) {
    errorMessage = "L'usuari ha de tenir entre 3 i 32 caràcters.";
    return false;
  }
  if (newUsername.indexOf(' ') >= 0) {
    errorMessage = "L'usuari no pot contenir espais.";
    return false;
  }
  if (newPassword.length() < 8 || newPassword.length() > 64) {
    errorMessage = "La contrasenya nova ha de tenir entre 8 i 64 caràcters.";
    return false;
  }
  if (newPassword == INITIAL_PASSWORD) {
    errorMessage = "La contrasenya nova no pot ser la contrasenya inicial.";
    return false;
  }

  authUsername = newUsername;
  authSalt = randomHex(16);
  authPasswordHash = passwordHash(authSalt, newPassword);
  passwordChangeRequired = false;

  authPreferences.begin(AUTH_NAMESPACE, false);
  authPreferences.putString("username", authUsername);
  authPreferences.putString("salt", authSalt);
  authPreferences.putString("pass_hash", authPasswordHash);
  authPreferences.putBool("must_change", false);
  authPreferences.putUChar("schema", AUTH_SCHEMA_VERSION);
  authPreferences.end();

  clearWebSession();
  errorMessage = "Credencials actualitzades.";
  return true;
}

String webAuthUsername() {
  return authUsername;
}

bool webAuthPasswordChangeRequired() {
  return passwordChangeRequired;
}

String createWebSession() {
  if (persistentSessionToken.length() != 64) {
    persistentSessionToken = randomHex(32);
    authPreferences.begin(AUTH_NAMESPACE, false);
    authPreferences.putString("session_v2", persistentSessionToken);
    authPreferences.end();
  }
  return persistentSessionToken;
}

bool isWebSessionCookieValid(const String& cookieHeader) {
  String suppliedToken = cookieValue(cookieHeader, "boia_session_v2");
  return suppliedToken.length() == 64 &&
         persistentSessionToken.length() == 64 &&
         constantTimeEquals(suppliedToken, persistentSessionToken);
}

void clearWebSession() {
  persistentSessionToken = randomHex(32);
  authPreferences.begin(AUTH_NAMESPACE, false);
  authPreferences.putString("session_v2", persistentSessionToken);
  authPreferences.end();
}
