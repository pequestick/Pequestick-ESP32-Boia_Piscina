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

    def test_browser_assisted_ota_keeps_sha256_verification(self):
        source = Path("src/WebServerBoia.cpp").read_text(encoding="utf-8")
        self.assertIn("installGithubViaBrowser", source)
        self.assertIn("X-Firmware-SHA256", source)
        self.assertIn("SHA-256 OTA verificat correctament", source)

    def test_upload_route_is_available_before_headers_are_parsed(self):
        auth_source = Path("src/AuthManager.cpp").read_text(encoding="utf-8")
        web_source = Path("src/WebServerBoia.cpp").read_text(encoding="utf-8")
        self.assertIn("String createWebUploadToken()", auth_source)
        self.assertIn('localOtaUploadPath = "/update/" + createWebUploadToken()', web_source)
        self.assertIn("server.on(localOtaUploadPath.c_str()", web_source)
        self.assertNotIn("setFilter(protectedUploadRequest)", web_source)
        self.assertIn("xhr.open('POST',local.action)", web_source)

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

    def test_browser_ota_uploads_resumable_blocks(self):
        web = Path("src/WebServerBoia.cpp").read_text(encoding="utf-8")
        self.assertIn("sendFirmwareBlock", web)
        self.assertIn("blockSize=65536", web)
        self.assertIn("X-Firmware-Offset", web)
        self.assertIn("requestedOffset == localOtaReceivedSize", web)
        self.assertIn("es pot reprendre des del byte", web)


if __name__ == "__main__":
    unittest.main()
