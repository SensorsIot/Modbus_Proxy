"""Drive the Universal Embedded Workbench's HTTP API with the tester-driver API.

The suite was written against `wifi_tester_driver.WiFiTesterDriver`, which talks
to a tester ESP32 over a local serial port. A workbench bench (`testbench-*`)
exposes the same instruments over HTTP instead, so this adapter presents the
methods the fixtures call and forwards them to `$WORKBENCH_URL`.

Point it at a bench with:

    WORKBENCH_URL=http://<bench>:8080 DUT_SERIAL_SLOT=SLOT3 DUT_IP=<dut> pytest
"""

import base64
import json
import time
import urllib.error
import urllib.request


class Response:
    """Minimal stand-in for a requests.Response."""

    def __init__(self, status_code, text):
        self.status_code = status_code
        self.text = text

    def json(self):
        return json.loads(self.text)


class TimeoutError_(Exception):
    pass


class WorkbenchDriver:
    def __init__(self, url, slot="SLOT1"):
        self.url = url.rstrip("/")
        self.slot = slot if str(slot).startswith("SLOT") else f"SLOT{slot}"
        self._last_station = None

    # --- transport ---

    def _call(self, path, data=None, timeout=60):
        body = json.dumps(data).encode() if data is not None else None
        req = urllib.request.Request(
            self.url + path, data=body,
            headers={"Content-Type": "application/json"} if body else {},
        )
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read().decode())

    def _slot(self, slot=None):
        if slot is None:
            return self.slot
        return slot if str(slot).startswith("SLOT") else f"SLOT{slot}"

    # --- lifecycle ---

    def open(self):
        return self._call("/api/info")

    def close(self):
        pass

    def ping(self):
        return self._call("/api/info")

    # --- WiFi AP ---

    def ap_start(self, ssid, password=None, channel=6):
        d = {"ssid": ssid, "channel": channel}
        if password:                      # "" means an open network
            d["pass"] = password
        return self._call("/api/wifi/ap_start", d)

    def ap_stop(self):
        return self._call("/api/wifi/ap_stop", {})

    def ap_status(self):
        return self._call("/api/wifi/ap_status")

    def scan(self):
        return self._call("/api/wifi/scan", timeout=90)

    # --- WiFi STA ---

    def sta_join(self, ssid, password=None, timeout=20):
        d = {"ssid": ssid, "timeout": timeout}
        if password:
            d["pass"] = password
        return self._call("/api/wifi/sta_join", d, timeout=timeout + 40)

    def sta_leave(self):
        return self._call("/api/wifi/sta_leave", {}, timeout=40)

    def wait_for_station(self, timeout=60):
        """Wait for a device to be reachable on the bench AP.

        `ap_status` reports leases, and it lags the radio -- worse, a device that
        re-associates after a brief outage keeps its lease and never issues a
        fresh DHCP request, so the list can stay empty while the device is back.
        Fall back to addressing the last device we saw: if it answers over the
        relay, it is associated whatever the lease table says.

        Raises on timeout; WIFI-503 asserts that no device appears.
        """
        end = time.time() + timeout
        while time.time() < end:
            stations = self.ap_status().get("stations") or []
            if stations:
                self._last_station = stations[0]
                return stations[0]
            if self._last_station:
                ip = self._last_station["ip"]
                if self._relay("GET", f"http://{ip}/api/status",
                               timeout=5, retry_for=0).status_code == 200:
                    return self._last_station
            time.sleep(3)
        raise TimeoutError_(f"no station joined the bench AP within {timeout}s")

    # --- HTTP relay to a device on the bench network ---

    def _relay(self, method, url, json=None, timeout=15, retry_for=25):
        """Relay one HTTP request, retrying while the device refuses.

        A DUT takes its DHCP lease before its web server is listening, so the
        first request after an association routinely fails. Retry briefly
        rather than reporting that as the device's answer.
        """
        d = {"method": method, "url": url, "timeout": timeout}
        if json is not None:
            d["headers"] = {"Content-Type": "application/json"}
            d["body"] = base64.b64encode(_dumps(json).encode()).decode()

        deadline = time.time() + retry_for
        while True:
            try:
                r = self._call("/api/wifi/http", d, timeout=timeout + 40)
            except urllib.error.HTTPError as e:
                return Response(e.code, e.read().decode(errors="replace"))
            if r.get("ok") or time.time() >= deadline:
                text = (base64.b64decode(r["body"]).decode(errors="replace")
                        if r.get("body") else r.get("error", ""))
                return Response(r.get("status", 0), text)
            time.sleep(3)

    def http_get(self, url, timeout=15, **kw):
        return self._relay("GET", url, timeout=timeout)

    def http_post(self, url, json=None, timeout=15, **kw):
        return self._relay("POST", url, json=json, timeout=timeout)

    # --- serial ---

    def serial_reset(self, slot=None, timeout=90):
        r = self._call("/api/serial/reset", {"slot": self._slot(slot)}, timeout=timeout)
        out = r.get("output") or []
        if isinstance(out, str):          # a JTAG reset answers with one string
            out = out.splitlines()
        r["output"] = out
        return r

    def serial_monitor(self, slot=None, pattern=None, timeout=10):
        d = {"slot": self._slot(slot), "timeout": timeout}
        if pattern:
            d["pattern"] = pattern
        r = self._call("/api/serial/monitor", d, timeout=timeout + 40)
        out = r.get("output") or []
        if isinstance(out, str):
            out = out.splitlines()
        r["output"] = out
        return r

    def get_slot(self, slot=None):
        label = self._slot(slot)
        for s in self._call("/api/devices")["slots"]:
            if s["label"] == label:
                return s
        raise KeyError(label)

    # --- GPIO ---

    def gpio_set(self, pin, value):
        return self._call("/api/gpio/set", {"pin": pin, "value": value}, timeout=20)

    def gpio_status(self):
        return self._call("/api/gpio/status")


def _dumps(obj):
    return json.dumps(obj)
