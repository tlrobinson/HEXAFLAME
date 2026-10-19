#!/usr/bin/env sh
# Build and run the host-side protocol tests (no PlatformIO or hardware needed).
set -eu
cd "$(dirname "$0")/../.."
mkdir -p build
c++ -std=c++11 -Wall -Wextra -O1 \
  -I lib/hexaflame_link/src \
  test/host/test_modbus.cpp lib/hexaflame_link/src/*.cpp \
  -o build/test_modbus
./build/test_modbus
