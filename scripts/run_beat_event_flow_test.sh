#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/.pio/build/host-tests"
BIN="${BUILD_DIR}/beat_event_flow_test"

mkdir -p "${BUILD_DIR}"

c++ -std=c++17 -Wall -Wextra -pedantic \
  "${ROOT_DIR}/test/beat_event_flow_test.cpp" \
  "${ROOT_DIR}/main/beat_scheduler_policy.cpp" \
  -I"${ROOT_DIR}/main" \
  -o "${BIN}"

"${BIN}"
