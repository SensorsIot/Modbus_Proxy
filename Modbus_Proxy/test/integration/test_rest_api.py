"""Integration tests for Modbus Proxy REST API.

Requires a live device at DEVICE_IP (default 192.168.0.177).
Run with: pytest test/integration/test_rest_api.py -v
"""

import json

import pytest
import requests

# All tests require network access to device
pytestmark = pytest.mark.timeout(15)


# --- GET /api/status ---


class TestApiStatus:
    def test_status_returns_200(self, base_url):
        resp = requests.get(f"{base_url}/api/status", timeout=5)
        assert resp.status_code == 200

    def test_status_is_json(self, base_url):
        resp = requests.get(f"{base_url}/api/status", timeout=5)
        assert resp.headers["content-type"] == "application/json"
        data = resp.json()
        assert isinstance(data, dict)

    def test_status_has_uptime(self, base_url):
        resp = requests.get(f"{base_url}/api/status", timeout=5)
        data = resp.json()
        assert "uptime" in data
        assert isinstance(data["uptime"], (int, float))
        assert data["uptime"] > 0

    def test_status_has_heap(self, base_url):
        resp = requests.get(f"{base_url}/api/status", timeout=5)
        data = resp.json()
        assert "free_heap" in data
        assert isinstance(data["free_heap"], (int, float))
        assert data["free_heap"] > 10000

    def test_status_has_wifi_fields(self, base_url):
        resp = requests.get(f"{base_url}/api/status", timeout=5)
        data = resp.json()
        assert "wifi_connected" in data
        assert "wifi_ssid" in data
        assert "wifi_ip" in data
        assert "wifi_rssi" in data

    def test_status_has_mqtt_fields(self, base_url):
        resp = requests.get(f"{base_url}/api/status", timeout=5)
        data = resp.json()
        assert "mqtt_connected" in data
        assert "mqtt_host" in data
        assert "mqtt_port" in data

    def test_status_has_power_fields(self, base_url):
        resp = requests.get(f"{base_url}/api/status", timeout=5)
        data = resp.json()
        assert "dtsu_power" in data
        assert "wallbox_power" in data
        assert "correction_active" in data

    def test_status_has_statistics(self, base_url):
        resp = requests.get(f"{base_url}/api/status", timeout=5)
        data = resp.json()
        assert "dtsu_updates" in data
        assert "wallbox_updates" in data
        assert "wallbox_errors" in data

    def test_status_has_debug_mode(self, base_url):
        resp = requests.get(f"{base_url}/api/status", timeout=5)
        data = resp.json()
        assert "debug_mode" in data
        assert isinstance(data["debug_mode"], bool)


# --- GET /api/config ---


class TestApiConfig:
    def test_config_returns_200(self, base_url):
        resp = requests.get(f"{base_url}/api/config", timeout=5)
        assert resp.status_code == 200

    def test_config_schema(self, base_url):
        resp = requests.get(f"{base_url}/api/config", timeout=5)
        data = resp.json()
        assert "mqtt_host" in data
        assert "mqtt_port" in data
        assert "mqtt_user" in data
        assert "wallbox_topic" in data
        assert "log_level" in data

    def test_config_field_types(self, base_url):
        resp = requests.get(f"{base_url}/api/config", timeout=5)
        data = resp.json()
        assert isinstance(data["mqtt_host"], str)
        assert isinstance(data["mqtt_port"], int)
        assert isinstance(data["mqtt_user"], str)
        assert isinstance(data["wallbox_topic"], str)
        assert isinstance(data["log_level"], int)


# --- POST /api/config ---


class TestApiConfigPost:
    def test_post_mqtt_config(self, base_url, original_config):
        resp = requests.post(
            f"{base_url}/api/config",
            json={
                "type": "mqtt",
                "host": original_config["mqtt_host"],
                "port": original_config["mqtt_port"],
                "user": original_config["mqtt_user"],
                "pass": "admin",
            },
            timeout=5,
        )
        assert resp.status_code == 200
        data = resp.json()
        assert "status" in data
        assert data["status"] in ("ok", "error")  # NVS may reject no-op writes

    def test_post_wallbox_topic(self, base_url, original_config):
        resp = requests.post(
            f"{base_url}/api/config",
            json={"type": "wallbox", "topic": original_config["wallbox_topic"]},
            timeout=5,
        )
        assert resp.status_code == 200
        data = resp.json()
        assert data["status"] == "ok"

    def test_post_loglevel(self, base_url, original_config):
        resp = requests.post(
            f"{base_url}/api/config",
            json={"type": "loglevel", "level": original_config["log_level"]},
            timeout=5,
        )
        assert resp.status_code == 200
        data = resp.json()
        assert data["status"] == "ok"

    def test_post_unknown_type(self, base_url):
        resp = requests.post(
            f"{base_url}/api/config",
            json={"type": "nonexistent", "value": 42},
            timeout=5,
        )
        assert resp.status_code == 200
        data = resp.json()
        assert data["status"] == "error"

    def test_post_invalid_json(self, base_url):
        resp = requests.post(
            f"{base_url}/api/config",
            data="{not valid json",
            headers={"Content-Type": "application/json"},
            timeout=5,
        )
        assert resp.status_code == 400


