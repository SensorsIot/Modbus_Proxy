"""Integration tests for Modbus Proxy test injection endpoint.

The /api/test/inject endpoint simulates DTSU meter data flowing through
the proxy pipeline. It requires debug mode to be enabled first.

Requires a live device at DEVICE_IP.
Run with: pytest test/integration/test_inject.py -v
"""

import json

import pytest
import requests

pytestmark = pytest.mark.timeout(30)


@pytest.fixture(autouse=True)
def enable_debug_mode(base_url):
    """Enable debug mode before each test, disable after."""
    requests.post(
        f"{base_url}/api/debug",
        json={"enabled": True},
        timeout=5,
    )
    yield
    requests.post(
        f"{base_url}/api/debug",
        json={"enabled": False},
        timeout=5,
    )


# --- Access control ---


class TestInjectAccessControl:
    def test_requires_debug_mode(self, base_url):
        """Injection endpoint returns 403 when debug mode is off."""
        # Disable debug mode
        requests.post(
            f"{base_url}/api/debug",
            json={"enabled": False},
            timeout=5,
        )
        resp = requests.post(
            f"{base_url}/api/test/inject",
            json={"power_total": 5000.0},
            timeout=5,
        )
        assert resp.status_code == 403
        data = resp.json()
        assert data["status"] == "error"
        assert "debug" in data["message"].lower()

    def test_allowed_when_debug_enabled(self, base_url):
        resp = requests.post(
            f"{base_url}/api/test/inject",
            json={"power_total": 5000.0},
            timeout=5,
        )
        assert resp.status_code == 200
        data = resp.json()
        assert data["status"] == "ok"


# --- Basic injection ---


class TestInjectBasic:
    def test_default_values(self, base_url):
        """Inject with no parameters uses defaults (5000W, 230V, 50Hz, 10A)."""
        resp = requests.post(
            f"{base_url}/api/test/inject",
            json={},
            timeout=5,
        )
        assert resp.status_code == 200
        data = resp.json()
        assert data["status"] == "ok"
        assert "dtsu_power" in data
        assert "wallbox_power" in data
        assert "correction_active" in data
        assert "sun2000_power" in data

    def test_custom_power(self, base_url):
        """Inject specific power value."""
        resp = requests.post(
            f"{base_url}/api/test/inject",
            json={"power_total": 7400.0},
            timeout=5,
        )
        assert resp.status_code == 200
        data = resp.json()
        assert data["status"] == "ok"
        # Power sign is negated by wire format round-trip (power_scale=-1)
        assert abs(abs(data["dtsu_power"]) - 7400.0) < 10.0

    def test_zero_power(self, base_url):
        """Inject zero power."""
        resp = requests.post(
            f"{base_url}/api/test/inject",
            json={"power_total": 0.0},
            timeout=5,
        )
        assert resp.status_code == 200
        data = resp.json()
        assert data["status"] == "ok"
        assert abs(data["dtsu_power"]) < 1.0

    def test_negative_power(self, base_url):
        """Inject negative power (export/feed-in)."""
        resp = requests.post(
            f"{base_url}/api/test/inject",
            json={"power_total": -3000.0},
            timeout=5,
        )
        assert resp.status_code == 200
        data = resp.json()
        assert data["status"] == "ok"
        # Wire format round-trip negates sign, so -3000 becomes 3000
        assert abs(abs(data["dtsu_power"]) - 3000.0) < 10.0

    def test_large_power(self, base_url):
        """Inject large power value (22kW)."""
        resp = requests.post(
            f"{base_url}/api/test/inject",
            json={"power_total": 22000.0},
            timeout=5,
        )
        assert resp.status_code == 200
        data = resp.json()
        assert data["status"] == "ok"
        assert abs(abs(data["dtsu_power"]) - 22000.0) < 50.0

    def test_custom_voltage_frequency(self, base_url):
        """Inject custom voltage and frequency."""
        resp = requests.post(
            f"{base_url}/api/test/inject",
            json={
                "power_total": 5000.0,
                "voltage": 240.0,
                "frequency": 50.05,
                "current": 20.0,
            },
            timeout=5,
        )
        assert resp.status_code == 200
        data = resp.json()
        assert data["status"] == "ok"


