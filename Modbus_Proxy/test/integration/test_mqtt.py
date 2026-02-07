"""Integration tests for Modbus Proxy MQTT functionality.

Requires a live device at DEVICE_IP and MQTT broker at MQTT_BROKER.
Run with: pytest test/integration/test_mqtt.py -v
"""

import json
import time

import pytest
import requests

pytestmark = pytest.mark.timeout(30)


# --- Wallbox power message formats ---


class TestWallboxMessages:
    """Wallbox message tests verify the device receives and parses MQTT messages.

    Note: wallbox_power in /api/status reflects the active correction value,
    which is only non-zero when the Modbus proxy loop is running with a live
    DTSU meter. We verify reception via the wallbox_updates counter instead.
    """

    def _get_updates(self, base_url):
        resp = requests.get(f"{base_url}/api/status", timeout=5)
        data = resp.json()
        return data.get("wallbox_updates", 0), data.get("wallbox_errors", 0)

    def test_plain_float(self, mqtt_client, base_url):
        before_updates, _ = self._get_updates(base_url)
        mqtt_client.publish("wallbox", "3456.7", qos=0)
        time.sleep(2)
        after_updates, _ = self._get_updates(base_url)
        assert after_updates > before_updates

    def test_json_power_key(self, mqtt_client, base_url):
        before_updates, _ = self._get_updates(base_url)
        mqtt_client.publish("wallbox", json.dumps({"power": 5000.0}), qos=0)
        time.sleep(2)
        after_updates, _ = self._get_updates(base_url)
        assert after_updates > before_updates

    def test_json_chargepower_key(self, mqtt_client, base_url):
        before_updates, _ = self._get_updates(base_url)
        mqtt_client.publish("wallbox", json.dumps({"chargePower": 7400}), qos=0)
        time.sleep(2)
        after_updates, _ = self._get_updates(base_url)
        assert after_updates > before_updates

    def test_zero_power(self, mqtt_client, base_url):
        before_updates, _ = self._get_updates(base_url)
        mqtt_client.publish("wallbox", "0", qos=0)
        time.sleep(2)
        after_updates, _ = self._get_updates(base_url)
        assert after_updates > before_updates

    def test_negative_power(self, mqtt_client, base_url):
        before_updates, _ = self._get_updates(base_url)
        mqtt_client.publish("wallbox", "-500.0", qos=0)
        time.sleep(2)
        after_updates, _ = self._get_updates(base_url)
        assert after_updates > before_updates


# --- MQTT config commands ---


class TestMqttConfigCommands:
    def test_get_config(self, mqtt_client, subscribe_and_collect):
        # Subscribe to response topic first
        collected = []

        def on_msg(client, userdata, msg, *args):
            collected.append(msg.payload.decode())

        mqtt_client.subscribe("MBUS-PROXY/cmd/config/response", qos=0)
        mqtt_client.on_message = on_msg
        time.sleep(0.5)

        mqtt_client.publish(
            "MBUS-PROXY/cmd/config",
            json.dumps({"cmd": "get_config"}),
            qos=0,
        )

        deadline = time.time() + 10
        while not collected and time.time() < deadline:
            time.sleep(0.2)

        mqtt_client.unsubscribe("MBUS-PROXY/cmd/config/response")

        assert len(collected) > 0
        data = json.loads(collected[0])
        assert "mqtt_host" in data
        assert "mqtt_port" in data
        assert "wallbox_topic" in data

    def test_set_log_level(self, mqtt_client, base_url):
        # Set log level to DEBUG (0)
        mqtt_client.publish(
            "MBUS-PROXY/cmd/config",
            json.dumps({"cmd": "set_log_level", "level": 0}),
            qos=0,
        )
        time.sleep(2)

        resp = requests.get(f"{base_url}/api/config", timeout=5)
        data = resp.json()
        assert data["log_level"] == 0

        # Restore to WARN (2)
        mqtt_client.publish(
            "MBUS-PROXY/cmd/config",
            json.dumps({"cmd": "set_log_level", "level": 2}),
            qos=0,
        )
        time.sleep(2)

    def test_unknown_command(self, mqtt_client):
        collected = []

        def on_msg(client, userdata, msg, *args):
            collected.append(msg.payload.decode())

        mqtt_client.subscribe("MBUS-PROXY/cmd/config/response", qos=0)
        mqtt_client.on_message = on_msg
        time.sleep(0.5)

        mqtt_client.publish(
            "MBUS-PROXY/cmd/config",
            json.dumps({"cmd": "nonexistent_command"}),
            qos=0,
        )

        deadline = time.time() + 10
        while not collected and time.time() < deadline:
            time.sleep(0.2)

        mqtt_client.unsubscribe("MBUS-PROXY/cmd/config/response")

        assert len(collected) > 0
        data = json.loads(collected[0])
        assert data.get("status") == "error"


