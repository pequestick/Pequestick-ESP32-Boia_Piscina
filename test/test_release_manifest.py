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


if __name__ == "__main__":
    unittest.main()
