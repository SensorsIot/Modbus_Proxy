FROM mcr.microsoft.com/devcontainers/base:debian

# Install required packages for PlatformIO and ESP32 development
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    libffi-dev \
    libssl-dev \
    udev \
    usbutils \
    minicom \
    screen \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Add udev rules for ESP32 USB serial adapters
RUN echo 'SUBSYSTEMS=="usb", ATTRS{idVendor}=="303a", MODE="0666"' > /etc/udev/rules.d/99-esp32.rules \
    && echo 'SUBSYSTEMS=="usb", ATTRS{idVendor}=="10c4", MODE="0666"' >> /etc/udev/rules.d/99-esp32.rules \
    && echo 'SUBSYSTEMS=="usb", ATTRS{idVendor}=="1a86", MODE="0666"' >> /etc/udev/rules.d/99-esp32.rules

# PlatformIO PATH (installed via postCreateCommand after Python feature)
ENV PATH="/home/vscode/.local/bin:${PATH}"