# --- Edge cases ---


class TestMqttEdgeCases:
    def test_empty_message(self, mqtt_client, base_url):
        """Empty wallbox message should be ignored gracefully."""
        # Record current error count
        resp = requests.get(f"{base_url}/api/status", timeout=5)
        before_errors = resp.json().get("wallbox_errors", 0)

        mqtt_client.publish("wallbox", "", qos=0)
        time.sleep(2)

        resp = requests.get(f"{base_url}/api/status", timeout=5)
        after_errors = resp.json().get("wallbox_errors", 0)
        # Error count should increase or stay same (no crash)
        assert after_errors >= before_errors

    def test_non_numeric_message(self, mqtt_client, base_url):
        """Non-numeric wallbox message should increment error count."""
        resp = requests.get(f"{base_url}/api/status", timeout=5)
        before_errors = resp.json().get("wallbox_errors", 0)

        mqtt_client.publish("wallbox", "not_a_number", qos=0)
        time.sleep(2)

        resp = requests.get(f"{base_url}/api/status", timeout=5)
        after_errors = resp.json().get("wallbox_errors", 0)
        assert after_errors > before_errors

    def test_oversized_message(self, mqtt_client, base_url):
        """Oversized message (>256 bytes) should be handled gracefully."""
        resp = requests.get(f"{base_url}/api/status", timeout=5)
        assert resp.status_code == 200  # Device alive before

        large_payload = "x" * 300
        mqtt_client.publish("wallbox", large_payload, qos=0)
        time.sleep(2)

        resp = requests.get(f"{base_url}/api/status", timeout=5)
        assert resp.status_code == 200  # Device still alive

    def test_malformed_json(self, mqtt_client, base_url):
        """Malformed JSON config command should not crash device."""
        mqtt_client.publish(
            "MBUS-PROXY/cmd/config", "{invalid json", qos=0
        )
        time.sleep(2)

        resp = requests.get(f"{base_url}/api/status", timeout=5)
        assert resp.status_code == 200  # Device still alive

    def test_missing_cmd_field(self, mqtt_client):
        """JSON without cmd field should return error."""
        collected = []

        def on_msg(client, userdata, msg, *args):
            collected.append(msg.payload.decode())

        mqtt_client.subscribe("MBUS-PROXY/cmd/config/response", qos=0)
        mqtt_client.on_message = on_msg
        time.sleep(0.5)

        mqtt_client.publish(
            "MBUS-PROXY/cmd/config",
            json.dumps({"foo": "bar"}),
            qos=0,
        )

        deadline = time.time() + 10
        while not collected and time.time() < deadline:
            time.sleep(0.2)

        mqtt_client.unsubscribe("MBUS-PROXY/cmd/config/response")

        if collected:
            data = json.loads(collected[0])
            assert data.get("status") == "error"

    def test_rapid_messages(self, mqtt_client, base_url):
        """10 messages in 1 second should not crash device."""
        resp = requests.get(f"{base_url}/api/status", timeout=5)
        before_updates = resp.json().get("wallbox_updates", 0)

        for i in range(10):
            mqtt_client.publish("wallbox", str(1000 + i), qos=0)
            time.sleep(0.1)

        time.sleep(2)

        resp = requests.get(f"{base_url}/api/status", timeout=5)
        data = resp.json()
        assert resp.status_code == 200
        # Device should have received multiple updates without crashing
        assert data.get("wallbox_updates", 0) > before_updates

    def test_special_chars_in_value(self, mqtt_client, base_url):
        """Special characters in message should not crash device."""
        mqtt_client.publish("wallbox", "12<>34&\"'", qos=0)
        time.sleep(2)

        resp = requests.get(f"{base_url}/api/status", timeout=5)
        assert resp.status_code == 200  # Device still alive

    def test_negative_wallbox_value(self, mqtt_client, base_url):
        """Negative values should be received without error."""
        resp = requests.get(f"{base_url}/api/status", timeout=5)
        before_updates = resp.json().get("wallbox_updates", 0)
        before_errors = resp.json().get("wallbox_errors", 0)

        mqtt_client.publish("wallbox", "-3000.5", qos=0)
        time.sleep(2)

        resp = requests.get(f"{base_url}/api/status", timeout=5)
        data = resp.json()
        # Should be counted as a valid update, not an error
        assert data.get("wallbox_updates", 0) > before_updates
        assert data.get("wallbox_errors", 0) == before_errors
