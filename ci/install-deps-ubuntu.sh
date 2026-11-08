#!/usr/bin/env bash
# Installs build dependencies for quill-qt on Ubuntu 24.04.
set -euo pipefail
SUDO=""
if [ "$(id -u)" -ne 0 ]; then SUDO="sudo"; fi
$SUDO apt-get update
$SUDO DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build \
    qt6-base-dev qt6-base-dev-tools \
    libgl1-mesa-dev libxkbcommon-dev \
    xvfb xauth
