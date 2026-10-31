# Build environment for the Linux ReXGlue SDK (scripts/build-rexglue.sh).
# Mirrors ReXGlue's own CI (.github/workflows/_build-platform.yaml): Ubuntu
# 22.04 keeps the glibc floor at 2.35, and g++-13's libstdc++ (22.04's 11 has
# no <format>) keeps libstdc++ at 3.4.32, so the game runs on older distros.
FROM docker.io/library/ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl wget gnupg git software-properties-common lsb-release

# clang 20 from apt.llvm.org, as the CI uses.
RUN wget -qO /tmp/llvm.sh https://apt.llvm.org/llvm.sh && chmod +x /tmp/llvm.sh && \
    /tmp/llvm.sh 20 && apt-get install -y clang-20 lld-20 && rm /tmp/llvm.sh

# g++-13 for its libstdc++; a CMake new enough for the 3.25 minimum.
RUN add-apt-repository -y ppa:ubuntu-toolchain-r/test && \
    wget -qO- https://apt.kitware.com/keys/kitware-archive-latest.asc | \
        gpg --dearmor -o /usr/share/keyrings/kitware.gpg && \
    echo "deb [signed-by=/usr/share/keyrings/kitware.gpg] https://apt.kitware.com/ubuntu/ jammy main" \
        > /etc/apt/sources.list.d/kitware.list && \
    apt-get update && apt-get install -y \
        cmake ninja-build build-essential g++-13 unzip zip autoconf python3-venv \
        libgtk-3-dev libx11-xcb-dev libxss-dev libvulkan-dev \
        libwayland-dev libwayland-bin wayland-protocols libxkbcommon-dev libdecor-0-dev \
        libasound2-dev libpulse-dev libpipewire-0.3-dev && \
    rm -rf /var/lib/apt/lists/*

RUN git config --global --add safe.directory '*'
