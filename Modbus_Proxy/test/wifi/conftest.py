"""Fixtures for Modbus Proxy WiFi integration tests.

These fixtures use the WiFi Tester instrument (serial-controlled ESP32-C3 AP)
to automate WiFi connection, reconnection, and captive portal testing.

Requires:
    - WiFi Tester hardware connected via USB serial
    - DUT (Modbus Proxy) powered and reachable on production network
    - Environment variables: WIFI_TESTER_PORT, DUT_IP (optional, have defaults)

Install driver:
    pip install -e <path-to-Wifi-Tester>/pytest
"""

import json
import os
import time
import uuid

import pytest
import requests

# Import will fail until wifi_tester_driver is installed from the Wifi-Tester repo
try:
    from wifi_tester_driver import WiFiTesterDriver
except ImportError:
    WiFiTesterDriver = None


# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

# DUT captive portal settings (must match config.h)
PORTAL_SSID = "MODBUS-Proxy-Setup"
PORTAL_IP = "192.168.4.1"
WIFI_CONNECT_TIMEOUT = 30  # DUT's WIFI_CONNECT_TIMEOUT_MS / 1000
PORTAL_BOOT_THRESHOLD = 3  # reboots needed to trigger portal
PORTAL_TIMEOUT_S = 300  # CAPTIVE_PORTAL_TIMEOUT_MS / 1000

# Test timing
DUT_BOOT_TIME = 15  # seconds from reboot to WiFi connected
FAILED_BOOT_CYCLE = WIFI_CONNECT_TIMEOUT + 5  # one failed boot cycle


# ---------------------------------------------------------------------------
# Session-scoped: WiFi Tester instrument
# ---------------------------------------------------------------------------


@pytest.fixture(scope="session")
def wifi_tester():
    """Session-scoped connection to the WiFi Tester instrument."""
    if WiFiTesterDriver is None:
        pytest.skip(
            "wifi_tester_driver not installed. "
            "Install from Wifi-Tester repo: pip install -e <path>/pytest"
        )

    port = os.environ.get("WIFI_TESTER_PORT", "/dev/ttyACM0")
    driver = WiFiTesterDriver(port)
    driver.open()

    info = driver.ping()
    print(f"WiFi Tester connected: {info}")

    yield driver

    # Cleanup: stop any running AP
    try:
        driver.ap_stop()
    except Exception:
        pass
    driver.close()


@pytest.fixture(scope="session")
def dut_production_ip():
    """DUT IP address on the production network."""
    return os.environ.get("DUT_IP", "192.168.0.177")


@pytest.fixture(scope="session")
def dut_production_url(dut_production_ip):
    """DUT base URL on the production network."""
    return f"http://{dut_production_ip}"


# ---------------------------------------------------------------------------
# Function-scoped: WiFi network lifecycle
# ---------------------------------------------------------------------------


@pytest.fixture
def wifi_network(wifi_tester):
    """Start a fresh test AP, stop on teardown. Yields network info dict."""
    ssid = f"TEST-{uuid.uuid4().hex[:6].upper()}"
    password = "testpass123"
    wifi_tester.ap_start(ssid, password)
    yield {"ssid": ssid, "password": password, "ap_ip": "192.168.4.1"}
    wifi_tester.ap_stop()


@pytest.fixture
def open_wifi_network(wifi_tester):
    """Start an open (no password) test AP, stop on teardown."""
    ssid = f"OPEN-{uuid.uuid4().hex[:6].upper()}"
    wifi_tester.ap_start(ssid, "")
    yield {"ssid": ssid, "password": "", "ap_ip": "192.168.4.1"}
    wifi_tester.ap_stop()


# ---------------------------------------------------------------------------
# DUT provisioning helpers
# ---------------------------------------------------------------------------


def _provision_dut_wifi(base_url, ssid, password, timeout=5):
    """Tell the DUT to switch to new WiFi credentials. DUT will reboot."""
    requests.post(
        f"{base_url}/api/wifi",
        json={"ssid": ssid, "password": password},
        timeout=timeout,
    )


def _get_dut_status(base_url, timeout=5):
    """Get DUT /api/status."""
    resp = requests.get(f"{base_url}/api/status", timeout=timeout)
    return resp.json()


