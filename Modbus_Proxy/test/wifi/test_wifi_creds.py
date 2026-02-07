"""WIFI-3xx: Invalid Credentials Tests.

Verify the DUT handles bad WiFi credentials gracefully.
"""

import time

import pytest


pytestmark = pytest.mark.wifi


class TestInvalidCredentials:
    """WIFI-300 to WIFI-303: Invalid credential handling."""

    def test_wrong_password(self, wifi_tester, dut_production_url):
        """WIFI-300: DUT fails gracefully with wrong password."""
        ssid = "SECURED-NET"
        wifi_tester.ap_start(ssid, "correct_password")

        try:
            # Provision DUT with wrong password
            from conftest import _provision_dut_wifi
            _provision_dut_wifi(dut_production_url, ssid, "wrong_password")

            # DUT should NOT connect
            with pytest.raises(Exception):
                wifi_tester.wait_for_station(timeout=35)
        finally:
            wifi_tester.ap_stop()
            # DUT will eventually fall back to credentials.h and rejoin production

    def test_wrong_ssid(self, wifi_tester, dut_production_url):
        """WIFI-301: DUT fails gracefully with nonexistent SSID."""
        # Provision DUT with SSID that doesn't exist
        from conftest import _provision_dut_wifi
        _provision_dut_wifi(dut_production_url, "NONEXISTENT-NETWORK-XYZ", "password")

        # No AP to connect to — DUT will reboot after timeout
        time.sleep(40)

        # DUT should eventually come back on production network
        # (falls back to credentials.h after failed NVS creds)
        from conftest import _wait_for_dut_on_production
        _wait_for_dut_on_production(dut_production_url, timeout=120)

    def test_empty_password_for_wpa2(self, wifi_tester, dut_production_url):
        """WIFI-302: DUT fails auth when WPA2 AP gets empty password."""
        ssid = "WPA2-NET"
        wifi_tester.ap_start(ssid, "real_password_123")

        try:
            from conftest import _provision_dut_wifi
            _provision_dut_wifi(dut_production_url, ssid, "")

            # DUT should NOT connect (empty password for WPA2)
            with pytest.raises(Exception):
                wifi_tester.wait_for_station(timeout=35)
        finally:
            wifi_tester.ap_stop()

    def test_correct_creds_after_bad(self, wifi_tester, dut_production_url):
        """WIFI-303: DUT connects after correcting bad credentials."""
        ssid = "RECOVERY-NET"
        password = "correct_pass_123"
        wifi_tester.ap_start(ssid, password)

        try:
            # First: provision with wrong password
            from conftest import _provision_dut_wifi
            _provision_dut_wifi(dut_production_url, ssid, "bad_password")

            # DUT fails and reboots
            time.sleep(40)

            # DUT falls back to production network
            from conftest import _wait_for_dut_on_production
            _wait_for_dut_on_production(dut_production_url, timeout=120)

            # Now provision with correct password
            _provision_dut_wifi(dut_production_url, ssid, password)

            # DUT should connect
            station = wifi_tester.wait_for_station(timeout=45)
            assert station["ip"].startswith("192.168.4.")

            # Restore to production
            wifi_tester.http_post(
                f"http://{station['ip']}/api/wifi",
                json={"ssid": "", "password": ""},
            )
        finally:
            wifi_tester.ap_stop()
