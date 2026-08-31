"""WIFI-4xx: Captive Portal Tests.

Verify the DUT's captive portal activation, page serving, and full
provisioning flow. Each portal activation costs one GPIO-triggered reboot.
"""

import time

import pytest
import requests

from conftest import (
    PORTAL_IP,
    PORTAL_PASS,
    PORTAL_SSID,
    PORTAL_TIMEOUT_S,
)


pytestmark = [pytest.mark.wifi, pytest.mark.captive_portal]


class TestCaptivePortalActivation:
    """WIFI-406: Portal is not entered without the button."""

    def test_normal_boot_does_not_trigger_portal(
        self, dut_on_test_ap, dut_http, esp32_tester
    ):
        """WIFI-406: A reboot without the portal button does NOT open it."""
        # Reboot with GPIO 2 left floating (pulled up)
        dut_http.post("/api/restart")
        time.sleep(5)

        # DUT should reconnect to test AP (not enter portal)
        station = esp32_tester.wait_for_station(timeout=45)

        # Verify NOT in portal mode
        resp = esp32_tester.http_get(f"http://{station['ip']}/api/status")
        assert resp.status_code == 200
        assert resp.json()["wifi_connected"] is True

        # No scan here: the bench has a single radio and answers 503 while its
        # own AP is running. Being associated to the test AP and reporting
        # wifi_connected already proves the DUT is not serving its portal --
        # portal mode never joins a network.


class TestCaptivePortalPages:
    """WIFI-401, WIFI-402, WIFI-404: Portal page serving."""

    def test_portal_page_accessible(self, dut_in_portal_mode, esp32_tester):
        """WIFI-401: Portal main page is served over HTTP."""
        esp32_tester.sta_join(dut_in_portal_mode, PORTAL_PASS, timeout=10)
        try:
            resp = esp32_tester.http_get(f"http://{PORTAL_IP}/")
            assert resp.status_code == 200
            assert len(resp.text) > 100
        finally:
            esp32_tester.sta_leave()

    def test_wifi_scan_endpoint(self, dut_in_portal_mode, esp32_tester):
        """WIFI-402: /api/scan returns visible networks in portal mode."""
        esp32_tester.sta_join(dut_in_portal_mode, PORTAL_PASS, timeout=10)
        try:
            resp = esp32_tester.http_get(f"http://{PORTAL_IP}/api/scan")
            assert resp.status_code == 200
            data = resp.json()
            assert "networks" in data
            assert isinstance(data["networks"], list)
        finally:
            esp32_tester.sta_leave()

    def test_portal_dns_redirect(self, dut_in_portal_mode, esp32_tester):
        """WIFI-404: DNS redirect sends all requests to portal page."""
        esp32_tester.sta_join(dut_in_portal_mode, PORTAL_PASS, timeout=10)
        try:
            # Request a captive portal detection URL
            resp = esp32_tester.http_get(f"http://{PORTAL_IP}/generate_204")
            # Should redirect to portal (200 with HTML, not 204)
            assert resp.status_code == 200
        finally:
            esp32_tester.sta_leave()


class TestCaptivePortalProvisioning:
    """WIFI-403: Full captive portal provisioning flow."""

    def test_full_provisioning_flow(self, dut_in_portal_mode, esp32_tester):
        """WIFI-403: Provision DUT via portal, verify it connects to new AP."""
        target_ssid = "PORTAL-TARGET"
        target_pass = "portal_test_123"

        # Join the DUT's portal AP
        esp32_tester.sta_join(dut_in_portal_mode, PORTAL_PASS, timeout=10)

        # Submit new WiFi credentials through the portal
        resp = esp32_tester.http_post(
            f"http://{PORTAL_IP}/api/wifi",
            json={"ssid": target_ssid, "password": target_pass},
        )
        assert resp.status_code == 200

        # Leave portal
        esp32_tester.sta_leave()
        time.sleep(2)

        # Start the target AP
        esp32_tester.ap_start(target_ssid, target_pass)

        # DUT should reboot and connect to the target AP
        station = esp32_tester.wait_for_station(timeout=45)
        assert station["ip"].startswith("192.168.4.")

        # Verify DUT is operational on the new network
        resp = esp32_tester.http_get(f"http://{station['ip']}/api/status")
        assert resp.status_code == 200
        assert resp.json()["wifi_ssid"] == target_ssid

        # Cleanup: restore DUT to production credentials
        esp32_tester.http_post(
            f"http://{station['ip']}/api/wifi",
            json={"ssid": "", "password": ""},
        )
        esp32_tester.ap_stop()


class TestCaptivePortalTimeout:
    """WIFI-405: Portal timeout."""

    # Covers the portal timeout, the poll past it, and the fixture's own
    # GPIO-trigger + reset + scan-confirm before the test body starts.
    @pytest.mark.timeout(PORTAL_TIMEOUT_S + 300)
    def test_portal_timeout(self, dut_in_portal_mode, dut_production_url):
        """WIFI-405 / CP-102: portal times out and the DUT reboots onto WiFi.

        Asserted on the consequence, not on the serial announcement: this
        console drops lines, and a timeout message that was printed and lost
        cannot be told from one never printed. The DUT answering on its own
        network proves both halves -- the portal exited and the device
        restarted. It does not re-enter the portal, because only the GPIO 2
        button opens it.

        Set PORTAL_TIMEOUT_S to match the firmware under test; the
        esp32-c3-benchtest build shortens it to 20s.
        """
        deadline = time.time() + PORTAL_TIMEOUT_S + 90
        while time.time() < deadline:
            try:
                if requests.get(f"{dut_production_url}/api/status",
                                timeout=3).status_code == 200:
                    return
            except requests.exceptions.RequestException:
                pass
            time.sleep(2)
        pytest.fail(
            f"DUT never left the portal: not reachable on its own network "
            f"within {PORTAL_TIMEOUT_S + 90}s of the portal opening"
        )