def _wait_for_dut_on_production(dut_production_url, timeout=60):
    """Poll until DUT is reachable on production network."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            resp = requests.get(
                f"{dut_production_url}/api/status", timeout=3
            )
            if resp.status_code == 200:
                return resp.json()
        except requests.exceptions.RequestException:
            pass
        time.sleep(2)
    raise TimeoutError("DUT did not come back on production network")


# ---------------------------------------------------------------------------
# DUT on test AP fixture
# ---------------------------------------------------------------------------


@pytest.fixture
def dut_on_test_ap(wifi_tester, wifi_network, dut_production_url):
    """Provision the DUT onto the test AP and wait for connection.

    Yields a dict with:
        - ip: DUT's IP on the test network
        - ssid: test AP SSID
        - password: test AP password

    On teardown, restores DUT to production network credentials.
    """
    # Record original SSID for restore
    try:
        original_status = _get_dut_status(dut_production_url)
        original_ssid = original_status.get("wifi_ssid", "")
    except Exception:
        original_ssid = ""

    # Tell DUT to connect to test AP (DUT reboots)
    _provision_dut_wifi(
        dut_production_url,
        wifi_network["ssid"],
        wifi_network["password"],
    )

    # Wait for DUT to connect to our AP
    station = wifi_tester.wait_for_station(timeout=DUT_BOOT_TIME + WIFI_CONNECT_TIMEOUT)
    dut_ip = station["ip"]

    yield {
        "ip": dut_ip,
        "ssid": wifi_network["ssid"],
        "password": wifi_network["password"],
    }

    # Restore: tell DUT to go back to production network
    try:
        wifi_tester.http_post(
            f"http://{dut_ip}/api/wifi",
            json={"ssid": original_ssid, "password": ""},
        )
    except Exception:
        pass

    # Wait for DUT to reappear on production network
    try:
        _wait_for_dut_on_production(dut_production_url, timeout=60)
    except TimeoutError:
        # DUT will eventually fall back to credentials.h
        pass


# ---------------------------------------------------------------------------
# DUT HTTP via relay
# ---------------------------------------------------------------------------


class DUTHttpClient:
    """HTTP client that routes requests through the WiFi Tester serial relay."""

    def __init__(self, wifi_tester, dut_ip):
        self._tester = wifi_tester
        self._dut_ip = dut_ip

    @property
    def base_url(self):
        return f"http://{self._dut_ip}"

    def get(self, path, **kwargs):
        return self._tester.http_get(f"{self.base_url}{path}", **kwargs)

    def post(self, path, json=None, **kwargs):
        return self._tester.http_post(f"{self.base_url}{path}", json=json, **kwargs)


@pytest.fixture
def dut_http(wifi_tester, dut_on_test_ap):
    """HTTP client for the DUT, routed through the WiFi Tester relay."""
    return DUTHttpClient(wifi_tester, dut_on_test_ap["ip"])


# ---------------------------------------------------------------------------
# Captive portal fixture
# ---------------------------------------------------------------------------


@pytest.fixture
def dut_in_portal_mode(wifi_tester, wifi_network, dut_production_url):
    """Trigger the DUT's captive portal mode by causing 3 failed WiFi boots.

    Prerequisites: DUT is currently on production network.

    Steps:
        1. Provision DUT with test AP's SSID (but AP is stopped)
        2. DUT reboots and fails WiFi 3 times
        3. DUT enters captive portal mode

    Yields the portal SSID.

    On teardown, waits for portal timeout or restores DUT via portal.
    """
    # Stop AP so DUT's WiFi will fail
    wifi_tester.ap_stop()

    # Provision DUT with our SSID — DUT will reboot and fail
    _provision_dut_wifi(
        dut_production_url,
        wifi_network["ssid"],
        wifi_network["password"],
    )

    # Wait for 3 failed boot cycles
    # Each cycle: ~30s WiFi timeout + ~5s boot overhead
    wait_time = FAILED_BOOT_CYCLE * PORTAL_BOOT_THRESHOLD + 10
    print(f"Waiting {wait_time}s for {PORTAL_BOOT_THRESHOLD} failed boot cycles...")
    time.sleep(wait_time)

    # Verify portal AP is broadcasting
    scan_result = wifi_tester.scan()
    portal_found = any(
        n["ssid"] == PORTAL_SSID for n in scan_result.get("networks", [])
    )
    if not portal_found:
        pytest.fail(
            f"Portal SSID '{PORTAL_SSID}' not found in scan after "
            f"{PORTAL_BOOT_THRESHOLD} failed boots. "
            f"Visible networks: {[n['ssid'] for n in scan_result.get('networks', [])]}"
        )

    yield PORTAL_SSID

    # Teardown: provision DUT back to production via the portal
    try:
        wifi_tester.sta_join(PORTAL_SSID, timeout=10)
        wifi_tester.http_post(
            f"http://{PORTAL_IP}/api/wifi",
            json={"ssid": "", "password": ""},  # empty = use credentials.h fallback
        )
        wifi_tester.sta_leave()
    except Exception:
        pass

    # Wait for DUT to come back on production (portal timeout is 5 min max)
    try:
        _wait_for_dut_on_production(dut_production_url, timeout=PORTAL_TIMEOUT_S + 30)
    except TimeoutError:
        pass
