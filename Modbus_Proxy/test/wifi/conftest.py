"""Fixtures for Modbus Proxy WiFi integration tests.

These fixtures use the Universal ESP32 Tester instrument (serial-controlled ESP32-C3 AP)
to automate WiFi connection, reconnection, and captive portal testing.

Requires:
    - Universal ESP32 Tester hardware connected via USB serial
    - DUT (Modbus Proxy) powered and reachable on production network
    - Environment variables: ESP32_TESTER_PORT, DUT_IP (optional, have defaults)

Install driver:
    pip install -e <path-to-Universal-ESP32-Tester>/pytest
"""

import json
import os
import time
import uuid

import pytest
import requests

# Import will fail until wifi_tester_driver is installed from the Universal-ESP32-Tester repo
try:
    from wifi_tester_driver import WiFiTesterDriver as ESP32TesterDriver
except ImportError:
    ESP32TesterDriver = None


# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

# DUT captive portal settings (must match config.h)
PORTAL_SSID = "MODBUS-Proxy-Setup"
PORTAL_IP = "192.168.4.1"
WIFI_CONNECT_TIMEOUT = 30  # DUT's WIFI_CONNECT_TIMEOUT_MS / 1000
PORTAL_TIMEOUT_S = 300  # CAPTIVE_PORTAL_TIMEOUT_MS / 1000

# The DUT enters the portal only when GPIO 2 reads LOW at boot (config.h
# PORTAL_BUTTON_PIN). There is no boot-counter fallback: the proxy stays on one
# SSID, and only a deliberate action puts it into setup mode. The Pi drives DUT
# GPIO 2 from its own GPIO 18 -- which is also the slot's download-mode strap,
# so it must be released before anything tries to flash.
PORTAL_BUTTON_GPIO = 18
SERIAL_SLOT = int(os.environ.get("DUT_SERIAL_SLOT", "1"))
PORTAL_BANNER = "CAPTIVE PORTAL MODE TRIGGERED"

# Test timing
DUT_BOOT_TIME = 15  # seconds from reboot to WiFi connected


# ---------------------------------------------------------------------------
# Session-scoped: Universal ESP32 Tester instrument
# ---------------------------------------------------------------------------


@pytest.fixture(scope="session")
def esp32_tester():
    """Session-scoped connection to the Universal ESP32 Tester instrument."""
    if ESP32TesterDriver is None:
        pytest.skip(
            "wifi_tester_driver not installed. "
            "Install from Universal-ESP32-Tester repo: pip install -e <path>/pytest"
        )

    port = os.environ.get("ESP32_TESTER_PORT", "/dev/ttyACM0")
    driver = ESP32TesterDriver(port)
    driver.open()

    info = driver.ping()
    print(f"ESP32 Tester connected: {info}")

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
def wifi_network(esp32_tester):
    """Start a fresh test AP, stop on teardown. Yields network info dict."""
    ssid = f"TEST-{uuid.uuid4().hex[:6].upper()}"
    password = "testpass123"
    esp32_tester.ap_start(ssid, password)
    yield {"ssid": ssid, "password": password, "ap_ip": "192.168.4.1"}
    esp32_tester.ap_stop()


@pytest.fixture
def open_wifi_network(esp32_tester):
    """Start an open (no password) test AP, stop on teardown."""
    ssid = f"OPEN-{uuid.uuid4().hex[:6].upper()}"
    esp32_tester.ap_start(ssid, "")
    yield {"ssid": ssid, "password": "", "ap_ip": "192.168.4.1"}
    esp32_tester.ap_stop()


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
def dut_on_test_ap(esp32_tester, wifi_network, dut_production_url):
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
    station = esp32_tester.wait_for_station(timeout=DUT_BOOT_TIME + WIFI_CONNECT_TIMEOUT)
    dut_ip = station["ip"]

    yield {
        "ip": dut_ip,
        "ssid": wifi_network["ssid"],
        "password": wifi_network["password"],
    }

    # Restore: tell DUT to go back to production network
    try:
        esp32_tester.http_post(
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
    """HTTP client that routes requests through the ESP32 Tester serial relay."""

    def __init__(self, tester, dut_ip):
        self._tester = tester
        self._dut_ip = dut_ip

    @property
    def base_url(self):
        return f"http://{self._dut_ip}"

    def get(self, path, **kwargs):
        return self._tester.http_get(f"{self.base_url}{path}", **kwargs)

    def post(self, path, json=None, **kwargs):
        return self._tester.http_post(f"{self.base_url}{path}", json=json, **kwargs)


@pytest.fixture
def dut_http(esp32_tester, dut_on_test_ap):
    """HTTP client for the DUT, routed through the ESP32 Tester relay."""
    return DUTHttpClient(esp32_tester, dut_on_test_ap["ip"])


# ---------------------------------------------------------------------------
# Captive portal fixture
# ---------------------------------------------------------------------------


@pytest.fixture
def dut_in_portal_mode(esp32_tester, dut_production_url):
    """Put the DUT into captive portal mode by holding its portal button.

    The firmware enters the portal only when GPIO 2 reads LOW during boot, so
    the tester pulls GPIO 17 low, resets the DUT over serial, waits for the
    portal banner, then releases the pin. Failing to reach an AP never opens
    the portal -- the DUT retries and reboots instead.

    Yields the portal SSID.

    On teardown, clears the DUT's stored credentials so it returns to the
    network compiled into credentials.h.
    """
    for call in ("gpio_set", "serial_reset", "serial_monitor"):
        if not hasattr(esp32_tester, call):
            pytest.skip(
                f"Tester driver has no {call}(); the portal can only be "
                "triggered through the DUT's GPIO 2 button."
            )

    esp32_tester.gpio_set(PORTAL_BUTTON_GPIO, 0)
    try:
        esp32_tester.serial_reset(SERIAL_SLOT)
        esp32_tester.serial_monitor(SERIAL_SLOT, pattern=PORTAL_BANNER, timeout=30)
    finally:
        esp32_tester.gpio_set(PORTAL_BUTTON_GPIO, "z")

    # Confirm the portal AP is broadcasting before handing over to the test.
    deadline = time.time() + 20
    while time.time() < deadline:
        networks = esp32_tester.scan().get("networks", [])
        if any(n["ssid"] == PORTAL_SSID for n in networks):
            break
        time.sleep(2)
    else:
        visible = [n["ssid"] for n in esp32_tester.scan().get("networks", [])]
        pytest.fail(
            f"Portal SSID '{PORTAL_SSID}' not broadcasting after a "
            f"GPIO-triggered boot. Visible networks: {visible}"
        )

    yield PORTAL_SSID

    # Teardown: an empty ssid clears NVS and reboots onto credentials.h.
    try:
        esp32_tester.sta_join(PORTAL_SSID, timeout=10)
        esp32_tester.http_post(
            f"http://{PORTAL_IP}/api/wifi",
            json={"ssid": "", "password": ""},
        )
        esp32_tester.sta_leave()
    except Exception:
        pass

    try:
        _wait_for_dut_on_production(dut_production_url, timeout=PORTAL_TIMEOUT_S + 30)
    except TimeoutError:
        pass
