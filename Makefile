.PHONY: build clean lint

# Default environment for CI builds (no USB required)
ENV ?= esp32-c3-serial
PROJECT_DIR := Modbus_Proxy

build:
	pio run -e $(ENV) --project-dir $(PROJECT_DIR)

clean:
	pio run -t clean --project-dir $(PROJECT_DIR)

# Install dependencies (useful for CI)
install:
	pip install platformio
	pio pkg install --project-dir $(PROJECT_DIR)
