"""WIFI-2xx: AP Dropout and Reconnection Tests.

Verify the DUT handles AP dropouts gracefully and reconnects.
"""

import time

import pytest


pytestmark = pytest.mark.wifi


class TestAPDropout:
    """WIFI-200 to WIFI-205: AP dropout and reconnection behavior."""

    def test_reconnect_after_ap_drops(self, dut_on_test_ap, wifi_tester):
        """WIFI-200: DUT reconnects after AP drops for 5 seconds."""
        ssid = dut_on_test_ap["ssid"]
        password = dut_on_test_ap["password"]

        # Drop AP
        wifi_tester.ap_stop()
        time.sleep(5)

        # Restart AP with same credentials
        wifi_tester.ap_start(ssid, password)

        # DUT should reconnect
        station = wifi_tester.wait_for_station(timeout=30)
        assert station["ip"].startswith("192.168.4.")

        # Verify DUT is operational
        resp = wifi_tester.http_get(f"http://{station['ip']}/api/status")
        assert resp.status_code == 200
        assert resp.json()["wifi_connected"] is True

    def test_brief_ap_dropout(self, dut_on_test_ap, wifi_tester):
        """WIFI-201: DUT recovers from a brief (2s) AP dropout without rebooting."""
        ssid = dut_on_test_ap["ssid"]
        password = dut_on_test_ap["password"]

        # Record uptime before dropout
        resp = wifi_tester.http_get(f"http://{dut_on_test_ap['ip']}/api/status")
        uptime_before = resp.json()["uptime"]

        # Brief dropout
        wifi_tester.ap_stop()
        time.sleep(2)
        wifi_tester.ap_start(ssid, password)

        # Wait for reconnect
        station = wifi_tester.wait_for_station(timeout=30)

        # Check uptime increased (no reboot)
        resp = wifi_tester.http_get(f"http://{station['ip']}/api/status")
        uptime_after = resp.json()["uptime"]
        assert uptime_after > uptime_before, "DUT appears to have rebooted"

    def test_extended_ap_dropout(self, dut_on_test_ap, wifi_tester):
        """WIFI-202: DUT eventually reconnects after extended (90s) AP dropout."""
        ssid = dut_on_test_ap["ssid"]
        password = dut_on_test_ap["password"]

        # Extended dropout (DUT may reboot during this)
        wifi_tester.ap_stop()
        time.sleep(90)

        # Restart AP
        wifi_tester.ap_start(ssid, password)

        # DUT should eventually reconnect (may need a boot cycle)
        station = wifi_tester.wait_for_station(timeout=60)
        assert station["ip"].startswith("192.168.4.")

    def test_ap_ssid_change_disconnects(self, dut_on_test_ap, wifi_tester):
        """WIFI-203: DUT cannot connect when AP SSID changes."""
        # Stop AP and restart with different SSID
        wifi_tester.ap_stop()
        time.sleep(1)
        wifi_tester.ap_start("DIFFERENT-SSID", dut_on_test_ap["password"])

        # DUT should NOT connect (wrong SSID)
        with pytest.raises(Exception):  # TimeoutError from wait_for_station
            wifi_tester.wait_for_station(timeout=15)

        # Cleanup: restore original AP so teardown can restore DUT
        wifi_tester.ap_stop()
        wifi_tester.ap_start(dut_on_test_ap["ssid"], dut_on_test_ap["password"])
        wifi_tester.wait_for_station(timeout=60)

    def test_ap_password_change_disconnects(self, dut_on_test_ap, wifi_tester):
        """WIFI-204: DUT cannot connect when AP password changes."""
        # Stop AP and restart with different password
        wifi_tester.ap_stop()
        time.sleep(1)
        wifi_tester.ap_start(dut_on_test_ap["ssid"], "wrong_password_999")

        # DUT should NOT connect (wrong password)
        with pytest.raises(Exception):
            wifi_tester.wait_for_station(timeout=15)

        # Cleanup: restore original AP
        wifi_tester.ap_stop()
        wifi_tester.ap_start(dut_on_test_ap["ssid"], dut_on_test_ap["password"])
        wifi_tester.wait_for_station(timeout=60)

    def test_multiple_dropout_cycles(self, dut_on_test_ap, wifi_tester):
        """WIFI-205: DUT reconnects through 5 dropout cycles, heap stays stable."""
        ssid = dut_on_test_ap["ssid"]
        password = dut_on_test_ap["password"]
        dut_ip = dut_on_test_ap["ip"]

        # Record initial heap
        resp = wifi_tester.http_get(f"http://{dut_ip}/api/status")
        initial_heap = resp.json()["free_heap"]

        for cycle in range(5):
            wifi_tester.ap_stop()
            time.sleep(10)
            wifi_tester.ap_start(ssid, password)
            station = wifi_tester.wait_for_station(timeout=30)
            dut_ip = station["ip"]

        # Verify heap is stable (within 10% of initial)
        resp = wifi_tester.http_get(f"http://{dut_ip}/api/status")
        final_heap = resp.json()["free_heap"]
        heap_drop_pct = (initial_heap - final_heap) / initial_heap * 100
        assert heap_drop_pct < 10, (
            f"Heap dropped {heap_drop_pct:.1f}% after 5 dropout cycles "
            f"({initial_heap} -> {final_heap})"
        )