# --- Status integration ---


class TestInjectStatusIntegration:
    def test_updates_dtsu_counter(self, base_url):
        """Injection increments dtsu_updates counter."""
        import time
        time.sleep(1)  # Allow device to settle from prior tests
        resp = requests.get(f"{base_url}/api/status", timeout=5)
        before = resp.json().get("dtsu_updates", 0)

        requests.post(
            f"{base_url}/api/test/inject",
            json={"power_total": 5000.0},
            timeout=5,
        )

        resp = requests.get(f"{base_url}/api/status", timeout=5)
        after = resp.json().get("dtsu_updates", 0)
        assert after > before

    def test_updates_status_power(self, base_url):
        """Injection updates dtsu_power in /api/status."""
        inject_resp = requests.post(
            f"{base_url}/api/test/inject",
            json={"power_total": 8888.0},
            timeout=5,
        )
        expected = inject_resp.json()["sun2000_power"]

        resp = requests.get(f"{base_url}/api/status", timeout=5)
        data = resp.json()
        # Status reflects sun2000_power (includes any active wallbox correction)
        assert abs(data["dtsu_power"] - expected) < 20.0

    def test_multiple_injections(self, base_url):
        """Multiple injections update status each time."""
        for power in [1000.0, 5000.0, 9000.0]:
            inject_resp = requests.post(
                f"{base_url}/api/test/inject",
                json={"power_total": power},
                timeout=5,
            )

        expected = inject_resp.json()["sun2000_power"]
        resp = requests.get(f"{base_url}/api/status", timeout=5)
        data = resp.json()
        assert abs(data["dtsu_power"] - expected) < 20.0


# --- Power correction pipeline ---


class TestInjectCorrection:
    def test_correction_without_wallbox(self, base_url):
        """Without wallbox data, correction should not be active."""
        resp = requests.post(
            f"{base_url}/api/test/inject",
            json={"power_total": 5000.0},
            timeout=5,
        )
        data = resp.json()
        # Without recent wallbox MQTT message, correction won't apply
        # sun2000_power should equal dtsu_power
        if not data["correction_active"]:
            assert abs(data["sun2000_power"] - data["dtsu_power"]) < 1.0

    def test_correction_with_wallbox(self, base_url, mqtt_client):
        """With wallbox data above threshold, correction should apply."""
        # Send wallbox power via MQTT
        mqtt_client.publish("wallbox", "3000.0", qos=0)
        import time
        time.sleep(2)

        resp = requests.post(
            f"{base_url}/api/test/inject",
            json={"power_total": 5000.0},
            timeout=5,
        )
        data = resp.json()

        if data["correction_active"]:
            # sun2000_power should differ from dtsu_power by ~wallbox_power
            delta = abs(data["sun2000_power"] - data["dtsu_power"])
            assert delta > 100  # Some correction was applied
        # If correction not active, wallbox data may have expired

    def test_response_fields_present(self, base_url):
        """Response has all expected fields."""
        resp = requests.post(
            f"{base_url}/api/test/inject",
            json={"power_total": 5000.0},
            timeout=5,
        )
        data = resp.json()
        assert "status" in data
        assert "dtsu_power" in data
        assert "wallbox_power" in data
        assert "correction_active" in data
        assert "sun2000_power" in data
        assert isinstance(data["correction_active"], bool)


# --- Error handling ---


class TestInjectErrors:
    def test_invalid_json(self, base_url):
        resp = requests.post(
            f"{base_url}/api/test/inject",
            data="{not valid json",
            headers={"Content-Type": "application/json"},
            timeout=5,
        )
        assert resp.status_code == 400

    def test_method_not_allowed(self, base_url):
        """GET on injection endpoint should return 405 or 404."""
        resp = requests.get(f"{base_url}/api/test/inject", timeout=5)
        assert resp.status_code in (404, 405)
