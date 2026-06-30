import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from tools.generate_manifest import build_manifest


class ReleaseManifestTests(unittest.TestCase):
    def test_manifest_uses_firmware_size_and_sha256(self):
        with tempfile.TemporaryDirectory() as directory:
            firmware = Path(directory) / "firmware.bin"
            firmware.write_bytes(b"esp32-firmware")

            manifest = build_manifest(
                firmware,
                "1.2.3",
                "abcdef1234567890",
                "2026-06-19T00:00:00Z",
                "https://raw.githubusercontent.com/example/firmware.bin",
                "Release notes",
            )

            self.assertEqual(manifest["size"], len(b"esp32-firmware"))
            self.assertEqual(
                manifest["sha256"],
                hashlib.sha256(b"esp32-firmware").hexdigest(),
            )
            self.assertEqual(manifest["build_short_sha"], "abcdef123456")

    def test_manifest_notes_are_valid_json_when_they_contain_quotes(self):
        with tempfile.TemporaryDirectory() as directory:
            firmware = Path(directory) / "firmware.bin"
            firmware.write_bytes(b"firmware")
            manifest = build_manifest(
                firmware,
                "1.0.0",
                "a" * 40,
                "2026-06-19T00:00:00Z",
                "https://example.invalid/firmware.bin",
                'Canvi "important"',
            )

            encoded = json.dumps(manifest, ensure_ascii=False)
            self.assertEqual(json.loads(encoded)["notes"], 'Canvi "important"')

    def test_default_network_credentials_are_empty(self):
        source = Path("src/AppConfig.cpp").read_text(encoding="utf-8")
        self.assertIn('DEFAULT_WIFI_SSID = ""', source)
        self.assertIn('DEFAULT_WIFI_PASSWORD = ""', source)
        self.assertIn('DEFAULT_MQTT_USER = ""', source)
        self.assertIn('DEFAULT_MQTT_PASSWORD = ""', source)

    def test_embedded_javascript_has_no_single_quote_cpp_escape(self):
        source = Path("src/WebServerBoia.cpp").read_text(encoding="utf-8")
        script = source.split('html += "<script>";', 1)[1].split(
            'html += "</script>";', 1
        )[0]
        self.assertNotIn("\\'", script)

    def test_origin_validation_supports_mobile_webviews(self):
        source = Path("src/WebServerBoia.cpp").read_text(encoding="utf-8")
        self.assertIn('if (origin == "null") return true;', source)
        self.assertIn("originAuthority.equalsIgnoreCase(requestAuthority)", source)

    def test_web_session_uses_a_persistent_versioned_cookie(self):
        auth_source = Path("src/AuthManager.cpp").read_text(encoding="utf-8")
        web_source = Path("src/WebServerBoia.cpp").read_text(encoding="utf-8")
        self.assertIn('getString("session_v2"', auth_source)
        self.assertIn('cookieValue(cookieHeader, "boia_session_v2")', auth_source)
        self.assertIn("boia_session_v2=", web_source)
        self.assertIn("Max-Age=604800", web_source)
        self.assertIn("Lectures públiques", web_source)

    def test_github_ota_uses_one_stream_and_range_only_for_resume(self):
        source = Path("src/GitHubOta.cpp").read_text(encoding="utf-8")
        self.assertIn("class OtaUpdateStream", source)
        self.assertIn("http.writeToStream(&updateStream)", source)
        self.assertIn("if (startByte > 0)", source)
        self.assertNotIn("maxRangeBlockSize", source)

    def test_server_side_ota_keeps_sha256_verification(self):
        source = Path("src/WebServerBoia.cpp").read_text(encoding="utf-8")
        self.assertIn("X-Firmware-SHA256", source)
        self.assertIn("SHA-256 OTA verificat correctament", source)
        self.assertIn("action='/github-update'", source)
        self.assertIn("performGitHubOtaUpdate", source)

    def test_upload_route_is_available_before_headers_are_parsed(self):
        auth_source = Path("src/AuthManager.cpp").read_text(encoding="utf-8")
        web_source = Path("src/WebServerBoia.cpp").read_text(encoding="utf-8")
        self.assertIn("String createWebUploadToken()", auth_source)
        self.assertIn('localOtaUploadPath = "/update/" + createWebUploadToken()', web_source)
        self.assertIn("server.on(localOtaUploadPath.c_str()", web_source)
        self.assertNotIn("setFilter(protectedUploadRequest)", web_source)
        self.assertIn("id='ota-local-form'", web_source)

    def test_sht41_is_wired_into_status_mqtt_and_home_assistant(self):
        config = Path("include/AppConfig.h").read_text(encoding="utf-8")
        sensor = Path("src/InternalEnvSensor.cpp").read_text(encoding="utf-8")
        mqtt = Path("src/MqttManager.cpp").read_text(encoding="utf-8")
        web = Path("src/WebServerBoia.cpp").read_text(encoding="utf-8")
        self.assertIn("INTERNAL_ENV_I2C_SDA_PIN 6", config)
        self.assertIn("INTERNAL_ENV_I2C_SCL_PIN 7", config)
        self.assertIn("SHT41_MEASURE_HIGH_PRECISION", sensor)
        self.assertIn("sht41Crc", sensor)
        self.assertIn('discoveryTopic("sensor", "internal_humidity")', mqtt)
        self.assertIn("internal_temperature_c", web)

    def test_internal_buoy_environment_alarms_are_exposed_to_home_assistant(self):
        mqtt = Path("src/MqttManager.cpp").read_text(encoding="utf-8")
        web = Path("src/WebServerBoia.cpp").read_text(encoding="utf-8")
        self.assertIn('discoveryTopic("binary_sensor", "internal_temperature_alarm")', mqtt)
        self.assertIn('discoveryTopic("binary_sensor", "internal_humidity_alarm")', mqtt)
        self.assertIn('discoveryTopic("switch", "internal_env_alarms")', mqtt)
        self.assertIn('discoveryTopic("number", "internal_temp_alarm")', mqtt)
        self.assertIn('discoveryTopic("number", "internal_humidity_alarm")', mqtt)
        self.assertIn("Temperatura interior de la boia", mqtt)
        self.assertIn("internal_temperature_alarm_threshold_c", web)
        self.assertIn("internal_humidity_alarm_threshold_percent", web)

    def test_local_ota_upload_route_validates_offsets(self):
        web = Path("src/WebServerBoia.cpp").read_text(encoding="utf-8")
        self.assertIn("X-Firmware-Offset", web)
        self.assertIn("requestedOffset == localOtaReceivedSize", web)
        self.assertIn("es pot reprendre des del byte", web)
        self.assertNotIn("sendFirmwareBlock", web)

    def test_sd_explorer_builds_child_paths_from_open_directory(self):
        sd = Path("src/SdManager.cpp").read_text(encoding="utf-8")
        self.assertIn("childPathForDirectoryEntry", sd)
        self.assertIn("String name = file.name();", sd)
        self.assertIn("return base + \"/\" + name;", sd)
        self.assertNotIn("clean + itemPath", sd)

    def test_deep_sleep_battery_saver_is_configurable_and_guarded(self):
        config = Path("src/AppConfig.cpp").read_text(encoding="utf-8")
        header = Path("include/AppConfig.h").read_text(encoding="utf-8")
        web = Path("src/WebServerBoia.cpp").read_text(encoding="utf-8")
        main = Path("src/main.cpp").read_text(encoding="utf-8")

        self.assertIn("DEFAULT_DEEP_SLEEP_ENABLED", config)
        self.assertIn("saveDeepSleepConfig", header)
        self.assertIn("deep_sleep_enabled", web)
        self.assertIn("esp_deep_sleep_start()", main)
        self.assertIn("isWifiApActive()", main)
        self.assertIn("appState.otaInProgress", main)

    def test_boot_blackbox_records_decoded_reset_history(self):
        config = Path("src/AppConfig.cpp").read_text(encoding="utf-8")
        state = Path("include/AppState.h").read_text(encoding="utf-8")
        sd = Path("src/SdManager.cpp").read_text(encoding="utf-8")
        web = Path("src/WebServerBoia.cpp").read_text(encoding="utf-8")

        self.assertIn("SD_BOOT_HISTORY_FILE", config)
        self.assertIn("resetReason", state)
        self.assertIn("previousBootAction", state)
        self.assertIn("resetReasonText", sd)
        self.assertIn("ESP_RST_BROWNOUT", sd)
        self.assertIn("BOOT_TRACE_NAMESPACE", sd)
        self.assertIn("rememberBootTrace", sd)
        self.assertIn("previous_action", sd)
        self.assertIn("previous_battery_voltage", sd)
        self.assertIn("appendSdBootHistory", sd)
        self.assertIn("boot_history.jsonl", web)
        self.assertIn("wakeup_cause", web)
        self.assertIn("previous_action", web)

    def test_storage_page_avoids_large_inline_sd_previews(self):
        web = Path("src/WebServerBoia.cpp").read_text(encoding="utf-8")

        self.assertNotIn("sdReadTextFileLimited(String(SD_BOOT_HISTORY_FILE)", web)
        self.assertIn("sdReadTextFileLimited(sdPendingMqttPathText(), 1024", web)
        self.assertIn("sdReadTextFileLimited(sdSystemLogPathText(), 2048", web)
        self.assertIn("sdReadTextFileLimited(String(SD_BOOT_BLACKBOX_FILE), 2048", web)
        self.assertIn("sdReadTextFileLimited(clean, 8192", web)

    def test_system_mode_page_has_small_dedicated_render_path(self):
        web = Path("src/WebServerBoia.cpp").read_text(encoding="utf-8")

        self.assertIn("appendSystemModeCard", web)
        self.assertIn('selectedSection == "sys-mode"', web)
        self.assertIn('server.uri() == "/system/action"', web)
        self.assertIn('"/system?section=" + section', web)

    def test_large_html_pages_are_sent_in_chunks(self):
        web = Path("src/WebServerBoia.cpp").read_text(encoding="utf-8")

        self.assertIn("sendHtmlResponse", web)
        self.assertIn("CONTENT_LENGTH_UNKNOWN", web)
        self.assertIn("server.sendContent", web)
        self.assertIn('String section = server.arg("section")', web)
        self.assertIn('if (section == "help-firmware")', web)
        self.assertIn('if (section == "help-files")', web)
        self.assertNotIn('server.send(200, "text/html", buildStoragePage())', web)
        self.assertNotIn('server.send(200, "text/html", buildSystemPage())', web)
        self.assertIn('server.send(200, "application/x-ndjson", "")', web)

    def test_home_assistant_statistics_cover_all_history_ranges_and_sensors(self):
        config = Path("src/AppConfig.cpp").read_text(encoding="utf-8")
        web = Path("src/WebServerBoia.cpp").read_text(encoding="utf-8")

        self.assertIn("/api/services/recorder/get_statistics?return_response", web)
        self.assertIn('request += "\\\"types\\\":[\\\"mean\\\",\\\"min\\\",\\\"max\\\"]}"', web)
        for history_range in ("48h", "31d", "6m", "1y"):
            self.assertIn("key==='" + history_range + "'", web)
        self.assertIn("configHaInternalTemperatureEntityId", web)
        self.assertIn("configHaInternalHumidityEntityId", web)
        self.assertIn("configHaBatteryEntityId", web)
        self.assertIn('preferences.getString("ha_battery"', config)
        self.assertIn("Mínim–màxim", web)
        self.assertNotIn("<h2>Lectures</h2>", web)
        self.assertNotIn("<h2>Comunicacions</h2>", web)


if __name__ == "__main__":
    unittest.main()
