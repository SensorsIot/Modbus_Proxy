FROM mcr.microsoft.com/devcontainers/base:debian

# Rename vscode user to dev (base image has vscode with UID 1000)
RUN usermod -l dev -d /home/dev -m vscode \
    && groupmod -n dev vscode \
    && sed -i 's/vscode/dev/g' /etc/sudoers.d/vscode || true \
    && mv /etc/sudoers.d/vscode /etc/sudoers.d/dev 2>/dev/null || true

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

# SSH quiet config
RUN mkdir -p /home/dev/.ssh \
    && printf 'Host *\n    StrictHostKeyChecking accept-new\n    LogLevel ERROR\n' > /home/dev/.ssh/config \
    && chmod 700 /home/dev/.ssh \
    && chmod 600 /home/dev/.ssh/config \
    && chown -R dev:dev /home/dev/.ssh

USER dev

# Set default working directory for SSH logins
WORKDIR /workspaces/Modbus_Proxy
RUN echo 'cd /workspaces/Modbus_Proxy 2>/dev/null' >> /home/dev/.bashrc

# tmux — terminal multiplexer for SSH sessions
USER root
RUN apt-get update && apt-get install -y --no-install-recommends tmux \
    && apt-get clean && rm -rf /var/lib/apt/lists/*
USER dev

# Per-user npm prefix on PATH so claude (installed into ~/.npm-global/bin by
# postCreate) and the superclaude alias resolve. MUST come before the tmux guard
# below: the guard execs tmux, so bashrc lines after it never run on SSH login.
RUN echo 'export PATH="$HOME/.npm-global/bin:$PATH"' >> /home/dev/.bashrc

# Force UTF-8 locale so tmux renders Unicode block glyphs (claude's splash logo)
# correctly. Must also come before the tmux guard.
RUN echo 'export LANG=C.UTF-8' >> /home/dev/.bashrc

# Persistent claude session: interactive SSH logins auto-attach a tmux session
# "main" running claude, so it survives disconnects (reconnect = continue).
RUN printf '%s\n' \
    '' \
    '# --- Persistent claude session (auto-attach tmux on interactive SSH) ---' \
    'if [ -n "$SSH_CONNECTION" ] && [ -z "$TMUX" ] && [ "$TERM_PROGRAM" != "vscode" ] && [ ! -f "$HOME/.no-tmux" ] && command -v tmux >/dev/null 2>&1; then' \
    '    case "$-" in' \
    '        *i*) exec tmux new-session -A -s main -c /workspaces/Modbus_Proxy "claude --dangerously-skip-permissions; exec bash" ;;' \
    '    esac' \
    'fi' \
    >> /home/dev/.bashrc
