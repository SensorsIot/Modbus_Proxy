"""WIFI-6xx: Network Services on Test AP.

Verify DUT's HTTP services work correctly when accessed through
the WiFi Tester's serial relay.
"""

import pytest


pytestmark = pytest.mark.wifi


class TestNetworkServices:
    """WIFI-600 to WIFI-603: Service verification via relay."""

    def test_full_rest_api_via_relay(self, dut_http):
        """WIFI-600: Key REST API endpoints work through the serial relay."""
        # GET /api/status
        resp = dut_http.get("/api/status")
        assert resp.status_code == 200
        status = resp.json()
        assert "fw_version" in status
        assert "uptime" in status
        assert "free_heap" in status

        # GET /api/config
        resp = dut_http.get("/api/config")
        assert resp.status_code == 200
        config = resp.json()
        assert "mqtt_host" in config
        assert "mqtt_port" in config

        # GET / (dashboard)
        resp = dut_http.get("/")
        assert resp.status_code == 200

        # GET /status (info page)
        resp = dut_http.get("/status")
        assert resp.status_code == 200

        # GET /setup (config page)
        resp = dut_http.get("/setup")
        assert resp.status_code == 200

        # 404 for unknown path
        resp = dut_http.get("/nonexistent")
        assert resp.status_code == 404

    def test_ota_health_check(self, dut_http):
        """WIFI-601: OTA health endpoint responds via relay."""
        resp = dut_http.get("/ota/health")
        assert resp.status_code == 200
        data = resp.json()
        assert data["status"] == "ok"

    def test_rssi_reported(self, dut_http):
        """WIFI-602: WiFi RSSI is a plausible negative value."""
        resp = dut_http.get("/api/status")
        data = resp.json()
        rssi = data["wifi_rssi"]
        # RSSI should be negative (typical: -30 to -80 for bench proximity)
        assert isinstance(rssi, int)
        assert -100 < rssi < 0, f"RSSI {rssi} is out of expected range"

    def test_wifi_ssid_matches_test_ap(self, dut_on_test_ap, dut_http):
        """WIFI-603: Reported wifi_ssid matches the test AP's SSID."""
        resp = dut_http.get("/api/status")
        data = resp.json()
        assert data["wifi_ssid"] == dut_on_test_ap["ssid"]
