FROM mcr.microsoft.com/devcontainers/base:debian

# Create dev user (UID 1000 to match host)
ARG USERNAME=dev
ARG USER_UID=1000
ARG USER_GID=$USER_UID

RUN groupadd --gid $USER_GID $USERNAME \
    && useradd --uid $USER_UID --gid $USER_GID -m $USERNAME \
    && apt-get update \
    && apt-get install -y sudo \
    && echo $USERNAME ALL=\(root\) NOPASSWD:ALL > /etc/sudoers.d/$USERNAME \
    && chmod 0440 /etc/sudoers.d/$USERNAME

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

# PlatformIO PATH
ENV PATH="/home/dev/.local/bin:${PATH}"

USER $USERNAME
