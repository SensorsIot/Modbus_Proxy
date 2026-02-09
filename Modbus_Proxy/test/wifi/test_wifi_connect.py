"""WIFI-1xx: WiFi Connection Tests.

Verify the DUT connects to a test AP, gets DHCP, and basic services work.
"""

import time

import pytest


pytestmark = pytest.mark.wifi


class TestWiFiConnection:
    """WIFI-100 to WIFI-107: Basic WiFi connection behavior."""

    def test_connect_to_test_ap(self, dut_on_test_ap, dut_http):
        """WIFI-100: DUT connects to test AP and reports connected."""
        resp = dut_http.get("/api/status")
        assert resp.status_code == 200
        data = resp.json()
        assert data["wifi_connected"] is True
        assert data["wifi_ssid"] == dut_on_test_ap["ssid"]

    def test_dhcp_address_assigned(self, dut_on_test_ap, dut_http):
        """WIFI-101: DUT gets a DHCP address in the 192.168.4.x range."""
        assert dut_on_test_ap["ip"].startswith("192.168.4.")

        resp = dut_http.get("/api/status")
        data = resp.json()
        assert data["wifi_ip"] == dut_on_test_ap["ip"]

    def test_mdns_resolves(self, esp32_tester, dut_on_test_ap):
        """WIFI-102: mDNS hostname resolves on the test network."""
        resp = esp32_tester.http_get("http://modbus-proxy.local/api/status")
        assert resp.status_code == 200
        data = resp.json()
        assert "fw_version" in data

    def test_web_dashboard_accessible(self, dut_http):
        """WIFI-103: Dashboard page loads via relay."""
        resp = dut_http.get("/")
        assert resp.status_code == 200
        assert len(resp.text) > 100  # non-trivial HTML content

    def test_rest_api_accessible(self, dut_http):
        """WIFI-104: /api/status returns valid JSON with expected fields."""
        resp = dut_http.get("/api/status")
        assert resp.status_code == 200
        data = resp.json()
        assert "fw_version" in data
        assert "uptime" in data
        assert "free_heap" in data
        assert "wifi_connected" in data
        assert "mqtt_connected" in data

    def test_connect_wpa2(self, esp32_tester, dut_production_url):
        """WIFI-105: DUT connects to WPA2-secured AP."""
        ssid = "WPA2-TEST"
        password = "secure_password_123"
        esp32_tester.ap_start(ssid, password)

        try:
            from conftest import _provision_dut_wifi
            _provision_dut_wifi(dut_production_url, ssid, password)
            station = esp32_tester.wait_for_station(timeout=45)
            assert station["ip"].startswith("192.168.4.")
        finally:
            # Restore DUT to production network
            try:
                esp32_tester.http_post(
                    f"http://{station['ip']}/api/wifi",
                    json={"ssid": "", "password": ""},
                )
            except Exception:
                pass
            esp32_tester.ap_stop()

    def test_connect_open_network(
        self, esp32_tester, open_wifi_network, dut_production_url
    ):
        """WIFI-106: DUT connects to an open (no password) network."""
        from conftest import _provision_dut_wifi, _wait_for_dut_on_production

        _provision_dut_wifi(
            dut_production_url,
            open_wifi_network["ssid"],
            open_wifi_network["password"],
        )

        station = esp32_tester.wait_for_station(timeout=45)
        assert station["ip"].startswith("192.168.4.")

        # Restore
        try:
            esp32_tester.http_post(
                f"http://{station['ip']}/api/wifi",
                json={"ssid": "", "password": ""},
            )
        except Exception:
            pass

    def test_boot_counter_resets_on_success(self, dut_on_test_ap, dut_http, esp32_tester):
        """WIFI-107: Boot counter resets after successful WiFi connection.

        After a successful connection, rebooting once should NOT trigger
        the captive portal (counter goes 0 -> 1 -> 0, not accumulating).
        """
        # Restart DUT
        dut_http.post("/api/restart")
        time.sleep(5)

        # Wait for DUT to reconnect to our AP
        station = esp32_tester.wait_for_station(timeout=45)

        # Verify DUT is in normal mode (not portal)
        resp = esp32_tester.http_get(f"http://{station['ip']}/api/status")
        assert resp.status_code == 200
        data = resp.json()
        assert data["wifi_connected"] is True
