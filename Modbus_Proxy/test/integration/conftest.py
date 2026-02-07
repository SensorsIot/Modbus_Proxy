"""Fixtures for Modbus Proxy integration tests."""

import json
import os
import time

import paho.mqtt.client as mqtt
import pytest
import requests


@pytest.fixture(scope="session")
def device_ip():
    """Device IP from env var or default."""
    return os.environ.get("DEVICE_IP", "192.168.0.177")


@pytest.fixture(scope="session")
def base_url(device_ip):
    """Base HTTP URL for the device."""
    return f"http://{device_ip}"


@pytest.fixture(scope="session")
def mqtt_broker():
    """MQTT broker address from env var or default."""
    return os.environ.get("MQTT_BROKER", "192.168.0.203")


@pytest.fixture(scope="session")
def mqtt_port():
    """MQTT broker port from env var or default."""
    return int(os.environ.get("MQTT_PORT", "1883"))


@pytest.fixture
def mqtt_client(mqtt_broker, mqtt_port):
    """Connected MQTT client, auto-disconnect on teardown."""
    client = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id=f"test-{time.time():.0f}",
        protocol=mqtt.MQTTv311,
    )
    client.username_pw_set("admin", "admin")
    client.connect(mqtt_broker, mqtt_port, keepalive=30)
    client.loop_start()
    time.sleep(0.5)  # Wait for connection
    yield client
    client.loop_stop()
    client.disconnect()


@pytest.fixture
def original_config(base_url):
    """Capture and restore device config around test."""
    resp = requests.get(f"{base_url}/api/config", timeout=5)
    config = resp.json()
    yield config
    # Restore original config after test
    requests.post(
        f"{base_url}/api/config",
        json={"type": "wallbox", "topic": config.get("wallbox_topic", "wallbox")},
        timeout=5,
    )
    requests.post(
        f"{base_url}/api/config",
        json={"type": "loglevel", "level": config.get("log_level", 2)},
        timeout=5,
    )


@pytest.fixture
def wait_for_status(base_url):
    """Helper: poll /api/status until a condition is met or timeout."""

    def _wait(key, expected, timeout=10, interval=1):
        deadline = time.time() + timeout
        while time.time() < deadline:
            resp = requests.get(f"{base_url}/api/status", timeout=5)
            data = resp.json()
            if data.get(key) == expected:
                return data
            time.sleep(interval)
        # Return last status even if condition not met
        return data

    return _wait


@pytest.fixture
def subscribe_and_collect(mqtt_client):
    """Subscribe to a topic and collect messages."""

    collected = []

    def _subscribe(topic, count=1, timeout=10):
        collected.clear()

        def on_message(client, userdata, msg, *args):
            collected.append(msg.payload.decode("utf-8", errors="replace"))

        mqtt_client.subscribe(topic, qos=0)
        mqtt_client.on_message = on_message

        deadline = time.time() + timeout
        while len(collected) < count and time.time() < deadline:
            time.sleep(0.1)

        mqtt_client.unsubscribe(topic)
        return list(collected)

    return _subscribe
