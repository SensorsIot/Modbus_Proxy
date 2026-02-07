"""WIFI-5xx: WiFi Credential Management Tests.

Verify NVS persistence, credential priority, and edge cases.
"""

import time

import pytest


pytestmark = pytest.mark.wifi


class TestNVSPersistence:
    """WIFI-500 to WIFI-503: Credential storage and persistence."""

    def test_nvs_credentials_persist_across_reboot(
        self, dut_on_test_ap, dut_http, wifi_tester
    ):
        """WIFI-500: DUT reconnects to same AP after reboot (NVS creds survive)."""
        # Reboot DUT
        dut_http.post("/api/restart")
        time.sleep(5)

        # DUT should reconnect with saved NVS credentials
        station = wifi_tester.wait_for_station(timeout=45)
        resp = wifi_tester.http_get(f"http://{station['ip']}/api/status")
        assert resp.json()["wifi_ssid"] == dut_on_test_ap["ssid"]

    def test_nvs_takes_priority_over_fallback(self, dut_on_test_ap, dut_http):
        """WIFI-501: NVS credentials are tried before credentials.h fallback.

        If DUT is connected to test AP (NVS creds), and credentials.h points
        to production network, the DUT chose NVS — proving priority.
        """
        resp = dut_http.get("/api/status")
        data = resp.json()
        # DUT is on test AP, not production — NVS took priority
        assert data["wifi_ssid"] == dut_on_test_ap["ssid"]

    def test_post_api_wifi_saves_and_reboots(
        self, dut_on_test_ap, wifi_tester
    ):
        """WIFI-502: POST /api/wifi saves credentials and triggers reboot."""
        dut_ip = dut_on_test_ap["ip"]
        new_ssid = dut_on_test_ap["ssid"]  # same AP, just verify the reboot

        # POST new credentials (same AP to keep test simple)
        resp = wifi_tester.http_post(
            f"http://{dut_ip}/api/wifi",
            json={"ssid": new_ssid, "password": dut_on_test_ap["password"]},
        )
        assert resp.status_code == 200

        # DUT reboots — wait for reconnect
        time.sleep(5)
        station = wifi_tester.wait_for_station(timeout=45)
        assert station["ip"].startswith("192.168.4.")

    def test_factory_reset_clears_wifi(
        self, dut_on_test_ap, dut_http, wifi_tester, dut_production_url
    ):
        """WIFI-503: Factory reset clears NVS WiFi and DUT falls back to credentials.h."""
        # Factory reset via API
        dut_http.post("/api/config", json={"type": "reset"})

        # DUT reboots with cleared NVS — should fall back to credentials.h
        # which points to the production network, not our test AP
        time.sleep(5)

        # DUT should NOT reconnect to test AP (NVS was cleared)
        with pytest.raises(Exception):
            wifi_tester.wait_for_station(timeout=20)

        # Instead, DUT should appear on production network
        from conftest import _wait_for_dut_on_production
        _wait_for_dut_on_production(dut_production_url, timeout=60)


class TestCredentialEdgeCases:
    """WIFI-504 to WIFI-505: Edge cases in SSID and password."""

    def test_long_ssid_32_chars(self, wifi_tester, dut_production_url):
        """WIFI-504: DUT connects to AP with max-length (32 char) SSID."""
        ssid = "A" * 32  # Max SSID length
        password = "testpass123"
        wifi_tester.ap_start(ssid, password)

        try:
            from conftest import _provision_dut_wifi
            _provision_dut_wifi(dut_production_url, ssid, password)

            station = wifi_tester.wait_for_station(timeout=45)
            assert station["ip"].startswith("192.168.4.")

            # Verify SSID reported correctly
            resp = wifi_tester.http_get(f"http://{station['ip']}/api/status")
            assert resp.json()["wifi_ssid"] == ssid

            # Restore
            wifi_tester.http_post(
                f"http://{station['ip']}/api/wifi",
                json={"ssid": "", "password": ""},
            )
        finally:
            wifi_tester.ap_stop()

    def test_special_chars_in_password(self, wifi_tester, dut_production_url):
        """WIFI-505: DUT connects with special characters in password."""
        ssid = "SPECIAL-TEST"
        password = "T3st!@#$%^&*()"
        wifi_tester.ap_start(ssid, password)

        try:
            from conftest import _provision_dut_wifi
            _provision_dut_wifi(dut_production_url, ssid, password)

            station = wifi_tester.wait_for_station(timeout=45)
            assert station["ip"].startswith("192.168.4.")

            # Restore
            wifi_tester.http_post(
                f"http://{station['ip']}/api/wifi",
                json={"ssid": "", "password": ""},
            )
        finally:
            wifi_tester.ap_stop()
