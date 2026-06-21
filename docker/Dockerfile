FROM ubuntu:24.04

# Install system dependencies in two RUN steps to reduce memory pressure
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    python3 python3-pip python3-venv build-essential git curl wget mc vim nano udev \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

RUN DEBIAN_FRONTEND=noninteractive apt-get update && apt-get install -y --no-install-recommends \
    clang-format-18 clang-tidy-18 \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /home/ubuntu/.platformio \
    && chown -R ubuntu:ubuntu /home/ubuntu

# Install PlatformIO
RUN pip3 install -U platformio --break-system-packages

USER ubuntu
WORKDIR /app

# Optional: pre-install ESP32 platform to speed up first build
RUN pio pkg install --global --platform espressif32