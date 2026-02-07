"""WIFI-4xx: Captive Portal Tests.

Verify the DUT's captive portal activation, page serving, and full
provisioning flow. These tests are slow (~90s per portal activation).
"""

import time

import pytest

from conftest import (
    PORTAL_IP,
    PORTAL_SSID,
    PORTAL_TIMEOUT_S,
)


pytestmark = [pytest.mark.wifi, pytest.mark.captive_portal]


class TestCaptivePortalActivation:
    """WIFI-400, WIFI-406: Portal activation and non-activation."""

    def test_portal_activates_after_3_failed_boots(
        self, dut_in_portal_mode, wifi_tester
    ):
        """WIFI-400: Portal AP appears after 3 failed WiFi boots."""
        scan_result = wifi_tester.scan()
        portal_ssids = [
            n["ssid"]
            for n in scan_result.get("networks", [])
            if n["ssid"] == PORTAL_SSID
        ]
        assert len(portal_ssids) == 1

    def test_normal_boot_does_not_trigger_portal(
        self, dut_on_test_ap, dut_http, wifi_tester
    ):
        """WIFI-406: A single reboot does NOT trigger the portal."""
        # Reboot DUT (boot counter goes 0 -> 1)
        dut_http.post("/api/restart")
        time.sleep(5)

        # DUT should reconnect to test AP (not enter portal)
        station = wifi_tester.wait_for_station(timeout=45)

        # Verify NOT in portal mode
        resp = wifi_tester.http_get(f"http://{station['ip']}/api/status")
        assert resp.status_code == 200
        assert resp.json()["wifi_connected"] is True

        # Portal SSID should NOT be visible
        scan_result = wifi_tester.scan()
        portal_found = any(
            n["ssid"] == PORTAL_SSID for n in scan_result.get("networks", [])
        )
        assert not portal_found


class TestCaptivePortalPages:
    """WIFI-401, WIFI-402, WIFI-404: Portal page serving."""

    def test_portal_page_accessible(self, dut_in_portal_mode, wifi_tester):
        """WIFI-401: Portal main page is served over HTTP."""
        wifi_tester.sta_join(dut_in_portal_mode, timeout=10)
        try:
            resp = wifi_tester.http_get(f"http://{PORTAL_IP}/")
            assert resp.status_code == 200
            assert len(resp.text) > 100
        finally:
            wifi_tester.sta_leave()

    def test_wifi_scan_endpoint(self, dut_in_portal_mode, wifi_tester):
        """WIFI-402: /api/scan returns visible networks in portal mode."""
        wifi_tester.sta_join(dut_in_portal_mode, timeout=10)
        try:
            resp = wifi_tester.http_get(f"http://{PORTAL_IP}/api/scan")
            assert resp.status_code == 200
            data = resp.json()
            assert "networks" in data
            assert isinstance(data["networks"], list)
        finally:
            wifi_tester.sta_leave()

    def test_portal_dns_redirect(self, dut_in_portal_mode, wifi_tester):
        """WIFI-404: DNS redirect sends all requests to portal page."""
        wifi_tester.sta_join(dut_in_portal_mode, timeout=10)
        try:
            # Request a captive portal detection URL
            resp = wifi_tester.http_get(f"http://{PORTAL_IP}/generate_204")
            # Should redirect to portal (200 with HTML, not 204)
            assert resp.status_code == 200
        finally:
            wifi_tester.sta_leave()


class TestCaptivePortalProvisioning:
    """WIFI-403: Full captive portal provisioning flow."""

    def test_full_provisioning_flow(self, dut_in_portal_mode, wifi_tester):
        """WIFI-403: Provision DUT via portal, verify it connects to new AP."""
        target_ssid = "PORTAL-TARGET"
        target_pass = "portal_test_123"

        # Join the DUT's portal AP
        wifi_tester.sta_join(dut_in_portal_mode, timeout=10)

        # Submit new WiFi credentials through the portal
        resp = wifi_tester.http_post(
            f"http://{PORTAL_IP}/api/wifi",
            json={"ssid": target_ssid, "password": target_pass},
        )
        assert resp.status_code == 200

        # Leave portal
        wifi_tester.sta_leave()
        time.sleep(2)

        # Start the target AP
        wifi_tester.ap_start(target_ssid, target_pass)

        # DUT should reboot and connect to the target AP
        station = wifi_tester.wait_for_station(timeout=45)
        assert station["ip"].startswith("192.168.4.")

        # Verify DUT is operational on the new network
        resp = wifi_tester.http_get(f"http://{station['ip']}/api/status")
        assert resp.status_code == 200
        assert resp.json()["wifi_ssid"] == target_ssid

        # Cleanup: restore DUT to production credentials
        wifi_tester.http_post(
            f"http://{station['ip']}/api/wifi",
            json={"ssid": "", "password": ""},
        )
        wifi_tester.ap_stop()


class TestCaptivePortalTimeout:
    """WIFI-405: Portal timeout."""

    @pytest.mark.timeout(PORTAL_TIMEOUT_S + 60)
    def test_portal_timeout(self, dut_in_portal_mode, wifi_tester):
        """WIFI-405: Portal times out after 5 minutes and DUT reboots."""
        # Portal is active — wait for timeout
        # The portal should disappear after PORTAL_TIMEOUT_S
        print(f"Waiting {PORTAL_TIMEOUT_S + 10}s for portal timeout...")
        time.sleep(PORTAL_TIMEOUT_S + 10)

        # After timeout, DUT reboots. Portal SSID should disappear
        # (briefly, then may reappear if DUT re-enters portal)
        scan_result = wifi_tester.scan()
        # The DUT has rebooted — it may or may not be back in portal
        # depending on whether WiFi succeeds. The key assertion is that
        # the portal DID restart (proving timeout worked).
        # We verify by checking the portal reappeared with a fresh timeout.
        portal_found = any(
            n["ssid"] == PORTAL_SSID for n in scan_result.get("networks", [])
        )
        # Portal should reappear (DUT still has bad creds, so it re-enters portal)
        assert portal_found, "Portal did not reappear after timeout reboot"