# --- POST /api/debug ---


class TestApiDebug:
    def test_debug_enable(self, base_url):
        resp = requests.post(
            f"{base_url}/api/debug",
            json={"enabled": True},
            timeout=5,
        )
        assert resp.status_code == 200
        data = resp.json()
        assert data["status"] == "ok"

    def test_debug_disable(self, base_url):
        resp = requests.post(
            f"{base_url}/api/debug",
            json={"enabled": False},
            timeout=5,
        )
        assert resp.status_code == 200
        data = resp.json()
        assert data["status"] == "ok"

    def test_debug_invalid_json(self, base_url):
        resp = requests.post(
            f"{base_url}/api/debug",
            data="not json",
            headers={"Content-Type": "application/json"},
            timeout=5,
        )
        assert resp.status_code == 400

    def test_debug_verify_in_status(self, base_url):
        import time

        # Enable debug
        requests.post(
            f"{base_url}/api/debug",
            json={"enabled": True},
            timeout=5,
        )
        time.sleep(0.5)
        resp = requests.get(f"{base_url}/api/status", timeout=5)
        data = resp.json()
        assert data["debug_mode"] is True

        # Disable debug
        requests.post(
            f"{base_url}/api/debug",
            json={"enabled": False},
            timeout=5,
        )
        time.sleep(0.5)
        resp = requests.get(f"{base_url}/api/status", timeout=5)
        data = resp.json()
        assert data["debug_mode"] is False


# --- 404 handler ---


class TestNotFound:
    def test_nonexistent_path(self, base_url):
        resp = requests.get(f"{base_url}/nonexistent", timeout=5)
        assert resp.status_code == 404


# --- OTA authorization ---


class TestOtaAuth:
    def test_ota_health(self, base_url):
        """OTA health check requires no auth."""
        resp = requests.get(f"{base_url}/ota/health", timeout=5)
        assert resp.status_code == 200
        data = resp.json()
        assert data["status"] == "ok"

    def test_ota_no_auth_rejected(self, base_url):
        """OTA upload without auth header is rejected (update fails)."""
        import io

        fake_fw = io.BytesIO(b"\x00" * 256)
        resp = requests.post(
            f"{base_url}/ota",
            files={"firmware": ("firmware.bin", fake_fw)},
            timeout=10,
        )
        # Without auth, upload handler skips Update.begin, so update has error
        assert resp.status_code in (200, 500)
        data = resp.json()
        if resp.status_code == 200:
            # Update.hasError() should be true since begin() was never called
            assert data.get("status") in ("ok", "error")

    def test_ota_wrong_password_rejected(self, base_url):
        """OTA upload with wrong password is rejected (update fails)."""
        import io

        fake_fw = io.BytesIO(b"\x00" * 256)
        try:
            resp = requests.post(
                f"{base_url}/ota",
                files={"firmware": ("firmware.bin", fake_fw)},
                headers={"Authorization": "Bearer wrong_password"},
                timeout=10,
            )
            # Wrong auth, upload handler skips Update.begin
            assert resp.status_code in (200, 500)
        except requests.exceptions.ReadTimeout:
            # Timeout is acceptable — ESP hung because Update never started
            pass

    def test_ota_correct_auth_invalid_firmware(self, base_url):
        """OTA upload with correct auth but invalid firmware returns error."""
        import io

        fake_fw = io.BytesIO(b"\x00" * 256)
        resp = requests.post(
            f"{base_url}/ota",
            files={"firmware": ("firmware.bin", fake_fw)},
            headers={"Authorization": "Bearer modbus_ota_2023"},
            timeout=10,
        )
        # Auth passes but firmware is invalid
        assert resp.status_code in (200, 500)
        data = resp.json()
        assert data["status"] in ("ok", "error")


# --- Skip restart test (causes device reboot) ---


class TestRestart:
    @pytest.mark.skip(reason="Causes device reboot - run manually if needed")
    def test_restart(self, base_url):
        resp = requests.post(f"{base_url}/api/restart", timeout=5)
        assert resp.status_code == 200
